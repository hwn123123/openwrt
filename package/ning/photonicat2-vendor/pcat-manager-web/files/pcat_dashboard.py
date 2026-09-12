"""Small collectors for the vendor dashboard.

All values come from procfs/sysfs.  The module does not start a worker or poll
hardware by itself; callers decide when a dashboard refresh should sample it.
"""

import glob
import os
import subprocess
import threading


def _read_text(path, default=""):
    try:
        with open(path, "r", encoding="utf-8") as handle:
            return handle.read().strip().strip("\x00")
    except OSError:
        return default


def _cpu_counters():
    try:
        fields = _read_text("/proc/stat").splitlines()[0].split()
        if not fields or fields[0] != "cpu":
            return None
        ticks = [int(value) for value in fields[1:]]
        idle = ticks[3] + (ticks[4] if len(ticks) > 4 else 0)
        return sum(ticks), idle
    except (IndexError, ValueError):
        return None


def _cpu_description(board_info):
    compatible = _read_text("/sys/firmware/devicetree/base/compatible")
    compatibles = [item for item in compatible.split("\x00") if item]
    soc = next((item.split(",", 1)[1].upper()
                for item in compatibles if item.startswith("rockchip,")), "")
    system = str((board_info or {}).get("system") or os.uname().machine)
    if soc and soc.lower() not in system.lower():
        return "Rockchip {} · {}".format(soc, system)
    return system


class SystemSampler:
    """Compute CPU deltas while also returning stable hardware facts."""

    def __init__(self):
        self._cpu_sample = _cpu_counters()

    def read(self, board_info=None):
        current = _cpu_counters()
        cpu_usage = None
        if current is not None and self._cpu_sample is not None:
            total_delta = current[0] - self._cpu_sample[0]
            idle_delta = current[1] - self._cpu_sample[1]
            if total_delta > 0:
                cpu_usage = round(max(0.0, min(
                    100.0, (total_delta - idle_delta) * 100.0 / total_delta
                )), 1)
        if current is not None:
            self._cpu_sample = current

        memory = {}
        try:
            for line in _read_text("/proc/meminfo").splitlines():
                key, separator, raw_value = line.partition(":")
                if separator:
                    memory[key] = int(raw_value.split()[0])
        except (ValueError, IndexError):
            memory = {}
        total_kib = memory.get("MemTotal", 0)
        available_kib = memory.get("MemAvailable")
        if available_kib is None:
            available_kib = sum(memory.get(key, 0) for key in (
                "MemFree", "Buffers", "Cached", "SReclaimable"
            ))
        used_kib = max(0, total_kib - available_kib)
        memory_usage = round(used_kib * 100.0 / total_kib, 1) if total_kib else None
        try:
            load_average = round(os.getloadavg()[0], 2)
        except OSError:
            load_average = None

        return {
            "cpu_usage": cpu_usage,
            "cpu_model": _cpu_description(board_info),
            "cpu_cores": os.cpu_count(),
            "load_average": load_average,
            "memory_usage": memory_usage,
            "memory_used_bytes": used_kib * 1024 if total_kib else None,
            "memory_total_bytes": total_kib * 1024 if total_kib else None,
        }


