"""Photonicat Wi-Fi discovery and parameter management.

The board has two radios whose UCI radio numbers are not stable across firmware
variants.  This module discovers them from their PHY bus paths and deliberately
keeps power switching out of the Wi-Fi parameter API; power remains owned by
the device-control page.
"""

import json
import os
import re
import subprocess
import threading


_RADIO_RE = re.compile(r"^radio[0-9]+$")
_COUNTRY_RE = re.compile(r"^[A-Z]{2}$")


def _run(argv, timeout=8):
    return subprocess.run(
        argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        text=True, timeout=timeout, check=False
    )


def _uci_show(package):
    result = _run(["uci", "-q", "show", package])
    values = {}
    if result.returncode != 0:
        return values
    for line in result.stdout.splitlines():
        if "=" not in line:
            continue
        key, value = line.split("=", 1)
        if len(value) >= 2 and value[0] == "'" and value[-1] == "'":
            value = value[1:-1].replace("'\\''", "'")
        values[key] = value
    return values


def _uci_set(key, value):
    result = _run(["uci", "set", "{}={}".format(key, value)])
    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip() or "uci set failed")


def _uci_delete(key):
    _run(["uci", "-q", "delete", key])


def _json_command(argv, timeout=8):
    try:
        result = _run(argv, timeout=timeout)
        if result.returncode == 0 and result.stdout.strip():
            return json.loads(result.stdout)
    except (OSError, subprocess.SubprocessError, ValueError):
        pass
    return {}


def _board_wlan():
    try:
        with open("/etc/board.json", "r", encoding="utf-8") as handle:
            return (json.load(handle) or {}).get("wlan", {}) or {}
    except (OSError, ValueError):
        return {}


def _phy_interfaces(phy_name):
    names = []
    net_root = "/sys/class/net"
    try:
        for name in os.listdir(net_root):
            phy_link = os.path.join(net_root, name, "phy80211")
            if os.path.basename(os.path.realpath(phy_link)) == phy_name:
                names.append(name)
    except OSError:
        pass
    return sorted(names)


def _radio_phy(radio, wireless, board):
    configured = wireless.get("wireless.{}.phy".format(radio), "")
    if configured and os.path.exists("/sys/class/ieee80211/{}".format(configured)):
        return configured

    path = wireless.get("wireless.{}.path".format(radio), "")
    for phy_name, phy_data in board.items():
        board_path = phy_data.get("path", "") if isinstance(phy_data, dict) else ""
        if path and board_path and (path == board_path or path.endswith(board_path) or board_path.endswith(path)):
            return phy_name

    # Last-resort matching by bus path.  It covers firmware variants that omit
    # the UCI phy key while keeping a normalized path in board.json.
    for phy_name in board:
        sys_path = os.path.realpath("/sys/class/ieee80211/{}/device".format(phy_name))
        if path and path.replace("platform/", "") in sys_path:
            return phy_name
    return ""


def _hardware_identity(phy_name):
    device = "/sys/class/ieee80211/{}/device".format(phy_name)
    real_path = os.path.realpath(device).lower()
    bus = "pcie" if ("/pci" in real_path or "pcie" in real_path) else "usb" if "/usb" in real_path else "unknown"
    driver = os.path.basename(os.path.realpath(os.path.join(device, "driver")))
    uevent = {}
    try:
        with open(os.path.join(device, "uevent"), "r", encoding="utf-8") as handle:
            for line in handle:
                if "=" in line:
                    key, value = line.rstrip().split("=", 1)
                    uevent[key] = value
    except OSError:
        pass

    pci_id = uevent.get("PCI_ID", "").upper()
    if pci_id.endswith(":7927") or driver == "mt7925e":
        model = "MT7927"
    elif bus == "usb" and driver.startswith("aic"):
        model = "AIC8800"
    else:
        model = pci_id or ("板载无线" if bus == "usb" else "无线模块")

    if model == "AIC8800" and driver == "usb" and os.path.isdir("/sys/module/aic8800_fdrv"):
        driver = "aic8800_fdrv"

    return {
        "bus": bus,
        "kind": "pcie" if bus == "pcie" else "onboard" if bus == "usb" else "unknown",
        "driver": driver or "未知",
        "model": model,
        "pci_id": pci_id,
        "device_path": real_path,
    }


def _bit_count(value):
    try:
        value = int(value)
        return bin(value).count("1") or 1
    except (TypeError, ValueError):
        return 1


