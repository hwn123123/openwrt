# -*- coding: utf-8 -*-
"""Vendor PMU buzzer event service.

The service only reads OpenWrt/PMU state.  It does not open a modem serial
port, issue AT commands, alter APN settings, or take part in dialing.
"""

import copy
import json
import os
import pathlib
import socket
import subprocess
import threading
import time

import requests

from pcat_util import pcat_logger


CONFIG_FILE = "/etc/pcat-sound-events.json"
SOCKET_FILE = "/tmp/pcat-manager.sock"
BOOT_MARKER = "/tmp/pcat-sound-events-boot-id"
SIDECAR_SMS_URL = "http://127.0.0.1:8092/v1/sms?limit=30"

PRESETS = {
    "short": {"label": "短音", "hint": "单次确认", "tones": [{"hz": 2700, "ms": 220}]},
    "double": {"label": "双音", "hint": "按键反馈", "tones": [{"hz": 2600, "ms": 150}, {"hz": 0, "ms": 90}, {"hz": 3200, "ms": 190}]},
    "notify": {"label": "通知音", "hint": "新事件提醒", "tones": [{"hz": 1800, "ms": 110}, {"hz": 2400, "ms": 110}, {"hz": 3200, "ms": 250}]},
    "success": {"label": "完成音", "hint": "操作完成", "tones": [{"hz": 1568, "ms": 120}, {"hz": 2093, "ms": 120}, {"hz": 2637, "ms": 260}]},
    "chime": {"label": "上升音", "hint": "启动提示", "tones": [{"hz": 1047, "ms": 160}, {"hz": 1319, "ms": 160}, {"hz": 1568, "ms": 180}, {"hz": 2093, "ms": 320}]},
    "down": {"label": "下降音", "hint": "结束提示", "tones": [{"hz": 2093, "ms": 150}, {"hz": 1568, "ms": 150}, {"hz": 1319, "ms": 180}, {"hz": 1047, "ms": 300}]},
    "warning": {"label": "警告音", "hint": "需要注意", "tones": [{"hz": 3000, "ms": 180}, {"hz": 0, "ms": 90}, {"hz": 3000, "ms": 180}]},
    "alarm": {"label": "警报音", "hint": "高优先级", "tones": [{"hz": 3200, "ms": 220}, {"hz": 1800, "ms": 180}, {"hz": 3200, "ms": 220}, {"hz": 1800, "ms": 180}]},
}

