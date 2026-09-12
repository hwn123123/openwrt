"""Safe FM350 radio controls for the Photonicat vendor web interface.

All AT traffic is queued through the vendor ``PcModemClient``.  This module
never opens a tty itself, so status polling, SMS and radio controls cannot race
each other for the modem response stream.
"""

import json
import os
import re
import threading
import time
from pathlib import Path

from flask import jsonify, request


CONFIG_PATH = Path("/etc/pcat-radio-control.json")
_CONFIG_LOCK = threading.Lock()
_REGISTERED = False


def _default_config():
    return {
        "cell_lock": {
            "enabled": False,
            "rat": "",
            "arfcn": "",
            "pci": "",
            "lock_mode": 3,
            "reapply": True,
            "verification": "off",
            "message": "未锁定",
            "updated": 0,
            "verify_deadline": 0,
        },
        "operator": {
            "mode": "auto",
            "plmn": "",
            "updated": 0,
        },
    }


def _load_config():
    result = _default_config()
    try:
        raw = json.loads(CONFIG_PATH.read_text(encoding="utf-8"))
    except (OSError, TypeError, ValueError):
        raw = {}
    if isinstance(raw, dict):
        if isinstance(raw.get("cell_lock"), dict):
            result["cell_lock"].update(raw["cell_lock"])
        if isinstance(raw.get("operator"), dict):
            result["operator"].update(raw["operator"])
    return result


def _save_config(config):
    CONFIG_PATH.parent.mkdir(mode=0o755, parents=True, exist_ok=True)
    temporary = CONFIG_PATH.with_name(CONFIG_PATH.name + ".tmp")
    payload = json.dumps(config, ensure_ascii=False, indent=2) + "\n"
    temporary.write_text(payload, encoding="utf-8")
    os.chmod(temporary, 0o600)
    os.replace(temporary, CONFIG_PATH)


def _update_config(callback):
    with _CONFIG_LOCK:
        config = _load_config()
        callback(config)
        _save_config(config)
        return config


def _int(value, default=None):
    try:
        return int(str(value).strip())
    except (TypeError, ValueError):
        return default


def _valid_imei(value):
    """Accept a 15-digit IMEI only when its Luhn check digit is valid."""
    if not re.fullmatch(r"\d{15}", value):
        return False
    total = 0
    for index, character in enumerate(value):
        digit = int(character)
        if index % 2:
            digit *= 2
            digit = digit // 10 + digit % 10
        total += digit
    return total % 10 == 0


def _radio_name(number):
    return {2: "WCDMA", 4: "LTE", 9: "NR5G"}.get(number, "RAT{}".format(number))


def _band_name(raw):
    number = _int(raw, 0)
    if 101 <= number <= 171:
        return "LTE B{}".format(number - 100)
    text = str(number)
    if number >= 501 and text.startswith("50"):
        return "NR n{}".format(text[2:])
    return ""


def _quality(rat, sinr_raw, rsrp_raw, rsrq_raw):
    sinr = _int(sinr_raw)
    rsrp = _int(rsrp_raw)
    rsrq = _int(rsrq_raw)
    if sinr == 255:
        sinr = None
    if rsrp == 255:
        rsrp = None
    if rsrq == 255:
        rsrq = None
    if rat == 9:
        return {
            "sinr_db": None if sinr is None else -23 + sinr * 0.5,
            "rsrp_dbm": None if rsrp is None else -156 + rsrp,
            "rsrq_db": None if rsrq is None else -43 + rsrq * 0.5,
        }
    return {
        "sinr_db": None if sinr is None else sinr * 0.5 - 50,
        "rsrp_dbm": None if rsrp is None else -140 + rsrp,
        "rsrq_db": None if rsrq is None else -19.5 + rsrq * 0.5,
    }


