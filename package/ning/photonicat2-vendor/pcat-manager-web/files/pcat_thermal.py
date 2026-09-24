#!/usr/bin/env python3
"""Read-only thermal, CPU and power telemetry for the vendor Web UI."""

from __future__ import annotations

import os
import re
import threading
import time
from pathlib import Path

from flask import jsonify, redirect, render_template, session


_CATEGORY_ORDER = {
    "processor": 0,
    "accelerator": 1,
    "memory": 2,
    "network": 3,
    "modem": 4,
    "board": 5,
    "storage": 6,
    "power": 7,
    "other": 8,
}

_THERMAL_LABELS = {
    "package-thermal": ("processor", "主控封装"),
    "bigcore-thermal": ("processor", "CPU 大核集群"),
    "littlecore-thermal": ("processor", "CPU 小核集群"),
    "gpu-thermal": ("accelerator", "GPU"),
    "npu-thermal": ("accelerator", "NPU"),
    "ddr-thermal": ("memory", "DDR 内存"),
}

_MODEM_LABELS = {
    "soc_max": "模组 SoC 最高温度",
    "cpu_little0": "模组 CPU 小核 0",
    "cpu_little1": "模组 CPU 小核 1",
    "cpu_little2": "模组 CPU 小核 2",
    "cpu_little3": "模组 CPU 小核 3",
    "gpu0": "模组 GPU 0",
    "gpu1": "模组 GPU 1",
    "dramc": "模组 DRAM 控制器",
    "mmsys": "模组多媒体系统",
    "md_5g": "5G 基带",
    "md_4g": "4G 基带",
    "md_3g": "3G 基带",
    "soc_dram_ntc": "SoC / DRAM NTC",
    "ltepa_ntc": "LTE 功放 NTC",
    "nrpa_ntc": "NR 功放 NTC",
    "rf_ntc": "射频 NTC",
    "md_rf": "射频基带",
    "conn_gps": "连接 / GPS",
    "pmic": "模组 PMIC",
    "pmic_vcore": "PMIC 核心电源",
    "pmic_vproc": "PMIC 处理器电源",
    "pmic_vgpu": "PMIC GPU 电源",
    "sensor_23": "模组传感器 23",
    "modem-ambient-usr": "模组环境温度",
}


def _read_text(path, default=""):
    try:
        return Path(path).read_text(encoding="utf-8").strip().strip("\x00")
    except (OSError, UnicodeError):
        return default


def _read_float(path):
    value = _read_text(path)
    try:
        return float(value)
    except (TypeError, ValueError):
        return None


def _temperature(raw):
    if raw is None:
        return None
    value = float(raw)
    if abs(value) > 1000:
        value /= 1000.0
    if value < -40 or value > 180:
        return None
    return round(value, 1)


def _scaled(raw, divisor):
    if raw is None:
        return None
    return round(float(raw) / divisor, 3)


def _normalise_name(value):
    return re.sub(r"[^a-z0-9]+", "_", str(value).lower()).strip("_")


def _sensor_state(value, warning=None, critical=None):
    if value is None:
        return "unavailable"
    if critical is not None and value >= critical:
        return "critical"
    if warning is not None and value >= warning:
        return "warning"
    if critical is None and value >= 90:
        return "critical"
    if warning is None and value >= 75:
        return "warning"
    return "normal"