EVENTS = {
    "boot": {"category": "系统", "label": "系统启动成功", "hint": "本次系统启动完成后提示一次", "icon": "⏻", "default": "success"},
    "shutdown": {"category": "系统", "label": "设备关机", "hint": "从管理页面执行正常关机时提示", "icon": "◉", "default": "down"},

    "sms_received": {"category": "SIM 与短信", "label": "收到新短信", "hint": "本地短信库出现新的接收短信", "icon": "SMS", "default": "notify"},
    "sim_ready": {"category": "SIM 与短信", "label": "SIM 卡就绪", "hint": "SIM 状态切换为可用", "icon": "SIM", "default": "success"},
    "sim_lost": {"category": "SIM 与短信", "label": "SIM 卡异常或移除", "hint": "SIM 从就绪切换为不可用", "icon": "SIM!", "default": "warning"},

    "charging": {"category": "供电与电池", "label": "接入外部电源", "hint": "检测到充电电源开始供电", "icon": "ϟ", "default": "notify"},
    "power_removed": {"category": "供电与电池", "label": "外部电源断开", "hint": "设备由充电切换为电池供电", "icon": "ϟ−", "default": "down"},
    "charge_full": {"category": "供电与电池", "label": "电池充满", "hint": "充电时电量首次达到 99%", "icon": "100", "default": "success"},
    "low_battery": {"category": "供电与电池", "label": "电池电量低", "hint": "放电时首次达到低电量阈值", "icon": "▱", "default": "warning"},
    "critical_battery": {"category": "供电与电池", "label": "电池电量严重不足", "hint": "放电时首次达到严重低电量阈值", "icon": "!!", "default": "alarm"},

    "board_hot": {"category": "温度安全", "label": "主板温度过高", "hint": "主板温度达到设置的告警值", "icon": "MB", "default": "alarm"},
    "board_normal": {"category": "温度安全", "label": "主板温度恢复", "hint": "主板温度回落到告警值以下 5°C", "icon": "MB✓", "default": "success"},
    "modem_hot": {"category": "温度安全", "label": "模组温度过高", "hint": "当前模组任一温度传感器达到告警值", "icon": "5G°", "default": "alarm"},
    "modem_normal": {"category": "温度安全", "label": "模组温度恢复", "hint": "当前模组最高温度回落到告警值以下 5°C", "icon": "5G✓", "default": "success"},

    "cellular": {"category": "蜂窝与出口", "label": "5G 连接成功", "hint": "蜂窝逻辑接口从离线变为上线", "icon": "5G", "default": "chime"},
    "cellular_lost": {"category": "蜂窝与出口", "label": "5G 连接断开", "hint": "蜂窝逻辑接口从在线变为离线", "icon": "5G×", "default": "warning"},
    "wan": {"category": "蜂窝与出口", "label": "WAN 连接成功", "hint": "WAN 逻辑接口从离线变为上线", "icon": "◎", "default": "double"},
    "wan_lost": {"category": "蜂窝与出口", "label": "WAN 连接断开", "hint": "WAN 逻辑接口从在线变为离线", "icon": "◎×", "default": "warning"},
    "uplink_cellular": {"category": "蜂窝与出口", "label": "出口切换到 5G", "hint": "系统默认路由切换为蜂窝网络", "icon": "→5G", "default": "chime"},
    "uplink_wan": {"category": "蜂窝与出口", "label": "出口切换到 WAN", "hint": "系统默认路由切换为有线 WAN", "icon": "→WAN", "default": "double"},

    "lan": {"category": "局域网与 Wi-Fi", "label": "LAN 连接成功", "hint": "LAN 物理链路由断开变为连接", "icon": "LAN", "default": "short"},
    "lan_lost": {"category": "局域网与 Wi-Fi", "label": "LAN 连接断开", "hint": "LAN 物理链路由连接变为断开", "icon": "LAN×", "default": "double"},
    "wifi_start": {"category": "局域网与 Wi-Fi", "label": "Wi-Fi 启动成功", "hint": "无线接入点开始工作", "icon": "WiFi", "default": "success"},
    "wifi_stop": {"category": "局域网与 Wi-Fi", "label": "Wi-Fi 已停止", "hint": "全部无线接入点停止工作", "icon": "WiFi×", "default": "down"},
    "wifi_client": {"category": "局域网与 Wi-Fi", "label": "Wi-Fi 新设备接入", "hint": "无线客户端数量增加", "icon": "+", "default": "short"},
    "wifi_client_left": {"category": "局域网与 Wi-Fi", "label": "Wi-Fi 设备离开", "hint": "无线客户端数量减少", "icon": "−", "default": "off"},

    "sd_inserted": {"category": "外接存储", "label": "SD 卡接入", "hint": "检测到外置 SD 卡块设备", "icon": "SD+", "default": "success"},
    "sd_removed": {"category": "外接存储", "label": "SD 卡移除", "hint": "外置 SD 卡块设备消失", "icon": "SD−", "default": "warning"},
    "nvme_inserted": {"category": "外接存储", "label": "NVMe 接入", "hint": "检测到 NVMe 固态硬盘", "icon": "NV+", "default": "success"},
    "nvme_removed": {"category": "外接存储", "label": "NVMe 移除", "hint": "NVMe 固态硬盘设备消失", "icon": "NV−", "default": "warning"},
}

DEFAULT_CUSTOM = [{"hz": 1047, "ms": 160}, {"hz": 1319, "ms": 160}, {"hz": 1568, "ms": 180}, {"hz": 2093, "ms": 320}]
def default_event_config(name):
    sound = EVENTS[name]["default"]
    tones = PRESETS.get(sound, {}).get("tones", DEFAULT_CUSTOM)
    return {"sound": sound, "tones": copy.deepcopy(tones)}


DEFAULT_CONFIG = {
    "version": 3,
    "low_battery_threshold": 15,
    "critical_battery_threshold": 5,
    "board_temperature_threshold": 65,
    "modem_temperature_threshold": 75,
    "events": {name: default_event_config(name) for name in EVENTS},
}


def _clamp(value, minimum, maximum, fallback):
    try:
        value = int(value)
    except (TypeError, ValueError):
        value = fallback
    return max(minimum, min(maximum, value))