class ModemRuntimeSampler:
    """Read the modem's own Linux runtime through its USB ADB interface.

    The sampler is entirely demand driven. It starts no worker and executes
    no command until the web UI asks for modem runtime data.
    """

    _separator = "__PCAT_MODEM_MEMORY__"
    _thermal_separator = "__PCAT_MODEM_THERMAL__"

    def __init__(self):
        self._cpu_sample = None
        self._lock = threading.Lock()

    @staticmethod
    def _parse_cpu(line):
        try:
            fields = line.split()
            if not fields or fields[0] != "cpu":
                return None
            ticks = [int(value) for value in fields[1:]]
            if len(ticks) < 4:
                return None
            idle = ticks[3] + (ticks[4] if len(ticks) > 4 else 0)
            return sum(ticks), idle
        except (TypeError, ValueError):
            return None

    @staticmethod
    def _parse_memory(lines):
        values = {}
        try:
            for line in lines:
                key, separator, raw_value = line.partition(":")
                if separator:
                    values[key] = int(raw_value.split()[0])
        except (ValueError, IndexError):
            return None, None, None
        total_kib = values.get("MemTotal", 0)
        available_kib = values.get("MemAvailable")
        if available_kib is None:
            available_kib = sum(values.get(key, 0) for key in (
                "MemFree", "Buffers", "Cached", "SReclaimable"
            ))
        if not total_kib:
            return None, None, None
        used_kib = max(0, total_kib - available_kib)
        return (
            round(used_kib * 100.0 / total_kib, 1),
            used_kib * 1024,
            total_kib * 1024,
        )

    @staticmethod
    def _parse_thermal(lines):
        sensors = {}
        for line in lines:
            name, separator, raw_value = line.strip().partition("|")
            if not separator or not name:
                continue
            try:
                value = float(raw_value)
            except ValueError:
                continue
            if abs(value) > 1000:
                value /= 1000.0
            if -40 <= value <= 150:
                sensors[name] = round(value, 1)
        primary = next((sensors[name] for name in (
            "soc_max", "md_5g", "md_4g", "md_rf", "rf_ntc"
        ) if name in sensors), None)
        return primary, sensors

    def read(self):
        result = {
            "available": False,
            "source": "adb",
            "cpu_usage": None,
            "memory_usage": None,
            "memory_used_bytes": None,
            "memory_total_bytes": None,
            "temperature": None,
            "thermal_sensors": {},
        }
        try:
            completed = subprocess.run(
                ["/usr/bin/adb", "shell",
                 ("head -n 1 /proc/stat; echo {memory}; cat /proc/meminfo; "
                  "echo {thermal}; for zone in /sys/class/thermal/thermal_zone*; "
                  "do [ -r \"$zone/temp\" ] || continue; "
                  "printf '%s|' \"$(cat \"$zone/type\" 2>/dev/null)\"; "
                  "cat \"$zone/temp\"; done").format(
                      memory=self._separator,
                      thermal=self._thermal_separator)],
                capture_output=True, text=True, timeout=3, check=False,
            )
        except (OSError, subprocess.SubprocessError):
            return result
        if completed.returncode != 0 or self._separator not in completed.stdout:
            return result

        cpu_text, payload = completed.stdout.split(self._separator, 1)
        if self._thermal_separator in payload:
            memory_text, thermal_text = payload.split(
                self._thermal_separator, 1)
        else:
            memory_text, thermal_text = payload, ""
        cpu_lines = cpu_text.strip().splitlines()
        current = self._parse_cpu(cpu_lines[0] if cpu_lines else "")
        memory_usage, memory_used, memory_total = self._parse_memory(
            memory_text.strip().splitlines())
        temperature, thermal_sensors = self._parse_thermal(
            thermal_text.strip().splitlines())

        with self._lock:
            if current is not None and self._cpu_sample is not None:
                total_delta = current[0] - self._cpu_sample[0]
                idle_delta = current[1] - self._cpu_sample[1]
                if total_delta > 0 and 0 <= idle_delta <= total_delta:
                    result["cpu_usage"] = round(
                        (total_delta - idle_delta) * 100.0 / total_delta, 1)
            if current is not None:
                self._cpu_sample = current

        result.update({
            "available": current is not None or memory_total is not None,
            "memory_usage": memory_usage,
            "memory_used_bytes": memory_used,
            "memory_total_bytes": memory_total,
            "temperature": temperature,
            "thermal_sensors": thermal_sensors,
        })
        return result


def _block_size(block_name):
    try:
        return int(_read_text("/sys/class/block/{}/size".format(block_name))) * 512
    except ValueError:
        return None


def _mounted_filesystem(block_name):
    prefixes = ("/dev/{}".format(block_name),)
    try:
        with open("/proc/mounts", "r", encoding="utf-8") as mounts:
            rows = [line.split()[:2] for line in mounts if len(line.split()) >= 2]
    except OSError:
        return None
    for source, mount_point in rows:
        if not source.startswith(prefixes) or mount_point == "/rom":
            continue
        try:
            stats = os.statvfs(mount_point)
            return {
                "mount": mount_point,
                "size": stats.f_blocks * stats.f_frsize,
                "free": stats.f_bavail * stats.f_frsize,
            }
        except OSError:
            continue
    return None


def _block_info(block_name, label):
    mounted = _mounted_filesystem(block_name)
    model = (_read_text("/sys/class/block/{}/device/name".format(block_name)) or
             _read_text("/sys/class/block/{}/device/model".format(block_name)) or label)
    info = {
        "present": True,
        "device": "/dev/{}".format(block_name),
        "model": model,
        "size": _block_size(block_name),
        "free": None,
        "mount": "",
    }
    if mounted:
        info.update(mounted)
    return info


def read_storage():
    """Distinguish the built-in eMMC from a genuinely inserted SD card."""
    result = {
        "internal": {"present": False},
        "sd": {"present": False},
        "nvme": {"present": False},
    }
    for path in sorted(glob.glob("/sys/class/block/mmcblk[0-9]")):
        name = os.path.basename(path)
        media_type = _read_text(os.path.join(path, "device/type")).upper()
        removable = _read_text(os.path.join(path, "removable")) == "1"
        if media_type == "MMC" and not result["internal"]["present"]:
            result["internal"] = _block_info(name, "内置 eMMC")
        elif (media_type == "SD" or removable) and not result["sd"]["present"]:
            result["sd"] = _block_info(name, "SD 卡")

    nvme_names = sorted(glob.glob("/sys/class/block/nvme*n[0-9]"))
    if nvme_names:
        result["nvme"] = _block_info(os.path.basename(nvme_names[0]), "NVMe 固态硬盘")
    return result