def parse_gtccinfo(response):
    """Parse every LTE/NR row returned by the documented +GTCCINFO API."""
    if isinstance(response, bytes):
        response = response.decode("utf-8", errors="ignore")
    cells = []
    for raw_line in str(response or "").replace("\r", "\n").split("\n"):
        line = raw_line.strip()
        if not line or line in ("OK", "ERROR", "+GTCCINFO:") or line.startswith("AT+GTCCINFO"):
            continue
        fields = [item.strip() for item in line.split(",")]
        if len(fields) < 12 or fields[0] not in ("1", "2"):
            continue
        rat = _int(fields[1])
        if rat not in (4, 9):
            continue
        serving = fields[0] == "1"
        cell = {
            "serving": serving,
            "rat": _radio_name(rat),
            "rat_code": rat,
            "mcc": fields[2],
            "mnc": fields[3],
            "tac": fields[4],
            "cell_id": fields[5],
            "arfcn": fields[6],
            "pci": fields[7],
            "band": _band_name(fields[8]) if serving else "",
            "bandwidth_code": fields[9] if serving or rat == 4 else "",
        }
        if serving:
            quality = _quality(rat, fields[10], fields[12], fields[13]) if len(fields) >= 14 else {}
        elif rat == 9:
            quality = _quality(rat, fields[8], fields[10], fields[11])
        else:
            quality = _quality(rat, None, fields[10], fields[11])
        cell.update(quality)
        cells.append(cell)
    cells.sort(key=lambda item: (not item["serving"], item["rat"], _int(item["arfcn"], 0), _int(item["pci"], 0)))
    return cells


def _queue(client, command, timeout=10):
    client.cmd_queue_append(command, timeout=timeout)


def _clear_serving_sample(client):
    for key in (
            "serving_cellid", "serving_rat", "serving_pci", "serving_tac",
            "serving_band", "serving_arfcn", "serving_duplex",
            "serving_bandwidth", "serving_rsrp_dbm", "serving_rsrq_db",
            "serving_sinr_db", "modem_serving_info", "modem_serving_quality",
            "cell_mccmnc", "operator_mode", "operator_rat", "isp_details",
            "cell_tech", "network_info", "cell_band", "cell_carrier_info"):
        client.basic[key] = ""
    client.basic["radio_cells"] = []
    client.basic["carrier_components"] = []


def _queue_fresh_radio_state(client):
    _queue(client, "AT+EMMCHLCK?")
    _queue(client, "AT+GTCCINFO?")
    _queue(client, "AT+GTCAINFO?")
    _queue(client, "AT+COPS=3,2")
    _queue(client, "AT+COPS?")


def _queue_unlock(client):
    _clear_serving_sample(client)
    _queue(client, "AT+CFUN=0", timeout=20)
    _queue(client, "AT+EMMCHLCK=0")
    _queue(client, "AT+CFUN=1", timeout=20)
    _queue_fresh_radio_state(client)


def _queue_cell_lock(client, lock):
    rat_number = 7 if lock["rat"] == "LTE" else 11
    pci = str(lock.get("pci") or "")
    command = "AT+EMMCHLCK=1,{rat},0,{arfcn},{pci},{mode}".format(
        rat=rat_number,
        arfcn=lock["arfcn"],
        pci=pci,
        mode=lock.get("lock_mode", 3),
    )
    _clear_serving_sample(client)
    _queue(client, "AT+CFUN=0", timeout=20)
    _queue(client, "AT+EMMCHLCK=0")
    _queue(client, command)
    _queue(client, "AT+CFUN=1", timeout=20)
    _queue_fresh_radio_state(client)


def _is_fm350(client):
    return "FM350" in str(client.basic.get("model", "")).upper()


def _modem_ready(app_module):
    client = getattr(app_module, "modem_client", None)
    if client is None:
        return False
    try:
        with client.mutex:
            valid = bool(client.modem_valid)
    except Exception:
        valid = False
    try:
        powered = bool(app_module.is_wwan_powered())
    except Exception:
        powered = False
    return valid and powered and _is_fm350(client)


def _target_matches(client, lock):
    if str(client.basic.get("serving_arfcn", "")) != str(lock.get("arfcn", "")):
        return False
    pci = str(lock.get("pci") or "")
    return not pci or str(client.basic.get("serving_pci", "")) == pci