def _capabilities(phy_name, board):
    phy_data = board.get(phy_name, {}) if isinstance(board, dict) else {}
    info = phy_data.get("info", {}) if isinstance(phy_data, dict) else {}
    bands = {}
    for raw_name, raw in (info.get("bands", {}) or {}).items():
        if not isinstance(raw, dict):
            continue
        band = raw_name.lower()
        modes = []
        for mode in raw.get("modes", []) or []:
            mode = str(mode).upper()
            if mode not in modes:
                modes.append(mode)
        mode_widths = [int(match.group(1)) for mode in modes
                       for match in [re.search(r"([0-9]+)$", mode)] if match]
        bands[band] = {
            "ht": bool(raw.get("ht")),
            "vht": bool(raw.get("vht")),
            "he": bool(raw.get("he")),
            "eht": bool(raw.get("eht")),
            "max_width": max(mode_widths) if mode_widths else int(raw.get("max_width", 0) or 0),
            "default_channel": int(raw.get("default_channel", 0) or 0),
            "modes": modes,
        }
    return {
        "bands": bands,
        "rx_streams": _bit_count(info.get("antenna_rx", 0)),
        "tx_streams": _bit_count(info.get("antenna_tx", 0)),
        "can_ap": True,
        "can_sta": True,
    }


def _interface_section(radio, wireless):
    candidates = []
    for key, value in wireless.items():
        if key.startswith("wireless.") and key.count(".") == 1 and value == "wifi-iface":
            section = key.split(".", 1)[1]
            if wireless.get("wireless.{}.device".format(section)) == radio:
                candidates.append(section)
    preferred = "default_{}".format(radio)
    if preferred in candidates:
        return preferred
    return candidates[0] if candidates else preferred


def _iwinfo_device(phy_name):
    interfaces = _phy_interfaces(phy_name)
    return interfaces[0] if interfaces else phy_name


def _iwinfo(method, device, timeout=8):
    return _json_command(
        ["ubus", "call", "iwinfo", method, json.dumps({"device": device}, separators=(",", ":"))],
        timeout=timeout,
    )


def _channels(phy_name, capabilities):
    result = _iwinfo("freqlist", _iwinfo_device(phy_name))
    channels = {"2g": [], "5g": [], "6g": []}
    for item in result.get("results", []) or []:
        try:
            mhz = int(item.get("mhz", 0))
            channel = int(item.get("channel", 0))
        except (TypeError, ValueError):
            continue
        if 2400 <= mhz < 2500:
            band = "2g"
        elif 4900 <= mhz < 5925:
            band = "5g"
        elif 5925 <= mhz < 7200:
            band = "6g"
        else:
            continue
        if band not in capabilities.get("bands", {}):
            continue
        flags = [str(flag) for flag in (item.get("flags", []) or [])]
        channels[band].append({
            "channel": channel,
            "mhz": mhz,
            "active": bool(item.get("active")),
            "restricted": bool(item.get("restricted")),
            "no_ir": "no_ir" in flags,
            "radar": "radar" in flags,
            "flags": flags,
        })
    for band in channels:
        seen = set()
        channels[band] = [item for item in sorted(channels[band], key=lambda row: row["channel"])
                          if not (item["channel"] in seen or seen.add(item["channel"]))]
    return channels


def _countries(device):
    result = _iwinfo("countrylist", device)
    countries = []
    for item in result.get("results", []) or []:
        code = str(item.get("code", "")).upper()
        if _COUNTRY_RE.match(code) or code == "00":
            countries.append({
                "code": code,
                "name": str(item.get("country", code)),
                "active": bool(item.get("active")),
            })
    return countries


def _txpowers(device):
    result = _iwinfo("txpowerlist", device)
    values = []
    for item in result.get("results", []) or []:
        try:
            dbm = int(item.get("dbm"))
        except (TypeError, ValueError):
            continue
        values.append({"dbm": dbm, "mw": int(item.get("mw", 0) or 0), "active": bool(item.get("active"))})
    return values


def _runtime_status(radio):
    data = _json_command(["wifi", "status", radio])
    state = data.get(radio, {}) if isinstance(data, dict) else {}
    return {
        "up": bool(state.get("up", False)),
        "pending": bool(state.get("pending", False)),
        "autostart": bool(state.get("autostart", True)),
        "disabled": bool(state.get("disabled", False)),
    }


