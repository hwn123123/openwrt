#!/usr/bin/env python3
"""Local data sidecar for the Photonicat display and telemetry history.

The vendor backend remains the only owner of the modem AT port.  This service
reads its durable SMS database with SQLite read-only mode and exposes only the
normalized message list on loopback.  It also samples the vendor's loopback
JSON APIs into a separate bounded history database.  It never imports the
vendor application and never opens /dev/ttyUSB*.
"""

import argparse
import json
import logging
import os
import sqlite3
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.error import HTTPError, URLError
from urllib.parse import parse_qs, urlsplit
from urllib.request import Request, urlopen


LOG = logging.getLogger("pcat-sidecar")
MAX_SMS_LIMIT = 200
MAX_TELEMETRY_POINTS = 1500
VENDOR_DASHBOARD_URL = "http://127.0.0.1/api/v1/dashboard.json"
VENDOR_MODEM_URL = "http://127.0.0.1/api/v1/modem/basic.json"
TELEMETRY_INTERVAL = 30.0
TELEMETRY_RETENTION_DAYS = 7
class SmsStore:
    def __init__(self, database):
        self.database = os.path.abspath(database)

    def _connect(self):
        database_uri = "file:{}?mode=ro".format(self.database)
        connection = sqlite3.connect(database_uri, uri=True, timeout=3.0)
        connection.row_factory = sqlite3.Row
        connection.execute("PRAGMA query_only = ON")
        connection.execute("PRAGMA busy_timeout = 3000")
        return connection

    @staticmethod
    def _normalize(row):
        item = {}
        raw_json = row["raw_json"]
        if raw_json:
            try:
                parsed = json.loads(raw_json)
                if isinstance(parsed, dict):
                    item.update(parsed)
            except (TypeError, ValueError, json.JSONDecodeError):
                pass

        direction = int(row["direction"] or 0)
        timestamp = (item.get("timestamp") or item.get("send_at") or
                     row["sent_at"] or row["created_at"] or "")
        sender = item.get("sender") or row["from_num"] or ""
        content = item.get("content") or row["content"] or ""

        # Database columns are authoritative.  In particular, never allow an
        # old raw_json record with a missing/wrong direction to make a received
        # message look like a sent message to the display notifier.
        item.update({
            "id": int(row["id"]),
            "sender": "me" if direction == 1 else sender,
            "from": row["from_num"] or "",
            "to": row["to_num"] or "",
            "content": content,
            "send_at": timestamp,
            "timestamp": timestamp,
            "status": row["status"] or item.get("status", ""),
            "direction": direction,
        })
        return item

    def list_sms(self, limit):
        with self._connect() as connection:
            rows = connection.execute(
                "SELECT id, from_num, to_num, content, status, direction, "
                "sent_at, created_at, raw_json FROM sms "
                "ORDER BY created_at DESC, id DESC LIMIT ?",
                (limit,),
            ).fetchall()
        return {"msg": [self._normalize(row) for row in rows]}

    def health(self):
        result = {
            "status": "ok",
            "database": self.database,
            "database_exists": os.path.isfile(self.database),
            "read_only": True,
        }
        try:
            with self._connect() as connection:
                result["sms_count"] = int(
                    connection.execute("SELECT COUNT(*) FROM sms").fetchone()[0]
                )
        except (OSError, sqlite3.Error) as error:
            result["status"] = "error"
            result["error"] = type(error).__name__
        return result


def _number(value):
    if value is None or value == "":
        return None
    try:
        return float(value)
    except (TypeError, ValueError):
        return None


def _first_number(value):
    if value is None:
        return None
    import re
    match = re.search(r"[-+]?\d+(?:\.\d+)?", str(value))
    return float(match.group(0)) if match else None


def _modem_temperature(value, decimal_value=None):
    direct = _number(decimal_value)
    if direct is not None:
        return direct
    if isinstance(value, dict):
        for item in value.values():
            direct = _number(item)
            if direct is not None:
                return direct
    return _number(value)