def _wrap_cell_parser(client):
    if getattr(client, "_pcat_radio_parser_wrapped", False):
        return
    original = client.handle_gtccinfo

    def handle_with_neighbors(response):
        cells = parse_gtccinfo(response)
        client.basic["radio_cells"] = cells
        client.basic["radio_cells_updated"] = int(time.time())
        original(response)
        if not cells:
            return
        with _CONFIG_LOCK:
            config = _load_config()
            lock = config["cell_lock"]
            if lock.get("enabled") and _target_matches(client, lock):
                lock["verification"] = "active"
                lock["message"] = "已在目标频点驻网" if not lock.get("pci") else "已在目标小区驻网"
                lock["verify_deadline"] = 0
                lock["updated"] = int(time.time())
                _save_config(config)

    client.handle_gtccinfo = handle_with_neighbors
    client._pcat_radio_parser_wrapped = True


def _background_worker(app_module):
    client = None
    previous_ready = False
    reapply_at = 0
    while True:
        current = getattr(app_module, "modem_client", None)
        if current is not None and current is not client:
            client = current
            _wrap_cell_parser(client)
            previous_ready = False

        ready = bool(client is not None and _modem_ready(app_module))
        now = int(time.time())
        if ready and not previous_ready:
            reapply_at = now + 6
        previous_ready = ready

        if ready:
            with _CONFIG_LOCK:
                config = _load_config()
                lock = dict(config["cell_lock"])

            if reapply_at and now >= reapply_at:
                reapply_at = 0
                if lock.get("enabled") and lock.get("reapply", True):
                    _queue_cell_lock(client, lock)
                    def mark_pending(saved):
                        saved_lock = saved["cell_lock"]
                        saved_lock["verification"] = "pending"
                        saved_lock["message"] = "已在模组上线后重新应用，等待驻网确认"
                        saved_lock["verify_deadline"] = int(time.time()) + 90
                        saved_lock["updated"] = int(time.time())
                    _update_config(mark_pending)

                # Preserve an explicitly selected PLMN when the modem or the
                # router restarts.  The vendor polling loop otherwise restores
                # automatic operator selection once per process.
                with _CONFIG_LOCK:
                    operator = dict(_load_config()["operator"])
                if operator.get("mode") == "manual" and re.fullmatch(
                        r"\d{5,6}", str(operator.get("plmn", ""))):
                    client.basic["fm350_cops_auto_set"] = True
                    _queue(
                        client,
                        'AT+COPS=1,2,"{}"'.format(operator["plmn"]),
                        timeout=120,
                    )
                    _queue(client, "AT+COPS=3,2")
                    _queue(client, "AT+COPS?")

            deadline = _int(lock.get("verify_deadline"), 0) or 0
            if lock.get("enabled") and lock.get("verification") == "pending" and deadline and now >= deadline:
                if _target_matches(client, lock):
                    def mark_active(saved):
                        saved["cell_lock"].update({
                            "verification": "active", "message": "已在目标小区驻网",
                            "verify_deadline": 0, "updated": int(time.time()),
                        })
                    _update_config(mark_active)
                else:
                    _queue_unlock(client)
                    def mark_failed(saved):
                        saved["cell_lock"].update({
                            "enabled": False, "verification": "auto_unlocked",
                            "message": "90 秒内未驻留到目标，已自动解锁",
                            "verify_deadline": 0, "updated": int(time.time()),
                        })
                    _update_config(mark_failed)
        time.sleep(2)


def _state(app_module):
    client = getattr(app_module, "modem_client", None)
    with _CONFIG_LOCK:
        config = _load_config()
    basic = client.basic if client is not None else {}
    online = _modem_ready(app_module)
    cells = (basic.get("radio_cells") or []) if online else []
    if online and not cells and basic.get("serving_arfcn"):
        cells = [{
            "serving": True,
            "rat": basic.get("serving_rat", ""),
            "rat_code": 9 if basic.get("serving_rat") == "NR5G" else 4,
            "mcc": str(basic.get("cell_mccmnc", ""))[:3],
            "mnc": str(basic.get("cell_mccmnc", ""))[3:],
            "tac": basic.get("serving_tac", ""),
            "cell_id": basic.get("serving_cellid", ""),
            "arfcn": basic.get("serving_arfcn", ""),
            "pci": basic.get("serving_pci", ""),
            "band": basic.get("cell_band", ""),
            "bandwidth_code": basic.get("serving_bandwidth_code", ""),
            "rsrp_dbm": basic.get("serving_rsrp_dbm"),
            "rsrq_db": basic.get("serving_rsrq_db"),
            "sinr_db": basic.get("serving_sinr_db"),
        }]
    return {
        "status": "ok",
        "supported": bool(client is not None and _is_fm350(client)),
        "online": online,
        "cells": cells,
        "cells_updated": basic.get("radio_cells_updated", 0),
        "cell_lock": config["cell_lock"],
        "operator": {
            **config["operator"],
            "current_mode": basic.get("operator_mode") if online else "",
            "current_plmn": basic.get("cell_mccmnc", "") if online else "",
            "current_rat": basic.get("operator_rat", "") if online else "",
            "current_name": basic.get("isp_details", "") if online else "",
        },
        "carrier_aggregation": basic.get("carrier_components", []) if online else [],
    }