def get_wireless_state():
    wireless = _uci_show("wireless")
    board = _board_wlan()
    radios = []
    country_source = ""

    radio_names = sorted({key.split(".")[1] for key, value in wireless.items()
                          if key.startswith("wireless.") and key.count(".") == 1
                          and value == "wifi-device" and _RADIO_RE.match(key.split(".")[1])})
    for radio in radio_names:
        phy_name = _radio_phy(radio, wireless, board)
        if not phy_name or not os.path.exists("/sys/class/ieee80211/{}".format(phy_name)):
            continue
        interface = _interface_section(radio, wireless)
        prefix = "wireless.{}.".format(interface)
        radio_prefix = "wireless.{}.".format(radio)
        identity = _hardware_identity(phy_name)
        caps = _capabilities(phy_name, board)
        iw_device = _iwinfo_device(phy_name)
        if not country_source or iw_device != phy_name:
            country_source = iw_device
        runtime = _runtime_status(radio)
        disabled = wireless.get(radio_prefix + "disabled", "0") == "1"
        iface_disabled = wireless.get(prefix + "disabled", "0") == "1"
        mode = wireless.get(prefix + "mode", "ap")
        current = {
            "mode": mode if mode in ("ap", "sta") else "ap",
            "ssid": wireless.get(prefix + "ssid", ""),
            "encryption": wireless.get(prefix + "encryption", "none").split("+")[0],
            "password": wireless.get(prefix + "key", ""),
            "hidden": wireless.get(prefix + "hidden", "0") == "1",
            "bssid": wireless.get(prefix + "bssid", ""),
            "network": wireless.get(prefix + "network", "lan" if mode == "ap" else "wifiwan"),
            "band": wireless.get(radio_prefix + "band", "2g").lower(),
            "channel": wireless.get(radio_prefix + "channel", "auto"),
            "htmode": wireless.get(radio_prefix + "htmode", "HT20").upper(),
            "country": wireless.get(radio_prefix + "country", "00").upper(),
            "txpower": wireless.get(radio_prefix + "txpower", "auto"),
        }
        standards = []
        for band in caps.get("bands", {}).values():
            for flag, label in (("ht", "Wi-Fi 4"), ("vht", "Wi-Fi 5"), ("he", "Wi-Fi 6"), ("eht", "Wi-Fi 7")):
                if band.get(flag) and label not in standards:
                    standards.append(label)
        radios.append({
            "exist": True,
            "device": radio,
            "phy": phy_name,
            "interface_section": interface,
            "interfaces": _phy_interfaces(phy_name),
            "device_type": "PCIE" if identity["kind"] == "pcie" else "Builtin" if identity["kind"] == "onboard" else "Unknown",
            "hardware": identity,
            "capabilities": caps,
            "standards": standards,
            "channels": _channels(phy_name, caps),
            "txpowers": _txpowers(iw_device),
            "enabled": not disabled and not iface_disabled,
            "runtime": runtime,
            "current": current,
            # Compatibility fields retained for older clients.
            "ssid": current["ssid"],
            "encryption": current["encryption"],
            "password": current["password"],
            "hidden": "1" if current["hidden"] else "0",
            "band": current["band"],
            "htmode": current["htmode"],
        })

    countries = _countries(country_source) if country_source else []
    return {"interfaces": radios, "countries": countries}


def _allowed_modes(radio, band):
    return set((radio.get("capabilities", {}).get("bands", {}).get(band, {}) or {}).get("modes", []) or [])


def _find_radio(device):
    for radio in get_wireless_state().get("interfaces", []):
        if radio.get("device") == device:
            return radio
    return None


def _ensure_wifiwan():
    network = _uci_show("network")
    if network.get("network.wifiwan") != "interface":
        _uci_set("network.wifiwan", "interface")
    _uci_set("network.wifiwan.proto", "dhcp")
    _uci_set("network.wifiwan.metric", "40")
    _uci_set("network.wifiwan.auto", "1")
    _run(["uci", "commit", "network"])

    firewall = _uci_show("firewall")
    wan_section = ""
    for key, value in firewall.items():
        if key.endswith(".name") and value == "wan":
            wan_section = key[:-5]
            break
    if wan_section:
        current = firewall.get(wan_section + ".network", "").split()
        if "wifiwan" not in current:
            _run(["uci", "add_list", "{}.network=wifiwan".format(wan_section)])
            _run(["uci", "commit", "firewall"])


def _reload_radio(radio):
    try:
        _run(["/sbin/wifi", "reload", radio], timeout=35)
    except Exception:
        pass