class TelemetryStore:
    """Small, bounded SQLite time-series store independent of Web sessions."""

    NUMERIC_COLUMNS = (
        "down_speed", "up_speed", "rsrp_percent", "rsrq_percent",
        "sinr_percent", "csq_percent", "rsrp_dbm", "rsrq_db",
        "sinr_db", "battery_soc", "battery_voltage", "battery_power",
        "battery_current", "board_temperature", "modem_temperature",
    )
    TEXT_COLUMNS = (
        "active_egress", "cell_tech", "cell_band", "operator",
        "cell_id", "tac", "pci", "arfcn", "bandwidth", "duplex",
    )

    def __init__(self, database, retention_days=TELEMETRY_RETENTION_DAYS):
        self.database = os.path.abspath(database)
        self.retention_seconds = max(86400, int(retention_days * 86400))
        self._initialize()

    def _connect(self):
        connection = sqlite3.connect(self.database, timeout=5.0)
        connection.row_factory = sqlite3.Row
        connection.execute("PRAGMA busy_timeout = 5000")
        return connection

    def _initialize(self):
        parent = os.path.dirname(self.database)
        if parent:
            os.makedirs(parent, exist_ok=True)
        with self._connect() as connection:
            connection.execute("PRAGMA journal_mode = WAL")
            connection.execute("PRAGMA synchronous = NORMAL")
            columns = ["ts INTEGER PRIMARY KEY"]
            columns.extend(name + " REAL" for name in self.NUMERIC_COLUMNS)
            columns.extend(name + " TEXT" for name in self.TEXT_COLUMNS)
            connection.execute(
                "CREATE TABLE IF NOT EXISTS telemetry_samples ({})".format(
                    ",".join(columns)
                )
            )
            connection.execute(
                "CREATE INDEX IF NOT EXISTS telemetry_samples_ts "
                "ON telemetry_samples(ts)"
            )

    @staticmethod
    def _quality_values(modem):
        structured = modem.get("serving") or {}
        values = {
            "sinr_db": _number(structured.get("sinr_db")),
            "rsrq_db": _number(structured.get("rsrq_db")),
            "rsrp_dbm": _number(structured.get("rsrp_dbm")),
        }
        if all(item is None for item in values.values()):
            parts = str(modem.get("modem_serving_quality") or "").split(",")
            if len(parts) >= 3:
                values.update({
                    "sinr_db": _first_number(parts[0]),
                    "rsrq_db": _first_number(parts[1]),
                    "rsrp_dbm": _first_number(parts[2]),
                })
        return values

    def add(self, dashboard, modem):
        now = int(time.time())
        serving = modem.get("serving") or {}
        quality = self._quality_values(modem)
        row = {
            "ts": now,
            "down_speed": _number(dashboard.get("down_speed")),
            "up_speed": _number(dashboard.get("up_speed")),
            "rsrp_percent": _number(dashboard.get("cell_signal_percent_qrsrp")),
            "rsrq_percent": _number(dashboard.get("cell_signal_percent_qrsrq")),
            "sinr_percent": _number(dashboard.get("cell_signal_percent_sinr")),
            "csq_percent": _number(dashboard.get("cell_signal_percent_csq")),
            "rsrp_dbm": quality["rsrp_dbm"],
            "rsrq_db": quality["rsrq_db"],
            "sinr_db": quality["sinr_db"],
            "battery_soc": _number(dashboard.get("charge_percent") or dashboard.get("battery_soc")),
            "battery_voltage": _number(dashboard.get("battery_voltage_v")),
            "battery_power": _number(dashboard.get("battery_wattage_w")),
            "battery_current": _number(dashboard.get("battery_current_a")),
            "board_temperature": _number(dashboard.get("board_temperature")),
            "modem_temperature": _modem_temperature(
                modem.get("modem_temperature"),
                modem.get("modem_temperature_decimal"),
            ),
            "active_egress": str(dashboard.get("active_egress") or ""),
            "cell_tech": str(dashboard.get("cell_tech") or dashboard.get("modem_mode") or ""),
            "cell_band": str(dashboard.get("cell_band") or serving.get("band") or ""),
            "operator": str(dashboard.get("cell_isp_native_name") or dashboard.get("isp_name") or ""),
            "cell_id": str(dashboard.get("cell_id") or serving.get("cell_id") or ""),
            "tac": str(dashboard.get("cell_tac") or serving.get("tac") or ""),
            "pci": str(serving.get("pci") or ""),
            "arfcn": str(serving.get("arfcn") or ""),
            "bandwidth": str(serving.get("bandwidth") or ""),
            "duplex": str(serving.get("duplex") or ""),
        }
        columns = tuple(row)
        placeholders = ",".join("?" for _ in columns)
        update = ",".join(
            "{}=excluded.{}".format(name, name)
            for name in columns if name != "ts"
        )
        with self._connect() as connection:
            connection.execute(
                "INSERT INTO telemetry_samples ({}) VALUES ({}) "
                "ON CONFLICT(ts) DO UPDATE SET {}".format(
                    ",".join(columns), placeholders, update
                ),
                tuple(row[name] for name in columns),
            )
            connection.execute(
                "DELETE FROM telemetry_samples WHERE ts < ?",
                (now - self.retention_seconds,),
            )

    @staticmethod
    def _range_seconds(range_name):
        return {"3h": 10800, "24h": 86400, "7d": 604800}.get(range_name, 10800)

    @staticmethod
    def _bucket_seconds(range_name):
        return {"3h": 30, "24h": 120, "7d": 600}.get(range_name, 30)

    def history(self, range_name):
        seconds = self._range_seconds(range_name)
        bucket = self._bucket_seconds(range_name)
        since = int(time.time()) - seconds
        averages = ",".join(
            "AVG({0}) AS {0}".format(name) for name in self.NUMERIC_COLUMNS
        )
        query = (
            "SELECT (ts / ?) * ? AS ts,{} FROM telemetry_samples "
            "WHERE ts >= ? GROUP BY (ts / ?) ORDER BY ts ASC LIMIT ?"
        ).format(averages)
        with self._connect() as connection:
            rows = connection.execute(
                query, (bucket, bucket, since, bucket, MAX_TELEMETRY_POINTS)
            ).fetchall()
            latest = connection.execute(
                "SELECT * FROM telemetry_samples ORDER BY ts DESC LIMIT 1"
            ).fetchone()
        points = []
        for row in rows:
            item = {"ts": int(row["ts"])}
            for name in self.NUMERIC_COLUMNS:
                value = row[name]
                item[name] = round(float(value), 4) if value is not None else None
            points.append(item)
        return {
            "status": "ok",
            "range": range_name,
            "bucket_seconds": bucket,
            "retention_days": self.retention_seconds // 86400,
            "points": points,
            "latest": dict(latest) if latest is not None else None,
        }

    def health(self):
        result = {
            "database": self.database,
            "database_exists": os.path.isfile(self.database),
            "retention_days": self.retention_seconds // 86400,
        }
        try:
            with self._connect() as connection:
                row = connection.execute(
                    "SELECT COUNT(*) AS count, MIN(ts) AS oldest, "
                    "MAX(ts) AS newest FROM telemetry_samples"
                ).fetchone()
            result.update(dict(row))
            result["status"] = "ok"
        except (OSError, sqlite3.Error) as error:
            result.update({"status": "error", "error": type(error).__name__})
        return result