def _identity_state(app_module):
    client = getattr(app_module, "modem_client", None)
    basic = client.basic if client is not None else {}
    return {
        "status": "ok",
        "supported": bool(client is not None and _is_fm350(client)),
        "online": _modem_ready(app_module),
        "imei": str(basic.get("imei_num", "")),
        "write_status": str(basic.get("imei_write_status", "idle")),
        "message": str(basic.get(
            "imei_write_message", "当前模组尚未返回 IMEI")),
        "updated": _int(basic.get("imei_write_updated"), 0) or 0,
    }


def register_radio(flask_app, api_auth_required, app_module):
    global _REGISTERED
    if _REGISTERED:
        return
    _REGISTERED = True

    @flask_app.route("/api/v1/modem/identity.json", methods=["GET"])
    @api_auth_required
    def pcat_modem_identity_state():
        client = getattr(app_module, "modem_client", None)
        if request.args.get("refresh") in ("1", "true") and client is not None:
            try:
                powered = bool(app_module.is_wwan_powered())
            except Exception:
                powered = False
            if powered:
                if _is_fm350(client):
                    client.cmd_mark_success("AT+EGMREXT=0,7")
                    _queue(client, "AT+EGMREXT=0,7")
                else:
                    _queue(client, "ATI")
        response = jsonify(_identity_state(app_module))
        response.headers["Cache-Control"] = "no-store"
        return response

    @flask_app.route("/api/v1/modem/identity.json", methods=["POST"])
    @api_auth_required
    def pcat_modem_identity_write():
        if not _modem_ready(app_module):
            return jsonify({"status": "error", "message": "FM350 模组当前不在线"}), 409
        payload = request.get_json(silent=True) or {}
        imei = str(payload.get("imei", "")).strip()
        if not re.fullmatch(r"\d{15}", imei):
            return jsonify({"status": "error", "message": "IMEI 必须是 15 位数字"}), 400
        if not _valid_imei(imei):
            return jsonify({"status": "error", "message": "IMEI 校验位不正确，请核对模组标签"}), 400

        client = app_module.modem_client
        if str(client.basic.get("imei_num", "")) == imei:
            client.basic.update({
                "imei_write_status": "verified",
                "imei_write_message": "当前 IMEI 已与输入值一致",
                "imei_write_updated": int(time.time()),
            })
            return jsonify(_identity_state(app_module))

        client.basic.update({
            "imei_write_target": imei,
            "imei_write_status": "pending",
            "imei_write_message": "正在写入并等待模组回读",
            "imei_write_updated": int(time.time()),
        })
        client.cmd_mark_success("AT+EGMREXT=0,7")
        _queue(client, 'AT+EGMREXT=1,7,"{}"'.format(imei))
        _queue(client, "AT+EGMREXT=0,7")
        return jsonify(_identity_state(app_module))

    @flask_app.route("/api/v1/modem/radio.json", methods=["GET"])
    @api_auth_required
    def pcat_radio_state():
        response = jsonify(_state(app_module))
        response.headers["Cache-Control"] = "no-store"
        return response

    @flask_app.route("/api/v1/modem/radio/refresh.json", methods=["POST"])
    @api_auth_required
    def pcat_radio_refresh():
        if not _modem_ready(app_module):
            return jsonify({"status": "error", "message": "FM350 模组当前不在线"}), 409
        client = app_module.modem_client
        _queue(client, "AT+GTCCINFO?")
        _queue(client, "AT+GTCAINFO?")
        _queue(client, "AT+COPS=3,2")
        _queue(client, "AT+COPS?")
        _queue(client, "AT+EMMCHLCK?")
        return jsonify({"status": "ok", "message": "已开始读取邻区和载波信息"})

    @flask_app.route("/api/v1/modem/radio/cell-lock.json", methods=["POST"])
    @api_auth_required
    def pcat_radio_cell_lock():
        if not _modem_ready(app_module):
            return jsonify({"status": "error", "message": "FM350 模组当前不在线"}), 409
        payload = request.get_json(silent=True) or {}
        action = str(payload.get("action", "lock"))
        client = app_module.modem_client
        if action == "unlock":
            _queue_unlock(client)
            def disable(saved):
                saved["cell_lock"].update({
                    "enabled": False, "verification": "off", "message": "已提交解锁",
                    "verify_deadline": 0, "updated": int(time.time()),
                })
            config = _update_config(disable)
            return jsonify({"status": "ok", "message": "已提交解锁并重新驻网", "cell_lock": config["cell_lock"]})

        rat = str(payload.get("rat", "")).upper()
        arfcn = str(payload.get("arfcn", "")).strip()
        pci = str(payload.get("pci", "")).strip()
        if rat not in ("LTE", "NR5G"):
            return jsonify({"status": "error", "message": "请选择 LTE 或 NR5G"}), 400
        if not re.fullmatch(r"\d{1,7}", arfcn) or not (0 <= int(arfcn) <= 2229167):
            return jsonify({"status": "error", "message": "频点必须是有效 EARFCN/NR-ARFCN"}), 400
        if not re.fullmatch(r"\d{1,4}", pci) or not (0 <= int(pci) <= 1007):
            return jsonify({"status": "error", "message": "锁定小区必须填写 0–1007 之间的 PCI"}), 400
        lock = {
            "enabled": True,
            "rat": rat,
            "arfcn": str(int(arfcn)),
            "pci": str(int(pci)) if pci else "",
            "lock_mode": 3,
            "reapply": bool(payload.get("reapply", True)),
            "verification": "pending",
            "message": "已提交，等待目标小区驻网",
            "updated": int(time.time()),
            "verify_deadline": int(time.time()) + 90,
        }
        _queue_cell_lock(client, lock)
        def save_lock(saved):
            saved["cell_lock"] = lock
        _update_config(save_lock)
        return jsonify({"status": "ok", "message": lock["message"], "cell_lock": lock})

    @flask_app.route("/api/v1/modem/radio/operator.json", methods=["POST"])
    @api_auth_required
    def pcat_radio_operator():
        if not _modem_ready(app_module):
            return jsonify({"status": "error", "message": "FM350 模组当前不在线"}), 409
        payload = request.get_json(silent=True) or {}
        mode = str(payload.get("mode", "auto")).lower()
        plmn = str(payload.get("plmn", "")).strip()
        if mode not in ("auto", "manual"):
            return jsonify({"status": "error", "message": "无效的运营商选择模式"}), 400
        if mode == "manual" and not re.fullmatch(r"\d{5,6}", plmn):
            return jsonify({"status": "error", "message": "PLMN 必须是 5 或 6 位 MCC+MNC"}), 400
        client = app_module.modem_client
        client.basic["fm350_cops_auto_set"] = True
        if mode == "manual":
            _queue(client, 'AT+COPS=1,2,"{}"'.format(plmn), timeout=120)
            message = "已提交锁定运营商 {}".format(plmn)
        else:
            _queue(client, "AT+COPS=0", timeout=120)
            plmn = ""
            message = "已恢复自动选择运营商"
        _queue(client, "AT+COPS=3,2")
        _queue(client, "AT+COPS?")
        def save_operator(saved):
            saved["operator"] = {"mode": mode, "plmn": plmn, "updated": int(time.time())}
        _update_config(save_operator)
        return jsonify({"status": "ok", "message": message})

    threading.Thread(
        target=_background_worker,
        args=(app_module,),
        name="pcat-radio-control",
        daemon=True,
    ).start()