def save_wireless(payload):
    payload = payload if isinstance(payload, dict) else {}
    device = str(payload.get("device", ""))
    radio = _find_radio(device)
    if not radio:
        return {"status": "failed", "message": "无线设备不存在"}, 400

    mode = str(payload.get("mode", "ap")).lower()
    ssid = str(payload.get("ssid", "")).strip()
    encryption = str(payload.get("encryption", "none")).lower()
    password = str(payload.get("password", ""))
    band = str(payload.get("band", "")).lower()
    htmode = str(payload.get("htmode", "")).upper()
    channel = str(payload.get("channel", "auto")).lower()
    country = str(payload.get("country", "00")).upper()
    txpower = str(payload.get("txpower", "auto")).lower()
    bssid = str(payload.get("bssid", "")).strip().upper()

    if mode not in ("ap", "sta"):
        return {"status": "failed", "message": "工作模式无效"}, 400
    if not ssid or len(ssid.encode("utf-8")) > 32:
        return {"status": "failed", "message": "SSID 必须为 1 至 32 字节"}, 400
    if band not in radio.get("capabilities", {}).get("bands", {}):
        return {"status": "failed", "message": "该无线模块不支持所选频段"}, 400
    if htmode not in _allowed_modes(radio, band):
        return {"status": "failed", "message": "该频段不支持所选制式或带宽"}, 400
    if channel != "auto" and (not channel.isdigit() or int(channel) < 1 or int(channel) > 233):
        return {"status": "failed", "message": "信道无效"}, 400
    if country != "00" and not _COUNTRY_RE.match(country):
        return {"status": "failed", "message": "国家代码无效"}, 400
    if encryption not in ("none", "psk2", "sae-mixed", "sae"):
        return {"status": "failed", "message": "加密方式无效"}, 400
    if band == "6g":
        encryption = "sae"
    if encryption != "none" and not 8 <= len(password) <= 63:
        return {"status": "failed", "message": "无线密码必须为 8 至 63 个字符"}, 400
    if bssid and not re.match(r"^(?:[0-9A-F]{2}:){5}[0-9A-F]{2}$", bssid):
        return {"status": "failed", "message": "BSSID 格式无效"}, 400
    if txpower != "auto" and (not txpower.isdigit() or int(txpower) < 0 or int(txpower) > 40):
        return {"status": "failed", "message": "发射功率无效"}, 400

    section = radio["interface_section"]
    radio_key = "wireless.{}".format(device)
    iface_key = "wireless.{}".format(section)
    try:
        # Preserve both radio and interface disabled keys.  This endpoint only
        # changes parameters; it never powers a radio on or off.
        _uci_set(iface_key, "wifi-iface")
        _uci_set(iface_key + ".device", device)
        _uci_set(iface_key + ".mode", mode)
        _uci_set(iface_key + ".ssid", ssid)
        _uci_set(iface_key + ".encryption", encryption)
        _uci_set(iface_key + ".network", "lan" if mode == "ap" else "wifiwan")
        _uci_set(radio_key + ".band", band)
        _uci_set(radio_key + ".htmode", htmode)
        _uci_set(radio_key + ".channel", channel)
        _uci_set(radio_key + ".country", country)
        if encryption == "none":
            _uci_delete(iface_key + ".key")
            _uci_delete(iface_key + ".ieee80211w")
        else:
            _uci_set(iface_key + ".key", password)
            if encryption in ("sae", "sae-mixed"):
                _uci_set(iface_key + ".ieee80211w", "2" if encryption == "sae" else "1")
            else:
                _uci_delete(iface_key + ".ieee80211w")
        if mode == "ap" and bool(payload.get("hidden", False)):
            _uci_set(iface_key + ".hidden", "1")
        else:
            _uci_delete(iface_key + ".hidden")
        if mode == "sta" and bssid:
            _uci_set(iface_key + ".bssid", bssid)
        else:
            _uci_delete(iface_key + ".bssid")
        if txpower == "auto":
            _uci_delete(radio_key + ".txpower")
        else:
            _uci_set(radio_key + ".txpower", txpower)
        if mode == "sta":
            _ensure_wifiwan()
        result = _run(["uci", "commit", "wireless"])
        if result.returncode != 0:
            raise RuntimeError(result.stderr.strip() or "commit failed")
    except Exception as exc:
        return {"status": "failed", "message": "保存失败：{}".format(exc)}, 500

    threading.Thread(target=_reload_radio, args=(device,), daemon=True).start()
    return {
        "status": "ok",
        "message": "参数已保存，{} 电源状态保持不变".format(
            radio["hardware"].get("model") or "无线模块"),
        "enabled_unchanged": True,
    }, 200


def scan_wireless(device):
    radio = _find_radio(str(device))
    if not radio:
        return {"status": "failed", "message": "无线设备不存在", "results": []}, 404
    if not radio.get("enabled") or not radio.get("runtime", {}).get("up"):
        return {"status": "failed", "message": "该无线模块已关闭，请先到设备控制页面开启", "results": []}, 409
    iw_device = _iwinfo_device(radio["phy"])
    result = _iwinfo("scan", iw_device, timeout=25)
    networks = []
    for item in result.get("results", []) or []:
        ssid = str(item.get("ssid", ""))
        if not ssid:
            continue
        encryption = item.get("encryption", {}) or {}
        auth = " ".join(str(x).lower() for x in encryption.get("authentication", []) or [])
        if "sae" in auth:
            security = "sae"
        elif encryption.get("enabled"):
            security = "psk2"
        else:
            security = "none"
        networks.append({
            "ssid": ssid,
            "bssid": str(item.get("bssid", "")),
            "channel": item.get("channel"),
            "signal": item.get("signal"),
            "quality": item.get("quality"),
            "quality_max": item.get("quality_max"),
            "encryption": security,
        })
    networks.sort(key=lambda item: int(item.get("signal") or -200), reverse=True)
    return {"status": "ok", "results": networks}, 200