class TelemetryCollector(threading.Thread):
    def __init__(self, store, interval=TELEMETRY_INTERVAL):
        super().__init__(name="pcat-telemetry", daemon=True)
        self.store = store
        self.interval = max(10.0, float(interval))
        self._stop_event = threading.Event()
        self._state_lock = threading.Lock()
        self._last_success = 0.0
        self._last_error = ""
        self._consecutive_failures = 0

    def stop(self):
        self._stop_event.set()

    def status(self):
        with self._state_lock:
            return {
                "last_success": int(self._last_success),
                "last_error": self._last_error,
                "consecutive_failures": self._consecutive_failures,
                "interval_seconds": int(self.interval),
            }

    @staticmethod
    def _json(url):
        request = Request(url, headers={
            "User-Agent": "pcat-sidecar/2.0 telemetry",
            "Cache-Control": "no-cache",
        })
        with urlopen(request, timeout=12.0) as response:
            if response.status != 200:
                raise HTTPError(url, response.status, "unexpected status",
                                response.headers, None)
            return json.loads(response.read(262144).decode("utf-8"))

    def _sample(self):
        dashboard = self._json(VENDOR_DASHBOARD_URL)
        modem = self._json(VENDOR_MODEM_URL)
        self.store.add(dashboard, modem)

    def run(self):
        while not self._stop_event.is_set():
            try:
                self._sample()
                with self._state_lock:
                    recovered = self._consecutive_failures > 0
                    self._last_success = time.time()
                    self._last_error = ""
                    self._consecutive_failures = 0
                if recovered:
                    LOG.info("telemetry collection recovered")
            except (HTTPError, URLError, OSError, TimeoutError,
                    ValueError, sqlite3.Error) as error:
                name = type(error).__name__
                with self._state_lock:
                    self._consecutive_failures += 1
                    failures = self._consecutive_failures
                    self._last_error = name
                if failures == 1 or failures % 20 == 0:
                    LOG.warning("telemetry collection failed (%s, count=%d)",
                                name, failures)
            self._stop_event.wait(self.interval)