def clean_tones(value):
    if not isinstance(value, list):
        return []
    tones = []
    for item in value[:20]:
        if not isinstance(item, dict):
            continue
        tones.append({
            "hz": _clamp(item.get("hz"), 0, 12000, 2700),
            "ms": _clamp(item.get("ms"), 1, 65535, 200),
        })
    return tones


def _preset_for_tones(tones):
    for name, preset in PRESETS.items():
        if tones == preset["tones"]:
            return name
    return None


def normalize_config(value):
    config = copy.deepcopy(DEFAULT_CONFIG)
    if not isinstance(value, dict):
        return config

    config["low_battery_threshold"] = _clamp(value.get("low_battery_threshold"), 5, 50, 15)
    config["critical_battery_threshold"] = _clamp(value.get("critical_battery_threshold"), 2, 30, 5)
    if config["critical_battery_threshold"] >= config["low_battery_threshold"]:
        config["critical_battery_threshold"] = max(2, config["low_battery_threshold"] - 5)
    config["board_temperature_threshold"] = _clamp(value.get("board_temperature_threshold"), 45, 95, 65)
    config["modem_temperature_threshold"] = _clamp(value.get("modem_temperature_threshold"), 45, 105, 75)
    legacy_custom = clean_tones(value.get("custom"))

    supplied = value.get("events")
    if not isinstance(supplied, dict):
        return config

    valid = set(PRESETS) | {"off", "custom"}
    for name, definition in EVENTS.items():
        item = supplied.get(name)
        sound = None
        tones = []
        if isinstance(item, str):
            sound = item
        elif isinstance(item, dict):
            if item.get("enabled") is False:
                sound = "off"
            else:
                sound = item.get("sound")
                tones = clean_tones(item.get("tones"))
                if not sound and tones:
                    sound = _preset_for_tones(tones)
                    if not sound:
                        sound = "custom"
        if sound in valid:
            if not tones:
                if sound == "custom":
                    tones = copy.deepcopy(legacy_custom or DEFAULT_CUSTOM)
                else:
                    tones = copy.deepcopy(PRESETS.get(sound, {}).get("tones", DEFAULT_CUSTOM))
            config["events"][name] = {"sound": sound, "tones": tones}
        else:
            config["events"][name] = default_event_config(name)
    return config