class ThermalSampler:
    """Collect a single snapshot without changing any device setting."""

    def __init__(self, app_module=None, sys_root="/sys", proc_root="/proc"):
        self.app_module = app_module
        self.sys_root = Path(sys_root)
        self.proc_root = Path(proc_root)
        self._lock = threading.Lock()
        self._previous_cpu = self._cpu_counters()

    def _cpu_counters(self):
        counters = {}
        for line in _read_text(self.proc_root / "stat").splitlines():
            fields = line.split()
            if not fields or not re.fullmatch(r"cpu\d*", fields[0]):
                continue
            try:
                ticks = [int(item) for item in fields[1:]]
            except ValueError:
                continue
            if len(ticks) < 4:
                continue
            idle = ticks[3] + (ticks[4] if len(ticks) > 4 else 0)
            counters[fields[0]] = (sum(ticks), idle)
        return counters

    def _cpu_snapshot(self):
        current = self._cpu_counters()

        def usage(name):
            before = self._previous_cpu.get(name)
            now = current.get(name)
            if before is None or now is None:
                return None
            total_delta = now[0] - before[0]
            idle_delta = now[1] - before[1]
            if total_delta <= 0 or idle_delta < 0 or idle_delta > total_delta:
                return None
            return round((total_delta - idle_delta) * 100.0 / total_delta, 1)

        cores = []
        total_usage = usage("cpu")
        for name in sorted((key for key in current if key != "cpu"),
                           key=lambda item: int(item[3:])):
            cores.append({"id": int(name[3:]), "usage": usage(name)})
        self._previous_cpu = current

        policies = []
        cpufreq_root = self.sys_root / "devices/system/cpu/cpufreq"
        for path in sorted(cpufreq_root.glob("policy*"),
                           key=lambda item: int(item.name[6:])):
            related = []
            for item in _read_text(path / "related_cpus").split():
                try:
                    related.append(int(item))
                except ValueError:
                    pass
            policies.append({
                "name": path.name,
                "cores": related,
                "current_mhz": _scaled(_read_float(path / "scaling_cur_freq"), 1000),
                "minimum_mhz": _scaled(_read_float(path / "scaling_min_freq"), 1000),
                "maximum_mhz": _scaled(_read_float(path / "scaling_max_freq"), 1000),
                "governor": _read_text(path / "scaling_governor"),
            })
        try:
            load = [round(item, 2) for item in os.getloadavg()]
        except OSError:
            load = [None, None, None]
        return {
            "usage": total_usage,
            "cores": cores,
            "online_cores": len(cores),
            "load": load,
            "policies": policies,
        }

    @staticmethod
    def _trip_points(zone):
        warning = None
        critical = None
        for temp_path in sorted(zone.glob("trip_point_*_temp")):
            match = re.search(r"trip_point_(\d+)_temp$", temp_path.name)
            if not match:
                continue
            value = _temperature(_read_float(temp_path))
            kind = _read_text(zone / "trip_point_{}_type".format(match.group(1))).lower()
            if value is None:
                continue
            if kind == "critical":
                critical = value if critical is None else min(critical, value)
            elif kind in ("passive", "active", "hot"):
                warning = value if warning is None else min(warning, value)
        return warning, critical

    def _thermal_zones(self):
        sensors = []
        zone_names = set()
        root = self.sys_root / "class/thermal"
        for zone in sorted(root.glob("thermal_zone*"), key=lambda item: item.name):
            zone_type = _read_text(zone / "type") or zone.name
            zone_names.add(_normalise_name(zone_type))
            category, label = _THERMAL_LABELS.get(
                zone_type, ("other", zone_type.replace("-", " ")))
            value = _temperature(_read_float(zone / "temp"))
            warning, critical = self._trip_points(zone)
            sensors.append({
                "id": "thermal:" + zone.name,
                "category": category,
                "label": label,
                "source": zone_type,
                "temperature_c": value,
                "warning_c": warning,
                "critical_c": critical,
                "state": _sensor_state(value, warning, critical),
            })
        return sensors, zone_names

    @staticmethod
    def _hwmon_identity(name, label, index):
        normalised = _normalise_name(name)
        if normalised.startswith("mt7925"):
            return "network", "MT7927 Wi-Fi"
        if normalised == "pcat_pm_hwmon_temp_mb":
            return "board", "设备主板"
        if normalised.startswith("nvme"):
            return "storage", label or "NVMe"
        if normalised in ("battery", "charger"):
            return "power", label or ("电池" if normalised == "battery" else "充电器")
        shown = label or name or "温度传感器 {}".format(index)
        return "other", shown.replace("_", " ")

    def _hwmon_sensors(self, thermal_zone_names):
        sensors = []
        root = self.sys_root / "class/hwmon"
        for hwmon in sorted(root.glob("hwmon*"), key=lambda item: item.name):
            name = _read_text(hwmon / "name") or hwmon.name
            if _normalise_name(name) in thermal_zone_names:
                continue
            for input_path in sorted(hwmon.glob("temp*_input")):
                match = re.fullmatch(r"temp(\d+)_input", input_path.name)
                if not match:
                    continue
                index = int(match.group(1))
                label = _read_text(hwmon / "temp{}_label".format(index))
                category, shown = self._hwmon_identity(name, label, index)
                value = _temperature(_read_float(input_path))
                warning = _temperature(_read_float(hwmon / "temp{}_max".format(index)))
                critical = _temperature(_read_float(hwmon / "temp{}_crit".format(index)))
                sensors.append({
                    "id": "hwmon:{}:{}".format(name, index),
                    "category": category,
                    "label": shown,
                    "source": "{} / temp{}".format(name, index),
                    "temperature_c": value,
                    "warning_c": warning,
                    "critical_c": critical,
                    "state": _sensor_state(value, warning, critical),
                })
        return sensors

    def _modem_sensors(self):
        client = getattr(self.app_module, "modem_client", None) if self.app_module else None
        if client is None:
            return []
        try:
            # A shallow dict copy is atomic under CPython's GIL and never waits
            # for the modem serial mutex, which may be held during a long AT
            # transaction. This page only observes the vendor client's cache.
            basic = dict(getattr(client, "basic", {}) or {})
        except (AttributeError, RuntimeError, TypeError):
            return []
        values = basic.get("fm350_temperature_sensors")
        if not isinstance(values, dict) or not values:
            values = basic.get("qtemp_sensors")
        if not isinstance(values, dict):
            return []
        sensors = []
        for name, raw_value in values.items():
            try:
                value = _temperature(float(raw_value))
            except (TypeError, ValueError):
                continue
            sensors.append({
                "id": "modem:" + str(name),
                "category": "modem",
                "label": _MODEM_LABELS.get(str(name), str(name).replace("_", " ")),
                "source": "FM350-GL / " + str(name),
                "temperature_c": value,
                "warning_c": None,
                "critical_c": None,
                "state": _sensor_state(value),
            })
        return sensors

    def _board_temperature_fallback(self, sensors):
        if any(item["category"] == "board" for item in sensors):
            return []
        socket_client = (getattr(self.app_module, "socket_client", None)
                         if self.app_module else None)
        value = getattr(socket_client, "board_temperature", None)
        value = _temperature(value)
        if value is None:
            return []
        return [{
            "id": "manager:board",
            "category": "board",
            "label": "设备主板",
            "source": "电源管理单元",
            "temperature_c": value,
            "warning_c": None,
            "critical_c": None,
            "state": _sensor_state(value),
        }]

    def _power_snapshot(self):
        root = self.sys_root / "class/power_supply"
        battery = root / "battery"
        charger = root / "charger"
        voltage = _scaled(_read_float(battery / "voltage_now"), 1_000_000)
        current = _scaled(_read_float(battery / "current_now"), 1_000_000)
        power = _scaled(_read_float(battery / "power_now"), 1_000_000)
        if power is None and voltage is not None and current is not None:
            power = round(abs(voltage * current), 3)
        capacity = _read_float(battery / "capacity")
        return {
            "battery_voltage_v": voltage,
            "battery_current_a": current,
            "battery_power_w": power,
            "battery_capacity": round(capacity, 1) if capacity is not None else None,
            "battery_status": _read_text(battery / "status") or "Unknown",
            "charger_online": _read_text(charger / "online") == "1",
            "charger_voltage_v": _scaled(
                _read_float(charger / "voltage_now"), 1_000_000),
        }

    def _fan_snapshot(self):
        fans = []
        root = self.sys_root / "class/hwmon"
        for hwmon in sorted(root.glob("hwmon*"), key=lambda item: item.name):
            name = _read_text(hwmon / "name") or hwmon.name
            for input_path in sorted(hwmon.glob("fan*_input")):
                match = re.fullmatch(r"fan(\d+)_input", input_path.name)
                if not match:
                    continue
                value = _read_float(input_path)
                fans.append({
                    "name": _read_text(
                        hwmon / "fan{}_label".format(match.group(1))) or "散热风扇",
                    "source": name,
                    "rpm": int(value) if value is not None else None,
                })
        return fans

    def read(self):
        with self._lock:
            uptime_fields = (_read_text(self.proc_root / "uptime") or "").split()
            try:
                uptime_seconds = round(float(uptime_fields[0]), 2)
            except (IndexError, ValueError):
                uptime_seconds = None
            cpu = self._cpu_snapshot()
            sensors, zone_names = self._thermal_zones()
            sensors.extend(self._hwmon_sensors(zone_names))
            sensors.extend(self._modem_sensors())
            sensors.extend(self._board_temperature_fallback(sensors))
            sensors.sort(key=lambda item: (
                _CATEGORY_ORDER.get(item["category"], 99), item["label"], item["id"]))
            available = [item["temperature_c"] for item in sensors
                         if item["temperature_c"] is not None]
            return {
                "status": "ok",
                "generated_at": int(time.time()),
                "uptime_seconds": uptime_seconds,
                "cpu": cpu,
                "power": self._power_snapshot(),
                "fans": self._fan_snapshot(),
                "temperatures": sensors,
                "temperature_summary": {
                    "count": len(available),
                    "highest_c": max(available) if available else None,
                    "average_c": (round(sum(available) / len(available), 1)
                                  if available else None),
                },
            }


def register_thermal(flask_app, api_auth_required, base_locales, app_module):
    sampler = ThermalSampler(app_module)

    @flask_app.route("/thermal")
    def pcat_thermal_page():
        if "username" not in session:
            return redirect("/login?next=/thermal")
        return render_template("thermal.html", locales=base_locales())

    @flask_app.route("/api/v1/thermal.json", methods=["GET"])
    @api_auth_required
    def pcat_thermal_api():
        return jsonify(sampler.read())