class SmsEventBroker:
    """Loopback-only change notification; SMS content remains in SQLite."""

    def __init__(self):
        self._condition = threading.Condition()
        self._version = time.time_ns()

    def notify(self):
        with self._condition:
            self._version = max(self._version + 1, time.time_ns())
            self._condition.notify_all()
            return self._version

    def wait(self, since, timeout):
        deadline = time.monotonic() + timeout
        with self._condition:
            while self._version <= since:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    break
                self._condition.wait(remaining)
            return {
                "status": "ok",
                "version": self._version,
                "changed": self._version > since,
            }

    def status(self):
        with self._condition:
            return {"version": self._version}


class SidecarServer(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True

    def __init__(self, address, store, sms_events,
                 telemetry_store=None, telemetry_collector=None):
        self.store = store
        self.sms_events = sms_events
        self.telemetry_store = telemetry_store
        self.telemetry_collector = telemetry_collector
        super().__init__(address, SidecarHandler)


class SidecarHandler(BaseHTTPRequestHandler):
    server_version = "pcat-sidecar/1.0"

    def log_message(self, fmt, *args):
        # The display polls frequently.  Keep routine requests out of logread,
        # and never log SMS query results or message metadata.
        return

    def _send_json(self, status, payload):
        data = json.dumps(
            payload, ensure_ascii=False, separators=(",", ":")
        ).encode("utf-8")
        try:
            self.send_response(status)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Cache-Control", "no-store")
            self.send_header("X-Content-Type-Options", "nosniff")
            self.end_headers()
            self.wfile.write(data)
        except (BrokenPipeError, ConnectionResetError):
            # Long-poll clients may disappear when a page closes or a service
            # restarts.  That is a normal lifecycle event, not a system error.
            return False
        return True

    def do_GET(self):
        parsed = urlsplit(self.path)
        if parsed.path == "/v1/health":
            result = self.server.store.health()
            result["sms_events"] = self.server.sms_events.status()
            if self.server.telemetry_store is not None:
                result["telemetry"] = self.server.telemetry_store.health()
            if self.server.telemetry_collector is not None:
                result["telemetry_collector"] = self.server.telemetry_collector.status()
            self._send_json(200 if result["status"] == "ok" else 503, result)
            return

        if parsed.path == "/v1/sms/events":
            try:
                query = parse_qs(parsed.query)
                since = max(0, int(query.get("since", ["0"])[0]))
                timeout = max(1.0, min(60.0, float(
                    query.get("timeout", ["55"])[0])))
                self._send_json(200, self.server.sms_events.wait(since, timeout))
            except (TypeError, ValueError):
                self._send_json(400, {
                    "status": "error", "message": "invalid event cursor"})
            return

        if parsed.path == "/v1/telemetry":
            if self.server.telemetry_store is None:
                self._send_json(503, {"status": "error", "message": "telemetry unavailable"})
                return
            try:
                range_name = parse_qs(parsed.query).get("range", ["3h"])[0]
                if range_name not in ("3h", "24h", "7d"):
                    raise ValueError("unsupported range")
                self._send_json(200, self.server.telemetry_store.history(range_name))
            except (OSError, ValueError, sqlite3.Error) as error:
                LOG.warning("telemetry database read failed: %s", type(error).__name__)
                self._send_json(503, {"status": "error", "message": "telemetry unavailable"})
            return

        if parsed.path != "/v1/sms":
            self._send_json(404, {"status": "error", "message": "not found"})
            return

        try:
            values = parse_qs(parsed.query).get("limit", ["10"])
            limit = max(1, min(MAX_SMS_LIMIT, int(values[0])))
            self._send_json(200, self.server.store.list_sms(limit))
        except (OSError, ValueError, sqlite3.Error) as error:
            LOG.warning("SMS database read failed: %s", type(error).__name__)
            self._send_json(503, {
                "status": "error",
                "message": "SMS database unavailable",
            })

    def do_POST(self):
        parsed = urlsplit(self.path)
        if parsed.path != "/v1/sms/notify":
            self._send_json(404, {"status": "error", "message": "not found"})
            return
        # Drain the deliberately empty/small local request body.
        try:
            length = min(4096, max(0, int(self.headers.get("Content-Length", "0"))))
            if length:
                self.rfile.read(length)
        except (TypeError, ValueError):
            pass
        self._send_json(200, {
            "status": "ok", "version": self.server.sms_events.notify()})


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--database", default="/etc/pc_modem.db")
    parser.add_argument("--telemetry-database", default="/etc/pcat-telemetry.db")
    parser.add_argument("--telemetry-interval", type=float, default=TELEMETRY_INTERVAL)
    parser.add_argument("--telemetry-retention-days", type=int, default=TELEMETRY_RETENTION_DAYS)
    parser.add_argument("--listen", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8092)
    return parser.parse_args()


def main():
    args = parse_args()
    if args.listen not in ("127.0.0.1", "::1", "localhost"):
        raise SystemExit("pcat-sidecar may only listen on loopback")

    logging.basicConfig(level=logging.INFO, format="%(name)s: %(message)s")
    sms_events = SmsEventBroker()
    telemetry_store = TelemetryStore(
        args.telemetry_database, retention_days=args.telemetry_retention_days
    )
    collector = TelemetryCollector(
        telemetry_store, interval=args.telemetry_interval
    )
    server = SidecarServer(
        (args.listen, args.port), SmsStore(args.database), sms_events,
        telemetry_store=telemetry_store, telemetry_collector=collector,
    )
    LOG.info("listening on %s:%d (SMS DB %s, telemetry DB %s)",
             args.listen, args.port, os.path.abspath(args.database),
             telemetry_store.database)
    collector.start()
    try:
        server.serve_forever(poll_interval=0.5)
    except KeyboardInterrupt:
        pass
    finally:
        collector.stop()
        server.server_close()


if __name__ == "__main__":
    main()