class SoundEventManager(threading.Thread):
    """Monitor state transitions and play short PMU buzzer sequences."""

    def __init__(self, socket_client=None):
        super().__init__(daemon=True, name="pcat-sound-events")
        self.socket_client = socket_client
        self.modem_client = None
        self.running = True
        self.lock = threading.RLock()
        self.config = self._load()
        self.history = []
        self.runtime = {
            "monitor_alive": False,
            "cellular": None,
            "lan": None,
            "wan": None,
            "wifi": None,
            "wifi_clients": None,
            "charging": None,
            "battery_percent": None,
            "sim_state": None,
            "latest_sms_id": None,
            "board_temperature": None,
            "modem_temperature": None,
            "default_uplink": None,
            "sd_present": None,
            "nvme_present": None,
            "charge_full": None,
            "battery_low": None,
            "battery_critical": None,
            "board_hot": None,
            "modem_hot": None,
            "last_event": None,
            "last_error": None,
        }
        self._state_ready = False
        self._last_played = {}
        self._boot_due = time.monotonic() + 25

    def set_modem_client(self, modem_client):
        """Attach the vendor modem cache without opening or querying its AT port."""
        self.modem_client = modem_client

    def _load(self):
        try:
            with open(CONFIG_FILE, "r", encoding="utf-8") as stream:
                return normalize_config(json.load(stream))
        except (OSError, ValueError, TypeError):
            return copy.deepcopy(DEFAULT_CONFIG)

    def snapshot(self):
        with self.lock:
            result = {
                "status": "ok",
                "config": copy.deepcopy(self.config),
                "defaults": copy.deepcopy(DEFAULT_CONFIG),
                "presets": copy.deepcopy(PRESETS),
                "event_definitions": copy.deepcopy(EVENTS),
                "runtime": copy.deepcopy(self.runtime),
                "history": copy.deepcopy(self.history[-24:]),
            }
        result["runtime"]["beeper_enabled"] = getattr(self.socket_client, "beeper_enabled", None)
        return result

    def save(self, value):
        config = normalize_config(value)
        temporary = CONFIG_FILE + ".tmp"
        with self.lock:
            with open(temporary, "w", encoding="utf-8") as stream:
                json.dump(config, stream, ensure_ascii=False, indent=2)
                stream.write("\n")
                stream.flush()
                os.fsync(stream.fileno())
            os.replace(temporary, CONFIG_FILE)
            self.config = config
        return self.snapshot()

    @staticmethod
    def _run(args, timeout=2):
        try:
            result = subprocess.run(args, check=False, stdout=subprocess.PIPE,
                                    stderr=subprocess.DEVNULL, text=True,
                                    timeout=timeout)
            return result.stdout.strip() if result.returncode == 0 else ""
        except (OSError, subprocess.SubprocessError):
            return ""

    @classmethod
    def _ubus_status(cls, name):
        raw = cls._run(["ubus", "call", "network.interface." + name, "status"])
        try:
            return json.loads(raw) if raw else {}
        except (TypeError, ValueError):
            return {}

    @classmethod
    def cellular_connected(cls):
        objects = cls._run(["ubus", "list", "network.interface.*"]).splitlines()
        for obj in objects:
            name = obj.rsplit(".", 1)[-1]
            if not name.lower().startswith(("wwan", "cell", "modem")):
                continue
            status = cls._ubus_status(name)
            if status.get("up") is True:
                return True
        return False

    @classmethod
    def wan_connected(cls):
        return cls._ubus_status("wan").get("up") is True

    @classmethod
    def lan_connected(cls):
        bridge = cls._run(["uci", "-q", "get", "network.lan.device"]) or "br-lan"
        bridge_dir = pathlib.Path("/sys/class/net") / bridge
        ports = bridge_dir / "brif"
        found = False
        try:
            if ports.is_dir():
                for port in ports.iterdir():
                    name = port.name.lower()
                    if name.startswith(("wlan", "phy", "radio")):
                        continue
                    carrier = pathlib.Path("/sys/class/net") / port.name / "carrier"
                    if carrier.is_file():
                        found = True
                        if carrier.read_text().strip() == "1":
                            return True
            carrier = bridge_dir / "carrier"
            return not found and carrier.is_file() and carrier.read_text().strip() == "1"
        except OSError:
            return False

    @classmethod
    def wifi_state(cls):
        objects = [item for item in cls._run(["ubus", "list", "hostapd.*"]).splitlines() if item]
        clients = 0
        for obj in objects:
            raw = cls._run(["ubus", "call", obj, "get_clients"])
            try:
                data = json.loads(raw) if raw else {}
                clients += len(data.get("clients", {}))
            except (TypeError, ValueError):
                continue
        return bool(objects), clients

    @classmethod
    def default_uplink(cls):
        route = cls._run(["ip", "-4", "route", "show", "default"])
        if not route:
            return None
        first = route.splitlines()[0].lower().split()
        try:
            device = first[first.index("dev") + 1]
        except (ValueError, IndexError):
            return "other"
        if device == "eth0":
            return "wan"
        if device == "eth2" or device.startswith(("wwan", "usb", "rmnet")):
            return "cellular"
        return "other"

    @staticmethod
    def latest_received_sms_id():
        """Read only message identifiers from the loopback sidecar."""
        try:
            response = requests.get(SIDECAR_SMS_URL, timeout=2)
            response.raise_for_status()
            messages = response.json().get("msg", [])
        except (requests.RequestException, ValueError, TypeError):
            return None
        identifiers = []
        for message in messages if isinstance(messages, list) else []:
            if not isinstance(message, dict):
                continue
            direction = str(message.get("direction", "0")).lower()
            if direction in ("1", "sent", "out", "outgoing"):
                continue
            try:
                identifiers.append(int(message.get("id")))
            except (TypeError, ValueError):
                continue
        return max(identifiers) if identifiers else None

    def modem_temperature(self):
        client = self.modem_client
        if client is None:
            return None
        try:
            basic = getattr(client, "basic", {})
            sensors = basic.get("fm350_temperature_sensors") or basic.get("qtemp_sensors") or {}
            values = []
            for value in list(sensors.values()):
                try:
                    value = float(value)
                except (TypeError, ValueError):
                    continue
                if -40 < value < 150:
                    values.append(value)
            if values:
                return round(max(values), 1)
            value = basic.get("modem_temperature_decimal")
            return round(float(value), 1) if value not in (None, "") else None
        except (AttributeError, TypeError, ValueError, RuntimeError):
            return None

    @staticmethod
    def _temperature_latch(value, previous, threshold):
        if value is None:
            return previous
        if previous is True:
            return value > threshold - 5
        return value >= threshold

    def _send(self, tones):
        payload = (json.dumps({"command": "pmu-beep-play", "tones": tones, "loop": 0},
                              separators=(",", ":")) + "\0").encode("utf-8")
        try:
            with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as client:
                client.settimeout(2)
                client.connect(SOCKET_FILE)
                client.sendall(payload)
                response = b""
                while b"\0" not in response and len(response) < 8192:
                    chunk = client.recv(2048)
                    if not chunk:
                        break
                    response += chunk
            data = json.loads(response.split(b"\0", 1)[0].decode("utf-8"))
            return data.get("code") == 0
        except (OSError, ValueError, UnicodeError) as exc:
            with self.lock:
                self.runtime["last_error"] = str(exc)
            pcat_logger.logger.warning("Sound event PMU request failed: %s", exc)
            return False

    @staticmethod
    def _tones_for(sound, custom=None):
        if sound == "custom":
            return clean_tones(custom)
        return copy.deepcopy(PRESETS.get(sound, {}).get("tones", []))

    def preview(self, sound, tones=None):
        enabled = getattr(self.socket_client, "beeper_enabled", None)
        if enabled is not None and not bool(enabled):
            return False, "蜂鸣器当前处于关闭或静音时段"
        selected = self._tones_for(sound, tones)
        if not selected:
            return False, "音效配置无效"
        return (True, "") if self._send(selected) else (False, "PMU 蜂鸣器未响应")

    def play_event(self, name, force=False):
        if name not in EVENTS:
            return False
        with self.lock:
            setting = copy.deepcopy(self.config["events"].get(name, default_event_config(name)))
        sound = setting.get("sound", EVENTS[name]["default"])
        if sound == "off":
            return False
        enabled = getattr(self.socket_client, "beeper_enabled", None)
        if enabled is not None and not bool(enabled):
            return False
        now = time.monotonic()
        if not force and now - self._last_played.get(name, 0) < 20:
            return False
        ok = self._send(self._tones_for(sound, setting.get("tones")))
        if ok:
            entry = {"time": int(time.time()), "event": name, "sound": sound}
            with self.lock:
                self._last_played[name] = now
                self.runtime["last_event"] = entry
                self.history.append(entry)
                self.history = self.history[-60:]
        return ok

    def _boot_once(self):
        if time.monotonic() < self._boot_due or not pathlib.Path(SOCKET_FILE).exists():
            return
        try:
            boot_id = pathlib.Path("/proc/sys/kernel/random/boot_id").read_text().strip()
            previous = pathlib.Path(BOOT_MARKER).read_text().strip() if pathlib.Path(BOOT_MARKER).exists() else ""
            if boot_id and boot_id != previous:
                self.play_event("boot")
                pathlib.Path(BOOT_MARKER).write_text(boot_id)
        except OSError as exc:
            pcat_logger.logger.debug("Unable to store sound-event boot marker: %s", exc)
        self._boot_due = float("inf")

    def _poll(self):
        cellular = self.cellular_connected()
        lan = self.lan_connected()
        wan = self.wan_connected()
        wifi, clients = self.wifi_state()
        uplink = self.default_uplink()
        sim_state = str(getattr(self.socket_client, "sim_state", "unknown") or "unknown").lower()
        latest_sms = self.latest_received_sms_id()
        charging = bool(getattr(self.socket_client, "on_charging", False))
        battery = getattr(self.socket_client, "charge_percent", None)
        board_temperature = getattr(self.socket_client, "board_temperature", None)
        modem_temperature = self.modem_temperature()
        try:
            battery = int(battery)
        except (TypeError, ValueError):
            battery = None
        try:
            board_temperature = round(float(board_temperature), 1)
        except (TypeError, ValueError):
            board_temperature = None

        with self.lock:
            low_threshold = self.config["low_battery_threshold"]
            critical_threshold = self.config["critical_battery_threshold"]
            board_threshold = self.config["board_temperature_threshold"]
            modem_threshold = self.config["modem_temperature_threshold"]
            previous_board_hot = self.runtime.get("board_hot")
            previous_modem_hot = self.runtime.get("modem_hot")

        charge_full = charging and battery is not None and battery >= 99
        battery_low = not charging and battery is not None and battery <= low_threshold
        battery_critical = not charging and battery is not None and battery <= critical_threshold
        board_hot = self._temperature_latch(board_temperature, previous_board_hot, board_threshold)
        modem_hot = self._temperature_latch(modem_temperature, previous_modem_hot, modem_threshold)

        current = {
            "cellular": cellular,
            "lan": lan,
            "wan": wan,
            "wifi": wifi,
            "wifi_clients": clients,
            "charging": charging,
            "battery_percent": battery,
            "sim_state": sim_state,
            "latest_sms_id": latest_sms,
            "board_temperature": board_temperature,
            "modem_temperature": modem_temperature,
            "default_uplink": uplink,
            "sd_present": pathlib.Path("/sys/class/block/mmcblk1").exists(),
            "nvme_present": pathlib.Path("/sys/class/block/nvme0n1").exists(),
            "charge_full": charge_full,
            "battery_low": battery_low,
            "battery_critical": battery_critical,
            "board_hot": board_hot,
            "modem_hot": modem_hot,
        }
        with self.lock:
            previous = {key: self.runtime.get(key) for key in current}
            self.runtime.update(current)

        if self._state_ready:
            if previous["cellular"] is False and cellular:
                self.play_event("cellular")
            elif previous["cellular"] is True and not cellular:
                self.play_event("cellular_lost")

            if previous["wan"] is False and wan:
                self.play_event("wan")
            elif previous["wan"] is True and not wan:
                self.play_event("wan_lost")

            if previous["lan"] is False and lan:
                self.play_event("lan")
            elif previous["lan"] is True and not lan:
                self.play_event("lan_lost")

            if previous["wifi"] is False and wifi:
                self.play_event("wifi_start")
            elif previous["wifi"] is True and not wifi:
                self.play_event("wifi_stop")

            if previous["wifi_clients"] is not None and clients > previous["wifi_clients"]:
                self.play_event("wifi_client")
            elif previous["wifi_clients"] is not None and clients < previous["wifi_clients"]:
                self.play_event("wifi_client_left")

            if previous["charge_full"] is False and charge_full:
                self.play_event("charge_full")
            elif previous["charging"] is False and charging:
                self.play_event("charging")
            elif previous["charging"] is True and not charging:
                self.play_event("power_removed")

            if previous["battery_critical"] is False and battery_critical:
                self.play_event("critical_battery")
            elif previous["battery_low"] is False and battery_low:
                self.play_event("low_battery")

            if board_hot is True and previous["board_hot"] is not True:
                self.play_event("board_hot")
            elif previous["board_hot"] is True and board_hot is False:
                self.play_event("board_normal")
            if modem_hot is True and previous["modem_hot"] is not True:
                self.play_event("modem_hot")
            elif previous["modem_hot"] is True and modem_hot is False:
                self.play_event("modem_normal")

            if previous["sim_state"] not in (None, "ready") and sim_state == "ready":
                self.play_event("sim_ready")
            elif previous["sim_state"] == "ready" and sim_state != "ready":
                self.play_event("sim_lost")
            if latest_sms is not None and previous["latest_sms_id"] is not None and latest_sms > previous["latest_sms_id"]:
                self.play_event("sms_received")

            if previous["default_uplink"] and uplink and previous["default_uplink"] != uplink:
                if uplink == "cellular":
                    self.play_event("uplink_cellular")
                elif uplink == "wan":
                    self.play_event("uplink_wan")

            if previous["sd_present"] is False and current["sd_present"]:
                self.play_event("sd_inserted")
            elif previous["sd_present"] is True and not current["sd_present"]:
                self.play_event("sd_removed")
            if previous["nvme_present"] is False and current["nvme_present"]:
                self.play_event("nvme_inserted")
            elif previous["nvme_present"] is True and not current["nvme_present"]:
                self.play_event("nvme_removed")
        else:
            self._state_ready = True

    def stop(self):
        self.running = False

    def run(self):
        pcat_logger.logger.info("PMU sound-event monitor started (read-only system monitoring)")
        with self.lock:
            self.runtime["monitor_alive"] = True
        while self.running:
            try:
                self._boot_once()
                self._poll()
                with self.lock:
                    self.runtime["last_error"] = None
            except Exception as exc:
                with self.lock:
                    self.runtime["last_error"] = str(exc)
                pcat_logger.logger.exception("Sound-event monitor poll failed")
            time.sleep(5)
        with self.lock:
            self.runtime["monitor_alive"] = False
