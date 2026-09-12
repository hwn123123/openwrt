#!/usr/bin/env python3
"""NAS integration for the Photonicat vendor UI.

The vendor 2.2.x firmware ships its NAS implementation as CPython 3.13-only
bytecode.  nwrt uses Python 3.14, so this module implements the same HTTP API
using OpenWrt's native block, UCI and service tools.  All file operations are
confined to mounted data volumes and all destructive disk operations reject
system partitions.
"""

from __future__ import annotations

import hashlib
import json
import mimetypes
import os
import re
import secrets
import shutil
import socket
import stat
import subprocess
import tarfile
import tempfile
import threading
import time
import uuid
from pathlib import Path

from flask import after_this_request, jsonify, make_response, redirect, render_template
from flask import request, send_file, session


STATE_DIR = Path("/etc/pcat-nas")
STATE_FILE = STATE_DIR / "config.json"
LOG_DIR = Path("/var/log/pcat-nas")
MOUNT_BASE = Path("/mnt/pcat-nas")
CRONTAB_FILE = Path("/etc/crontabs/root")
CRON_BEGIN = "# BEGIN PCAT NAS SYNC"
CRON_END = "# END PCAT NAS SYNC"
PROTECTED_MOUNTS = {"/", "/rom", "/overlay", "/boot", "/opt/docker"}
ALLOWED_FS = {"ext4", "exfat", "vfat", "f2fs", "btrfs"}
NAME_RE = re.compile(r"^[A-Za-z0-9._ -]{1,64}$")

_state_lock = threading.RLock()
_jobs_lock = threading.RLock()
_jobs: dict[str, dict] = {}
_socketio = None


def _run(argv, timeout=20, input_text=None, check=False):
    try:
        result = subprocess.run(
            argv, input=input_text, text=True, capture_output=True,
            timeout=timeout, check=False,
        )
    except (OSError, subprocess.TimeoutExpired) as exc:
        if check:
            raise RuntimeError(str(exc)) from exc
        return 127, "", str(exc)
    if check and result.returncode:
        raise RuntimeError((result.stderr or result.stdout or "command failed").strip())
    return result.returncode, result.stdout, result.stderr


def _which(name):
    return shutil.which(name) is not None


def _atomic_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(path.suffix + ".tmp")
    with open(tmp, "w", encoding="utf-8") as handle:
        json.dump(value, handle, ensure_ascii=False, indent=2)
        handle.flush()
        os.fsync(handle.fileno())
    os.replace(tmp, path)
    os.chmod(path, 0o600)


def _load_state():
    default = {"links": [], "shares": [], "users": [], "sync": [], "remotes": []}
    with _state_lock:
        try:
            with open(STATE_FILE, encoding="utf-8") as handle:
                saved = json.load(handle)
            if isinstance(saved, dict):
                default.update(saved)
        except (OSError, ValueError):
            pass
    return default


def _save_state(state):
    with _state_lock:
        _atomic_json(STATE_FILE, state)


def _sync_schedule(job):
    schedule = str(job.get("schedule") or "daily")
    presets = {"hourly": "7 * * * *", "daily": "17 3 * * *", "weekly": "27 3 * * 0"}
    if schedule in presets:
        return presets[schedule]
    if schedule == "cron":
        fields = str(job.get("cron") or "").split()
        if len(fields) != 5 or any(not re.fullmatch(r"[0-9*/?,\-]+", field) for field in fields):
            raise ValueError("自定义计划必须是安全的五段 cron 表达式")
        return " ".join(fields)
    raise ValueError("不支持的同步计划")


def _install_sync_crontab(state):
    """Install only our marked cron block and preserve every user cron entry."""
    try:
        current = CRONTAB_FILE.read_text(encoding="utf-8")
    except OSError:
        current = ""
    kept, skipping = [], False
    for line in current.splitlines():
        if line.strip() == CRON_BEGIN:
            skipping = True
            continue
        if line.strip() == CRON_END:
            skipping = False
            continue
        if not skipping:
            kept.append(line)
    jobs = []
    for job in state.get("sync", []):
        if not job.get("enabled"):
            continue
        ident = re.sub(r"[^A-Za-z0-9_-]", "", str(job.get("id") or ""))[:40]
        if not ident:
            continue
        expression = _sync_schedule(job)
        command = ("/usr/bin/python3 /usr/share/pcat-manager-web/pcat_nas.py "
                   f"--run-sync {ident} >>/var/log/pcat-nas/{ident}.cron.log 2>&1")
        jobs.append(expression + " " + command)
    content = "\n".join(kept).rstrip()
    if jobs:
        content += ("\n" if content else "") + CRON_BEGIN + "\n" + "\n".join(jobs) + "\n" + CRON_END
    CRONTAB_FILE.parent.mkdir(parents=True, exist_ok=True)
    tmp = CRONTAB_FILE.with_suffix(".pcat-nas.tmp")
    tmp.write_text(content.rstrip() + ("\n" if content else ""), encoding="utf-8")
    os.chmod(tmp, 0o600)
    os.replace(tmp, CRONTAB_FILE)
    if _which("crond"):
        rc, _, _ = _run(["/etc/init.d/cron", "reload"], timeout=15)
        if rc:
            _run(["/etc/init.d/cron", "restart"], timeout=15)


def _copy_sync_contents(src, dst, delete=False):
    os.makedirs(dst, mode=0o775, exist_ok=True)
    if os.path.isdir(src):
        source_names = set(os.listdir(src))
        if delete:
            for name in set(os.listdir(dst)) - source_names:
                target = os.path.join(dst, name)
                shutil.rmtree(target) if os.path.isdir(target) and not os.path.islink(target) else os.unlink(target)
        for name in source_names:
            source = os.path.join(src, name)
            target = os.path.join(dst, name)
            if os.path.isdir(source) and not os.path.islink(source):
                shutil.copytree(source, target, dirs_exist_ok=True)
            else:
                shutil.copy2(source, target, follow_symlinks=False)
    else:
        shutil.copy2(src, os.path.join(dst, os.path.basename(src)), follow_symlinks=False)


def _run_sync_item(item, update=lambda **_values: None):
    src = _resolve_path(item.get("src", ""))
    dst = _resolve_path(item.get("dst", ""))
    LOG_DIR.mkdir(parents=True, exist_ok=True)
    log_path = LOG_DIR / (str(item["id"]) + ".log")
    if _which("rsync"):
        argv = ["rsync", "-a", "--info=stats2"]
        if item.get("delete"):
            argv.append("--delete")
        argv.extend([src.rstrip("/") + "/", dst.rstrip("/") + "/"])
        rc, out, err = _run(argv, timeout=86400)
        log_path.write_text(out + err, encoding="utf-8")
        if rc:
            raise RuntimeError(err or "rsync 失败")
    else:
        _copy_sync_contents(src, dst, delete=bool(item.get("delete")))
        log_path.write_text("同步完成（Python 兼容模式）\n", encoding="utf-8")
    update(message="同步完成")


def _remote_mountpoint(item):
    ident = re.sub(r"[^A-Za-z0-9_-]", "", str(item.get("id") or ""))[:40]
    if not ident:
        raise ValueError("远程存储 ID 无效")
    return MOUNT_BASE / ("remote-" + ident)


def _mounted_at(path):
    wanted = os.path.realpath(str(path))
    try:
        with open("/proc/mounts", encoding="utf-8") as handle:
            for line in handle:
                fields = line.split()
                if len(fields) > 1:
                    mountpoint = fields[1].replace("\\040", " ").replace("\\011", "\t")
                    if os.path.realpath(mountpoint) == wanted:
                        return True
    except OSError:
        pass
    return False


def _remote_mount(item):
    if not item.get("enabled", True):
        raise RuntimeError("远程存储已停用")
    target = _remote_mountpoint(item)
    target.mkdir(parents=True, exist_ok=True)
    if _mounted_at(target):
        return
    kind = item.get("type")
    if kind == "sshfs":
        if not _which("sshfs"):
            raise RuntimeError("sshfs 未安装")
        host = str(item.get("host") or "").strip()
        user = str(item.get("user") or "").strip()
        remote_path = str(item.get("path") or "/")
        if not host or not user:
            raise ValueError("主机和用户名不能为空")
        argv = ["sshfs", f"{user}@{host}:{remote_path}", str(target),
                "-p", str(int(item.get("port") or 22)),
                "-o", "reconnect,ServerAliveInterval=15,ServerAliveCountMax=3,StrictHostKeyChecking=accept-new"]
        password = str(item.get("password") or "")
        if password:
            argv.extend(["-o", "password_stdin"])
        _run(argv, timeout=45, input_text=(password + "\n") if password else None, check=True)
    elif kind == "s3":
        if not _which("rclone"):
            raise RuntimeError("S3 挂载需要安装 rclone")
        ident = re.sub(r"[^A-Za-z0-9_-]", "", str(item["id"]))[:40]
        config = STATE_DIR / ("rclone-" + ident + ".conf")
        section = "pcat_" + ident
        text = "\n".join([
            f"[{section}]", "type = s3", "provider = Other",
            "access_key_id = " + str(item.get("access_key") or ""),
            "secret_access_key = " + str(item.get("secret_key") or ""),
            "endpoint = " + str(item.get("endpoint") or ""), "",
        ])
        config.write_text(text, encoding="utf-8")
        os.chmod(config, 0o600)
        remote = section + ":" + str(item.get("bucket") or "")
        _run(["rclone", "mount", remote, str(target), "--config", str(config), "--daemon"],
             timeout=45, check=True)
    else:
        raise ValueError("不支持的远程存储类型")
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        if _mounted_at(target):
            return
        time.sleep(0.2)
    raise RuntimeError("远程存储未能挂载")


def _remote_unmount(item):
    target = _remote_mountpoint(item)
    if _mounted_at(target):
        rc, out, err = _run(["umount", str(target)], timeout=30)
        if rc:
            raise RuntimeError(err or out or "卸载失败")
    try:
        target.rmdir()
    except OSError:
        pass


def _public_remotes(state):
    public = []
    for item in state.get("remotes", []):
        row = dict(item)
        target = _remote_mountpoint(item)
        row["mountpoint"] = str(target)
        row["has_secret"] = bool(item.get("password") or item.get("secret_key"))
        row["state"] = "disabled" if not item.get("enabled", True) else ("mounted" if _mounted_at(target) else "disconnected")
        row["available"] = _which("sshfs") if item.get("type") == "sshfs" else _which("rclone")
        row.pop("password", None)
        row.pop("secret_key", None)
        public.append(row)
    return public


def _json_ok(**values):
    return jsonify({"status": "ok", **values})


def _json_error(message, code="error", status=400):
    return jsonify({"status": "error", "code": code, "message": str(message)}), status


def _mountpoints(node):
    points = node.get("mountpoints")
    if isinstance(points, list):
        return [str(p) for p in points if p]
    point = node.get("mountpoint")
    return [str(point)] if point else []


def _lsblk():
    columns = "NAME,KNAME,PATH,PKNAME,TYPE,SIZE,FSTYPE,LABEL,UUID,MOUNTPOINTS,MODEL,TRAN,RO,RM"
    rc, out, err = _run(["lsblk", "-J", "-b", "-o", columns], timeout=12)
    if rc:
        raise RuntimeError((err or out or "lsblk failed").strip())
    return json.loads(out).get("blockdevices", [])


def _flatten(nodes):
    for node in nodes:
        yield node
        yield from _flatten(node.get("children") or [])


def _is_protected_node(node):
    if any(p in PROTECTED_MOUNTS for p in _mountpoints(node)):
        return True
    label = str(node.get("label") or "").lower()
    fstype = str(node.get("fstype") or "").lower()
    name = str(node.get("name") or "")
    return label in {"kernel", "rootfs", "rootfs_data"} or fstype == "squashfs" or "boot" in name


def _protected_devices():
    protected = set()
    try:
        nodes = _lsblk()
    except Exception:
        return protected
    for node in _flatten(nodes):
        if _is_protected_node(node):
            protected.add(str(node.get("path") or ""))
    return protected


def _partition_parent(dev):
    name = os.path.basename(dev)
    if name.startswith("nvme") or name.startswith("mmcblk"):
        name = re.sub(r"p\d+$", "", name)
    else:
        name = re.sub(r"\d+$", "", name)
    return "/dev/" + name


def _assert_block_device(dev, allow_disk=False):
    dev = os.path.realpath(str(dev or ""))
    if not dev.startswith("/dev/") or not os.path.exists(dev):
        raise ValueError("无效的块设备")
    mode = os.stat(dev).st_mode
    if not stat.S_ISBLK(mode):
        raise ValueError("目标不是块设备")
    protected = _protected_devices()
    if dev in protected:
        raise PermissionError("系统分区受保护，禁止操作")
    if not allow_disk and dev == _partition_parent(dev):
        raise ValueError("请选择数据分区")
    return dev


def _bus(node):
    tran = str(node.get("tran") or "").lower()
    name = str(node.get("name") or "")
    if name.startswith("nvme"):
        return "nvme"
    if name.startswith("mmcblk"):
        removable = bool(node.get("rm"))
        return "sd" if removable else "emmc"
    if tran in {"usb", "sata", "ata", "scsi"}:
        return "usb" if tran == "usb" else tran
    return tran or "unknown"


def _part_num(path):
    match = re.search(r"(?:p)?(\d+)$", path or "")
    return int(match.group(1)) if match else 0


def _statvfs(path):
    try:
        info = os.statvfs(path)
        return info.f_blocks * info.f_frsize, info.f_bavail * info.f_frsize
    except OSError:
        return None, None


def _automount_uuids():
    enabled = set()
    rc, out, _ = _run(["uci", "-q", "show", "fstab"], timeout=8)
    if rc:
        return enabled
    sections = {}
    for line in out.splitlines():
        match = re.match(r"fstab\.([^.]+)\.([^=]+)='?(.*?)'?$", line)
        if match:
            sections.setdefault(match.group(1), {})[match.group(2)] = match.group(3).strip("'")
    for values in sections.values():
        if values.get("enabled", "1") != "0" and values.get("uuid"):
            enabled.add(values["uuid"])
    return enabled


def _parted_free(dev):
    rc, out, _ = _run(["parted", "-sm", dev, "unit", "B", "print", "free"], timeout=15)
    if rc:
        return [], None
    regions, table = [], None
    for line in out.splitlines():
        fields = line.rstrip(";").split(":")
        if len(fields) >= 6 and fields[0] == dev:
            table = fields[5] or None
        if len(fields) >= 5 and fields[-1] == "free":
            try:
                regions.append({
                    "start": int(fields[1].rstrip("B")),
                    "end": int(fields[2].rstrip("B")),
                    "size": int(fields[3].rstrip("B")),
                })
            except ValueError:
                pass
    return regions, table


def _tool_status():
    return {
        "smart": _which("smartctl"), "parted": _which("parted"),
        "ext4": _which("mkfs.ext4"), "exfat": _which("mkfs.exfat"),
        "vfat": _which("mkfs.vfat"), "f2fs": _which("mkfs.f2fs"),
        "btrfs": _which("mkfs.btrfs"), "zfs": _which("zpool"),
        "samba": _which("smbd"), "rsync": _which("rsync"),
        "sshfs": _which("sshfs"), "rclone": _which("rclone"),
    }


def _disk_snapshot():
    automount = _automount_uuids()
    disks = []
    for node in _lsblk():
        name = str(node.get("name", ""))
        # Linux exposes eMMC boot/RPMB hardware areas as block devices.  They
        # are firmware storage, not user disks, and must never appear as NAS
        # formatting candidates.
        internal_area = bool(re.fullmatch(r"mmcblk\d+(?:boot\d+|rpmb)", name))
        if node.get("type") != "disk" or name.startswith(("loop", "zram", "dm-")) or internal_area:
            continue
        parts = []
        disk_protected = False
        for child in node.get("children") or []:
            if child.get("type") != "part":
                continue
            points = _mountpoints(child)
            point = points[0] if points else ""
            protected = _is_protected_node(child)
            disk_protected = disk_protected or protected
            total, avail = _statvfs(point) if point else (None, None)
            parts.append({
                "dev": child.get("path") or "/dev/" + child.get("name", ""),
                "num": _part_num(child.get("path") or child.get("name")),
                "size": int(child.get("size") or 0),
                "size_bytes": int(child.get("size") or 0),
                "fs": child.get("fstype") or "", "label": child.get("label") or "",
                "uuid": child.get("uuid") or "", "mounted": bool(point),
                "mountpoint": point, "avail_bytes": avail, "total_bytes": total,
                "protected": protected, "automount": (child.get("uuid") or "") in automount,
            })
        dev = node.get("path") or "/dev/" + node.get("name", "")
        free, _ = _parted_free(dev)
        usable_free = sum(r["size"] for r in free if r["size"] >= 16 * 1024 * 1024)
        disks.append({
            "dev": dev, "model": (node.get("model") or "").strip(),
            "bus": _bus(node), "size_bytes": int(node.get("size") or 0),
            "protected": disk_protected, "partitions": parts,
            "unallocated_bytes": usable_free,
        })
    return disks


def _volumes():
    volumes = []
    for disk in _disk_snapshot():
        for part in disk["partitions"]:
            point = part.get("mountpoint")
            if not point or point in PROTECTED_MOUNTS or part.get("protected"):
                continue
            real = os.path.realpath(point)
            if not os.path.isdir(real):
                continue
            total, free = _statvfs(real)
            volumes.append({
                "name": part.get("label") or os.path.basename(real) or part["dev"].replace("/dev/", ""),
                "mountpoint": real, "dev": part["dev"], "fs": part.get("fs") or "",
                "total_bytes": total or part.get("size_bytes") or 0,
                "free_bytes": free or 0,
            })
    for remote in _load_state().get("remotes", []):
        try:
            point = _remote_mountpoint(remote)
        except ValueError:
            continue
        if not _mounted_at(point):
            continue
        total, free = _statvfs(point)
        volumes.append({
            "name": remote.get("name") or ("远程存储 " + str(remote.get("id") or "")),
            "mountpoint": str(point), "dev": "remote:" + str(remote.get("id") or ""),
            "fs": str(remote.get("type") or "remote"), "total_bytes": total or 0,
            "free_bytes": free or 0,
        })
    return volumes


def _resolve_path(value, must_exist=True):
    if not isinstance(value, str) or not value.startswith("/"):
        raise ValueError("路径无效")
    candidate = os.path.realpath(value if must_exist else os.path.dirname(value))
    roots = [os.path.realpath(v["mountpoint"]) for v in _volumes()]
    if not any(candidate == root or candidate.startswith(root + os.sep) for root in roots):
        raise PermissionError("只允许访问已挂载的数据卷")
    if must_exist and not os.path.lexists(value):
        raise FileNotFoundError(value)
    return os.path.realpath(value) if must_exist else value


def _safe_name(name):
    if not isinstance(name, str) or not NAME_RE.fullmatch(name) or name in {".", ".."}:
        raise ValueError("名称包含不允许的字符")
    return name


def _entry(path):
    info = os.lstat(path)
    return {
        "name": os.path.basename(path),
        "type": "dir" if stat.S_ISDIR(info.st_mode) else "file",
        "size": info.st_size, "mtime": int(info.st_mtime),
        "symlink": stat.S_ISLNK(info.st_mode),
    }


def _emit_job(job):
    if _socketio is not None:
        try:
            _socketio.emit("nas_job_status", dict(job))
        except Exception:
            pass


def _start_job(kind, label, label_cn, worker, cancellable=False):
    ident = uuid.uuid4().hex[:12]
    job = {
        "id": ident, "kind": kind, "label": label, "label_cn": label_cn,
        "state": "queued", "created": int(time.time()), "pct": None,
        "message": "等待执行", "bytes_done": 0, "bytes_total": 0,
        "error": "", "log_tail": [], "cancellable": cancellable,
    }
    with _jobs_lock:
        _jobs[ident] = job

    def update(**values):
        with _jobs_lock:
            job.update(values)
            snapshot = dict(job)
        _emit_job(snapshot)

    def runner():
        update(state="running", message="正在处理")
        try:
            worker(update)
            update(state="done", pct=100, message="已完成")
        except Exception as exc:
            update(state="error", error=str(exc), message="执行失败")

    threading.Thread(target=runner, name="pcat-nas-" + kind, daemon=True).start()
    return ident


def _copy_item(src, dst, move=False):
    target = os.path.join(dst, os.path.basename(src.rstrip(os.sep)))
    if os.path.exists(target):
        raise FileExistsError(os.path.basename(target) + " 已存在")
    if move:
        shutil.move(src, target)
    elif os.path.isdir(src):
        shutil.copytree(src, target)
    else:
        shutil.copy2(src, target)


def _samba_snapshot(state=None):
    state = state or _load_state()
    installed = _which("smbd") and os.path.exists("/etc/init.d/samba4")
    running = installed and _run(["/etc/init.d/samba4", "running"], timeout=5)[0] == 0
    shares = []
    for row in state.get("shares", []):
        item = dict(row)
        try:
            _resolve_path(item.get("path", ""))
            item["valid"] = True
        except Exception:
            item["valid"] = False
        shares.append(item)
    return {"installed": installed, "running": running}, shares, state.get("users", [])


def _apply_samba(state):
    if not _which("uci") or not os.path.exists("/etc/config/samba4"):
        return
    _run(["uci", "-q", "delete", "samba4.pcat_global"])
    if _run(["uci", "-q", "get", "samba4.@samba[0]"], timeout=5)[0] != 0:
        _run(["uci", "add", "samba4", "samba"], check=True)
    _run(["uci", "set", "samba4.@samba[0].workgroup=WORKGROUP"])
    _run(["uci", "set", "samba4.@samba[0].description=Photonicat NAS"])
    _run(["uci", "set", "samba4.@samba[0].interface=lan"])
    rc, out, _ = _run(["uci", "-q", "show", "samba4"])
    for section in re.findall(r"^samba4\.([^.=]+)=sambashare$", out, re.M) if rc == 0 else []:
        if section.startswith("pcat_"):
            _run(["uci", "-q", "delete", "samba4." + section])
    for share in state.get("shares", []):
        try:
            _resolve_path(share.get("path", ""))
        except Exception:
            continue
        section = "pcat_" + re.sub(r"[^A-Za-z0-9_]", "_", share["id"])
        _run(["uci", "set", f"samba4.{section}=sambashare"])
        for key, value in {
            "name": share["name"], "path": share["path"],
            "read_only": "yes" if share.get("read_only") else "no",
            "guest_ok": "yes" if share.get("mode") == "guest" else "no",
            "guest_only": "yes" if share.get("mode") == "guest" else "no",
            "force_root": "1", "create_mask": "0664", "dir_mask": "0775",
        }.items():
            _run(["uci", "set", f"samba4.{section}.{key}={value}"])
        if share.get("mode") == "users" and share.get("users"):
            _run(["uci", "set", f"samba4.{section}.users={','.join(share['users'])}"])
    _run(["uci", "commit", "samba4"], check=True)
    _run(["/etc/init.d/samba4", "enable"])
    _run(["/etc/init.d/samba4", "restart"], timeout=30)


NAS_CN = {
    "nas_title": "网络存储中心", "tab_storage": "文件与磁盘", "tab_shares": "共享",
    "tab_remotes": "远程存储", "tab_tasks": "任务", "tab_settings": "磁盘健康",
    "disks_no_disks": "未检测到存储磁盘", "disks_partitions": "分区管理",
    "disks_protected": "系统盘", "disks_protected_hint": "系统引导和根分区受保护，不能格式化或删除",
    "disks_free_space": "未分配空间", "disks_use_free": "使用空闲空间",
    "disks_use_free_hint": "创建新的数据卷", "disks_unformatted": "未格式化",
    "disks_btn_mount": "挂载", "disks_btn_unmount": "卸载", "disks_btn_force": "强制卸载",
    "disks_busy_warn": "设备正被占用，强制卸载可能导致数据丢失", "disks_automount": "自动挂载",
    "disks_smart": "SMART 健康监测", "disks_health_ok": "健康", "disks_health_fail": "异常",
    "disks_health_na": "不可用", "files_no_volumes": "没有可浏览的数据卷",
    "files_free": "可用", "files_btn_upload": "上传文件", "files_btn_upload_folder": "上传文件夹",
    "files_btn_new_folder": "新建文件夹", "files_btn_refresh": "刷新", "files_btn_download": "下载",
    "files_btn_rename": "重命名", "files_btn_copy": "复制", "files_btn_move": "移动",
    "files_btn_paste": "粘贴", "files_btn_delete": "删除", "files_btn_preview": "预览",
    "files_btn_more": "更多操作", "files_new_folder_name": "文件夹名称", "files_col_name": "名称",
    "files_col_size": "大小", "files_col_modified": "修改时间", "files_empty_dir": "此文件夹为空",
    "files_drop_hint": "可将文件拖到这里上传", "files_upload_done": "上传完成",
    "files_preview_hint": "点击预览", "files_preview_prev": "上一个", "files_preview_next": "下一个",
    "files_preview_close": "关闭", "shares_add": "添加共享", "shares_edit": "编辑",
    "shares_btn_save": "保存", "shares_btn_cancel": "取消", "shares_btn_delete": "删除",
    "shares_choose": "选择", "shares_name": "共享名称", "shares_path": "共享路径",
    "shares_mode_guest": "访客访问", "shares_mode_users": "指定用户", "shares_read_only": "只读",
    "shares_no_shares": "尚未创建共享", "shares_invalid": "路径不可用", "shares_users_title": "共享用户",
    "shares_add_user": "添加用户", "shares_username": "用户名", "shares_password": "密码",
    "shares_samba_missing": "尚未安装 Samba 服务", "shares_install_samba": "安装 Samba",
    "shares_samba_stopped": "Samba 服务未运行", "shares_btn_service_start": "启动服务",
    "shares_http_browse": "允许网页浏览", "shares_http_browse_warn": "开启后局域网用户可通过链接浏览此共享",
    "shares_public_url": "网页地址", "link_title": "临时分享链接", "link_list_help": "为文件或目录生成可撤销的访问链接",
    "link_none": "尚无分享链接", "link_btn": "分享链接", "link_menu_hint": "生成局域网访问链接",
    "link_create": "创建链接", "link_delete": "删除链接", "link_copy": "复制", "link_copied": "已复制",
    "link_password_opt": "访问密码（可选）", "link_password_ph": "留空表示无密码", "link_protected": "有密码",
    "confirm_title": "确认操作", "confirm_btn": "确认", "confirm_cannot_undo": "此操作无法撤销，请确认已备份重要数据",
    "confirm_delete_n": "删除所选项目", "confirm_type_name": "输入磁盘名称以确认",
    "part_title": "分区管理", "part_col_num": "编号", "part_col_size": "大小", "part_col_fs": "文件系统",
    "part_free_region": "空闲区域", "part_btn_create": "新建分区", "part_btn_delete": "删除分区",
    "part_btn_format": "格式化", "part_btn_resize": "调整大小", "part_new_gpt": "新建 GPT 分区表",
    "part_new_gpt_warn": "这会清除整块磁盘上的所有分区", "part_new_size_gb": "容量（GB）",
    "part_fs_type": "文件系统", "part_fs_label": "卷标", "part_fs_not_installed": "工具未安装",
    "part_fs_unavailable": "不可用", "part_format_unmount_first": "请先卸载此分区",
    "part_resize_ext4_only": "当前仅支持调整 ext4", "part_protected_note": "系统分区已锁定",
    "part_fs_hint_ext4": "Linux 推荐，稳定可靠", "part_fs_hint_exfat": "兼容 Windows、macOS 与 Linux",
    "part_fs_hint_vfat": "兼容性最好，单文件最大 4GB", "part_fs_hint_f2fs": "适合闪存介质",
    "part_fs_hint_btrfs": "支持校验与快照", "part_fs_hint_zfs": "高级存储池",
    "backup_title": "配置备份", "backup_desc": "将设备配置备份到外部数据卷",
    "backup_not_ready": "需要至少一个已挂载的外部数据卷", "backup_run": "立即备份",
    "sync_title": "目录同步", "sync_add": "添加同步任务", "sync_edit": "编辑同步任务",
    "sync_name": "任务名称", "sync_src": "来源", "sync_dst": "目标", "sync_schedule": "计划",
    "sync_hourly": "每小时", "sync_daily": "每天", "sync_weekly": "每周", "sync_custom": "自定义 Cron",
    "sync_delete_extra": "删除目标中多余文件", "sync_enabled": "启用计划", "sync_run_now": "立即运行",
    "sync_log": "日志", "sync_no_jobs": "没有同步计划", "tasks_no_jobs": "暂无后台任务",
    "tasks_btn_cancel": "取消", "tasks_btn_dismiss": "清除", "picker_title": "选择目录", "picker_select": "选择此目录",
    "settings_automount_help": "设备启动或插入磁盘时自动挂载数据卷", "settings_automount_empty": "没有可配置的数据分区",
    "settings_smart_help": "读取 NVMe、SATA 和 USB 磁盘的实时健康数据", "settings_smart_missing": "smartctl 未安装",
    "settings_smart_refresh": "刷新", "settings_smart_temp": "温度", "settings_smart_hours": "通电时间",
    "settings_smart_attr": "属性", "settings_smart_raw": "原始值", "settings_smart_no_attrs": "没有可显示的属性",
    "settings_smart_click": "点击刷新读取健康数据", "settings_smart_empty": "没有支持 SMART 的磁盘",
    "remotes_title": "远程存储", "remotes_add": "添加远程存储", "remotes_edit": "编辑",
    "remotes_no_remotes": "尚未配置远程存储", "remotes_type": "类型", "remotes_sshfs": "SSH/SFTP",
    "remotes_s3": "S3 对象存储", "remotes_host": "主机", "remotes_port": "端口", "remotes_user": "用户名",
    "remotes_password": "密码", "remotes_remote_path": "远程路径", "remotes_endpoint": "Endpoint",
    "remotes_bucket": "Bucket", "remotes_access_key": "Access Key", "remotes_secret_key": "Secret Key",
    "remotes_mount": "挂载", "remotes_unmount": "卸载", "remotes_test": "测试连接",
    "remotes_state_disabled": "已停用", "remotes_state_disconnected": "未连接", "remotes_state_mounted": "已挂载",
    "remotes_auto_note": "凭据保存在设备本地，仅 root 可读",
}


def _locales(base):
    values = dict(base())
    if values.get("current_locale") in {"CN", "zh_CN"}:
        values.update(NAS_CN)
    else:
        # English fallback remains readable even when a new locale key is added.
        values.update({key: key.replace("_", " ").title() for key in NAS_CN})
        values.update({"nas_title": "Network Storage Center", "tab_storage": "Files & Disks",
                       "tab_shares": "Shares", "tab_remotes": "Remote Storage",
                       "tab_tasks": "Tasks", "tab_settings": "Disk Health"})
    return values


def register_nas(flask_app, api_auth_required, base_locales, socketio=None):
    global _socketio
    _socketio = socketio
    try:
        _install_sync_crontab(_load_state())
    except Exception:
        # A read-only/custom crontab must not prevent the management UI from
        # starting; saving a schedule will surface the concrete error.
        pass

    @flask_app.route("/nas")
    def nas_page():
        if "username" not in session:
            return redirect("/login?next=/nas")
        return render_template("nas.html", locales=_locales(base_locales))

    @flask_app.route("/api/v1/nas/disks.json")
    @api_auth_required
    def nas_disks():
        try:
            return _json_ok(disks=_disk_snapshot(), tools=_tool_status())
        except Exception as exc:
            return _json_error(exc, status=500)

    @flask_app.route("/api/v1/nas/files/list.json")
    @api_auth_required
    def nas_files_list():
        try:
            raw = request.args.get("path", "")
            if not raw:
                return _json_ok(volumes=_volumes())
            path = _resolve_path(raw)
            if not os.path.isdir(path):
                raise ValueError("目标不是目录")
            entries = []
            with os.scandir(path) as scan:
                for item in scan:
                    try:
                        entries.append(_entry(item.path))
                    except OSError:
                        continue
            entries.sort(key=lambda item: (item["type"] != "dir", item["name"].casefold()))
            root = next(v for v in _volumes() if path == v["mountpoint"] or path.startswith(v["mountpoint"] + os.sep))
            return _json_ok(path=path, entries=entries, volume=root)
        except Exception as exc:
            return _json_error(exc)

    @flask_app.route("/api/v1/nas/files/download")
    @api_auth_required
    def nas_file_download():
        try:
            path = _resolve_path(request.args.get("path", ""))
            if not os.path.isfile(path):
                raise ValueError("目标不是文件")
            inline = request.args.get("inline") == "1"
            return send_file(path, as_attachment=not inline, download_name=os.path.basename(path),
                             mimetype=mimetypes.guess_type(path)[0] or "application/octet-stream",
                             conditional=True)
        except Exception as exc:
            return _json_error(exc)

    @flask_app.route("/api/v1/nas/files/archive")
    @api_auth_required
    def nas_file_archive():
        try:
            paths = [_resolve_path(p) for p in request.args.getlist("path")]
            if not paths:
                raise ValueError("未选择文件")
            handle, archive = tempfile.mkstemp(prefix="pcat-nas-", suffix=".tar.gz", dir="/tmp")
            os.close(handle)
            with tarfile.open(archive, "w:gz") as tar:
                for path in paths:
                    tar.add(path, arcname=os.path.basename(path.rstrip(os.sep)), recursive=True)
            @after_this_request
            def cleanup(response):
                try:
                    os.unlink(archive)
                except OSError:
                    pass
                return response
            return send_file(archive, as_attachment=True, download_name="photonicat-nas.tar.gz")
        except Exception as exc:
            return _json_error(exc)

    @flask_app.route("/api/v1/nas/files/upload", methods=["POST"])
    @api_auth_required
    def nas_file_upload():
        try:
            dest = _resolve_path(request.args.get("dest", ""))
            rel = str(request.args.get("relpath", "upload.bin")).replace("\\", "/").lstrip("/")
            if any(part in {"", ".", ".."} for part in rel.split("/")):
                raise ValueError("上传路径无效")
            target = os.path.join(dest, rel)
            parent = _resolve_path(os.path.dirname(target)) if os.path.exists(os.path.dirname(target)) else os.path.dirname(target)
            root_test = _resolve_path(dest)
            if not (os.path.realpath(parent) == root_test or os.path.realpath(parent).startswith(root_test + os.sep)):
                raise PermissionError("上传路径越界")
            os.makedirs(parent, mode=0o775, exist_ok=True)
            policy = request.args.get("conflict", "rename")
            if os.path.exists(target):
                if policy == "skip":
                    return _json_ok(skipped=True)
                if policy != "overwrite":
                    stem, ext = os.path.splitext(target)
                    index = 1
                    while os.path.exists(f"{stem} ({index}){ext}"):
                        index += 1
                    target = f"{stem} ({index}){ext}"
            tmp = target + ".uploading"
            with open(tmp, "wb") as handle:
                while True:
                    chunk = request.stream.read(1024 * 1024)
                    if not chunk:
                        break
                    handle.write(chunk)
            os.replace(tmp, target)
            return _json_ok(name=os.path.basename(target))
        except Exception as exc:
            return _json_error(exc)

    def file_json():
        return request.get_json(silent=True) or {}

    @flask_app.route("/api/v1/nas/files/mkdir.json", methods=["POST"])
    @api_auth_required
    def nas_file_mkdir():
        try:
            path = _resolve_path(file_json().get("path", ""))
            name = _safe_name(file_json().get("name", ""))
            os.mkdir(os.path.join(path, name), 0o775)
            return _json_ok()
        except Exception as exc:
            return _json_error(exc)

    @flask_app.route("/api/v1/nas/files/rename.json", methods=["POST"])
    @api_auth_required
    def nas_file_rename():
        try:
            data = file_json()
            source = _resolve_path(data.get("path", ""))
            name = _safe_name(data.get("new_name", ""))
            target = os.path.join(os.path.dirname(source), name)
            if os.path.exists(target):
                raise FileExistsError(name + " 已存在")
            os.rename(source, target)
            return _json_ok()
        except Exception as exc:
            return _json_error(exc)

    def multi_file_action(action):
        data = file_json()
        sources = [_resolve_path(p) for p in data.get("sources", [])]
        dest = _resolve_path(data.get("dest_dir", ""))
        if not sources or not os.path.isdir(dest):
            raise ValueError("来源或目标无效")
        def worker(update):
            for index, source in enumerate(sources, 1):
                _copy_item(source, dest, move=action == "move")
                update(pct=round(index * 100 / len(sources)), message=os.path.basename(source))
        job_id = _start_job(action, action.title(), "移动文件" if action == "move" else "复制文件", worker)
        return _json_ok(job_id=job_id)

    @flask_app.route("/api/v1/nas/files/copy.json", methods=["POST"])
    @api_auth_required
    def nas_file_copy():
        try:
            return multi_file_action("copy")
        except Exception as exc:
            return _json_error(exc)

    @flask_app.route("/api/v1/nas/files/move.json", methods=["POST"])
    @api_auth_required
    def nas_file_move():
        try:
            return multi_file_action("move")
        except Exception as exc:
            return _json_error(exc)

    @flask_app.route("/api/v1/nas/files/delete.json", methods=["POST"])
    @api_auth_required
    def nas_file_delete():
        try:
            paths = [_resolve_path(p) for p in file_json().get("paths", [])]
            if not paths:
                raise ValueError("未选择文件")
            volume_roots = {v["mountpoint"] for v in _volumes()}
            if any(path in volume_roots for path in paths):
                raise PermissionError("不能删除数据卷根目录")
            def worker(update):
                for index, path in enumerate(paths, 1):
                    shutil.rmtree(path) if os.path.isdir(path) else os.unlink(path)
                    update(pct=round(index * 100 / len(paths)), message=os.path.basename(path))
            job_id = _start_job("delete", "Delete", "删除文件", worker)
            return _json_ok(job_id=job_id)
        except Exception as exc:
            return _json_error(exc)

    @flask_app.route("/api/v1/nas/jobs.json", methods=["GET", "POST"])
    @api_auth_required
    def nas_jobs():
        data = request.get_json(silent=True) or {}
        with _jobs_lock:
            if request.method == "POST" and data.get("action") == "dismiss":
                job = _jobs.get(str(data.get("id", "")))
                if job and job.get("state") not in {"queued", "running"}:
                    _jobs.pop(job["id"], None)
            rows = list(_jobs.values())[-100:]
        return _json_ok(jobs=rows)

    @flask_app.route("/api/v1/nas/mount.json", methods=["POST"])
    @api_auth_required
    def nas_mount():
        try:
            data = file_json()
            dev = _assert_block_device(data.get("dev"))
            action = data.get("action")
            disk = next((p for d in _disk_snapshot() for p in d["partitions"] if p["dev"] == dev), None)
            if not disk:
                raise ValueError("未找到分区")
            if action == "unmount":
                argv = ["umount"] + (["-l"] if data.get("force") else []) + [dev]
                rc, out, err = _run(argv, timeout=30)
                if rc:
                    return _json_error(err or out or "设备忙", code="busy", status=409)
            elif action == "mount":
                target = MOUNT_BASE / (disk.get("label") or os.path.basename(dev))
                target.mkdir(parents=True, exist_ok=True)
                _run(["mount", dev, str(target)], timeout=30, check=True)
            else:
                raise ValueError("操作无效")
            return _json_ok(disks=_disk_snapshot(), tools=_tool_status())
        except Exception as exc:
            return _json_error(exc)

    @flask_app.route("/api/v1/nas/automount.json", methods=["POST"])
    @api_auth_required
    def nas_automount():
        try:
            data = file_json()
            dev = _assert_block_device(data.get("dev"))
            part = next((p for d in _disk_snapshot() for p in d["partitions"] if p["dev"] == dev), None)
            if not part or not part.get("uuid") or not part.get("fs"):
                raise ValueError("分区没有可用的 UUID 或文件系统")
            section = "pcat_nas_" + hashlib.sha1(part["uuid"].encode()).hexdigest()[:10]
            if data.get("enabled"):
                target = str(MOUNT_BASE / (part.get("label") or os.path.basename(dev)))
                for command in [
                    ["uci", "set", f"fstab.{section}=mount"],
                    ["uci", "set", f"fstab.{section}.uuid={part['uuid']}"],
                    ["uci", "set", f"fstab.{section}.target={target}"],
                    ["uci", "set", f"fstab.{section}.enabled=1"],
                ]:
                    _run(command, check=True)
            else:
                _run(["uci", "-q", "delete", f"fstab.{section}"])
            _run(["uci", "commit", "fstab"], check=True)
            return _json_ok(disks=_disk_snapshot(), tools=_tool_status())
        except Exception as exc:
            return _json_error(exc)

    @flask_app.route("/api/v1/nas/smart.json")
    @api_auth_required
    def nas_smart():
        try:
            dev = _assert_block_device(request.args.get("dev", ""), allow_disk=True)
            if not _which("smartctl"):
                raise RuntimeError("smartctl 未安装")
            rc, out, err = _run(["smartctl", "-a", "-j", dev], timeout=35)
            if not out.strip():
                raise RuntimeError(err or "无法读取 SMART")
            raw = json.loads(out)
            passed = raw.get("smart_status", {}).get("passed")
            temp = raw.get("temperature", {}).get("current")
            hours = raw.get("power_on_time", {}).get("hours")
            attrs = []
            for row in raw.get("ata_smart_attributes", {}).get("table", []):
                attrs.append({"id": row.get("id"), "name": row.get("name"),
                              "raw": row.get("raw", {}).get("string", "")})
            nvme = raw.get("nvme_smart_health_information_log", {})
            if temp is None:
                temp = nvme.get("temperature")
            if hours is None:
                hours = nvme.get("power_on_hours")
            for key, value in nvme.items():
                if key not in {"temperature", "power_on_hours"}:
                    attrs.append({"id": "NVMe", "name": key.replace("_", " "), "raw": str(value)})
            return _json_ok(health="ok" if passed is not False and rc in {0, 2} else "fail",
                            temp_c=temp, power_on_hours=hours, attrs=attrs,
                            model=raw.get("model_name"), serial=raw.get("serial_number"))
        except Exception as exc:
            return _json_error(exc)

    @flask_app.route("/api/v1/nas/partitions.json", methods=["GET", "POST"])
    @api_auth_required
    def nas_partitions():
        try:
            if request.method == "GET":
                dev = request.args.get("dev", "")
                if not dev.startswith("/dev/"):
                    raise ValueError("磁盘无效")
                disk = next((d for d in _disk_snapshot() if d["dev"] == dev), None)
                if not disk:
                    raise ValueError("磁盘不存在")
                free, table = _parted_free(dev)
                parts = []
                for part in disk["partitions"]:
                    row = dict(part)
                    row.update({"start": 0, "end": 0})
                    parts.append(row)
                return _json_ok(dev=dev, protected=disk["protected"], table=table,
                                parts=parts, free=free, size=disk["size_bytes"])
            data = file_json()
            action = data.get("action")
            if action != "format":
                return _json_error("为保护系统盘，新建、删除和缩放分区请使用 LuCI 磁盘管理", code="protected", status=409)
            dev = _assert_block_device(data.get("dev"))
            fs = str(data.get("fs") or "ext4").lower()
            if fs not in ALLOWED_FS:
                raise ValueError("不支持的文件系统")
            if str(data.get("confirm", "")) != os.path.basename(_partition_parent(dev)):
                raise PermissionError("磁盘确认名称不匹配")
            label = re.sub(r"[^A-Za-z0-9_.-]", "-", str(data.get("label") or "pcat-data"))[:16]
            commands = {
                "ext4": ["mkfs.ext4", "-F", "-L", label, dev],
                "exfat": ["mkfs.exfat", "-L", label, dev],
                "vfat": ["mkfs.vfat", "-F", "32", "-n", label.upper()[:11], dev],
                "f2fs": ["mkfs.f2fs", "-f", "-l", label, dev],
                "btrfs": ["mkfs.btrfs", "-f", "-L", label, dev],
            }
            if not _which(commands[fs][0]):
                raise RuntimeError(commands[fs][0] + " 未安装")
            def worker(update):
                update(message="正在格式化 " + dev)
                _run(["umount", dev], timeout=20)
                rc, out, err = _run(commands[fs], timeout=900)
                if rc:
                    raise RuntimeError((err or out or "格式化失败").strip())
                update(message="格式化完成")
            return _json_ok(job_id=_start_job("format", "Format " + dev, "格式化 " + dev, worker))
        except Exception as exc:
            return _json_error(exc)

    @flask_app.route("/api/v1/nas/backup.json", methods=["GET", "POST"])
    @api_auth_required
    def nas_backup():
        volumes = _volumes()
        external = next((v for v in volumes if not v["dev"].startswith("/dev/mmcblk")), None) or (volumes[0] if volumes else None)
        dest = os.path.join(external["mountpoint"], "Photonicat-Backup") if external else ""
        if request.method == "GET":
            return _json_ok(ready=bool(external), sd=external, dest=dest)
        if not external:
            return _json_error("没有可用的数据卷")
        def worker(update):
            os.makedirs(dest, exist_ok=True)
            stamp = time.strftime("%Y%m%d-%H%M%S")
            archive = os.path.join(dest, "nwrt-config-" + stamp + ".tar.gz")
            rc, out, err = _run(["sysupgrade", "-b", archive], timeout=300)
            if rc:
                raise RuntimeError(err or out or "备份失败")
            update(message=archive)
        return _json_ok(job_id=_start_job("backup", "System backup", "系统配置备份", worker))

    @flask_app.route("/api/v1/nas/links.json", methods=["GET", "POST"])
    @api_auth_required
    def nas_links():
        state = _load_state()
        if request.method == "POST":
            data = file_json()
            if data.get("action") == "create":
                path = _resolve_path(data.get("path", ""))
                token = secrets.token_urlsafe(12)
                password = str(data.get("password") or "")
                link = {"token": token, "path": path, "name": os.path.basename(path),
                        "type": "dir" if os.path.isdir(path) else "file",
                        "has_password": bool(password),
                        "password_hash": hashlib.sha256(password.encode()).hexdigest() if password else "",
                        "created": int(time.time())}
                state["links"].append(link)
                _save_state(state)
                public = {k: v for k, v in link.items() if k != "password_hash"}
                return _json_ok(link=public, links=[{k: v for k, v in x.items() if k != "password_hash"} for x in state["links"]])
            if data.get("action") == "delete":
                state["links"] = [x for x in state["links"] if x.get("token") != data.get("token")]
                _save_state(state)
        return _json_ok(links=[{k: v for k, v in x.items() if k != "password_hash"} for x in state["links"]])

    def public_link_allowed(link):
        if not link.get("password_hash"):
            return True
        key = "nas_link_" + link["token"]
        if session.get(key):
            return True
        if request.method == "POST":
            supplied = str(request.form.get("password") or "")
            if secrets.compare_digest(hashlib.sha256(supplied.encode()).hexdigest(), link["password_hash"]):
                session[key] = True
                return True
        return False

    @flask_app.route("/s/<token>", defaults={"subpath": ""}, methods=["GET", "POST"])
    @flask_app.route("/s/<token>/<path:subpath>", methods=["GET", "POST"])
    def nas_public_link(token, subpath):
        link = next((x for x in _load_state()["links"] if x.get("token") == token), None)
        if not link:
            return "Not found", 404
        if not public_link_allowed(link):
            return render_template("public_link_password.html", token=token, name=link.get("name") or "共享文件",
                                   error=request.method == "POST")
        try:
            base = _resolve_path(link["path"])
            path = base if not subpath else os.path.realpath(os.path.join(base, subpath))
            if path != base and not path.startswith(base + os.sep):
                raise PermissionError("bad path")
            if os.path.isfile(path):
                return send_file(path, as_attachment=request.args.get("inline") != "1")
            entries = [_entry(item.path) for item in os.scandir(path)]
            entries.sort(key=lambda item: (item["type"] != "dir", item["name"].casefold()))
            crumbs = [p for p in subpath.split("/") if p]
            return render_template("public_share.html", share_name=link["name"], entries=entries,
                                   crumbs=crumbs, truncated=False, base_url="/s/" + token + "/")
        except Exception:
            return "Not found", 404

    @flask_app.route("/api/v1/nas/shares.json", methods=["GET", "POST"])
    @api_auth_required
    def nas_shares():
        try:
            state = _load_state()
            if request.method == "POST":
                data = file_json()
                if data.get("action") in {"create", "update"}:
                    name = _safe_name(data.get("name", ""))
                    path = _resolve_path(data.get("path", ""))
                    ident = str(data.get("id") or uuid.uuid4().hex[:10])
                    item = {"id": ident, "name": name, "path": path,
                            "mode": "users" if data.get("mode") == "users" else "guest",
                            "users": [u for u in data.get("users", []) if u in state.get("users", [])],
                            "read_only": bool(data.get("read_only")), "http_browse": bool(data.get("http_browse"))}
                    state["shares"] = [x for x in state["shares"] if x.get("id") != ident] + [item]
                elif data.get("action") == "delete":
                    state["shares"] = [x for x in state["shares"] if x.get("id") != data.get("id")]
                _save_state(state)
                if _which("smbd"):
                    _apply_samba(state)
            samba, shares, users = _samba_snapshot(state)
            return _json_ok(samba=samba, shares=shares, users=users)
        except Exception as exc:
            return _json_error(exc)

    @flask_app.route("/api/v1/nas/share_users.json", methods=["POST"])
    @api_auth_required
    def nas_share_users():
        try:
            data = file_json()
            state = _load_state()
            name = re.sub(r"[^a-zA-Z0-9_-]", "", str(data.get("name") or ""))[:32]
            if not name:
                raise ValueError("用户名无效")
            if data.get("action") == "add":
                if not _which("smbpasswd"):
                    raise RuntimeError("请先安装 Samba")
                _run(["id", name], timeout=5)[0] or None
                if _run(["id", name], timeout=5)[0] != 0:
                    if _which("useradd"):
                        argv = ["useradd", "-M", "-s", "/bin/false", name]
                    elif _which("adduser"):
                        argv = ["adduser", "-D", "-H", "-s", "/bin/false", name]
                    else:
                        raise RuntimeError("系统缺少 useradd/adduser，无法创建共享账户")
                    _run(argv, timeout=15, check=True)
                password = str(data.get("password") or "")
                _run(["smbpasswd", "-a", "-s", name], input_text=password + "\n" + password + "\n", timeout=20, check=True)
                if name not in state["users"]:
                    state["users"].append(name)
            elif data.get("action") == "remove":
                _run(["smbpasswd", "-x", name], timeout=15)
                state["users"] = [u for u in state["users"] if u != name]
                for share in state["shares"]:
                    share["users"] = [u for u in share.get("users", []) if u != name]
            _save_state(state)
            return _json_ok(users=state["users"], shares=_samba_snapshot(state)[1])
        except Exception as exc:
            return _json_error(exc)

    @flask_app.route("/api/v1/nas/samba.json", methods=["POST"])
    @api_auth_required
    def nas_samba():
        try:
            if not _which("smbd"):
                raise RuntimeError("Samba 未安装")
            action = file_json().get("action")
            if action in {"enable", "start"}:
                _apply_samba(_load_state())
            elif action in {"disable", "stop"}:
                _run(["/etc/init.d/samba4", "stop"], timeout=30)
                if action == "disable":
                    _run(["/etc/init.d/samba4", "disable"])
            samba, shares, _ = _samba_snapshot()
            return _json_ok(samba=samba, shares=shares)
        except Exception as exc:
            return _json_error(exc)

    @flask_app.route("/public/share/<name>/", defaults={"subpath": ""})
    @flask_app.route("/public/share/<name>/<path:subpath>")
    def nas_public_share(name, subpath):
        share = next((x for x in _load_state()["shares"] if x.get("name") == name and x.get("http_browse")), None)
        if not share:
            return "Not found", 404
        try:
            base = _resolve_path(share["path"])
            path = os.path.realpath(os.path.join(base, subpath))
            if path != base and not path.startswith(base + os.sep):
                raise PermissionError("bad path")
            if os.path.isfile(path):
                return send_file(path, as_attachment=True)
            entries = [_entry(item.path) for item in os.scandir(path)]
            entries.sort(key=lambda item: (item["type"] != "dir", item["name"].casefold()))
            return render_template("public_share.html", share_name=name, entries=entries,
                                   crumbs=[p for p in subpath.split("/") if p], truncated=False,
                                   base_url="/public/share/" + name + "/")
        except Exception:
            return "Not found", 404

    @flask_app.route("/api/v1/nas/sync.json", methods=["GET", "POST"])
    @api_auth_required
    def nas_sync():
        try:
            state = _load_state()
            if request.method == "POST":
                data = file_json()
                action = data.get("action")
                if action in {"create", "update"}:
                    src, dst = _resolve_path(data.get("src", "")), _resolve_path(data.get("dst", ""))
                    ident = re.sub(r"[^A-Za-z0-9_-]", "", str(data.get("id") or ""))[:40] or uuid.uuid4().hex[:10]
                    item = {"id": ident, "name": str(data.get("name") or "同步任务")[:64],
                            "src": src, "dst": dst, "schedule": data.get("schedule", "daily"),
                            "cron": str(data.get("cron") or ""), "delete": bool(data.get("delete")),
                            "enabled": bool(data.get("enabled", True))}
                    _sync_schedule(item)
                    state["sync"] = [x for x in state["sync"] if x.get("id") != ident] + [item]
                    _save_state(state)
                    _install_sync_crontab(state)
                elif action == "delete":
                    state["sync"] = [x for x in state["sync"] if x.get("id") != data.get("id")]
                    _save_state(state)
                    _install_sync_crontab(state)
                elif action == "run_now":
                    item = next((x for x in state["sync"] if x.get("id") == data.get("id")), None)
                    if not item:
                        raise ValueError("同步任务不存在")
                    def worker(update):
                        _run_sync_item(item, update)
                    _start_job("sync", item["name"], "同步：" + item["name"], worker)
            return _json_ok(jobs=state["sync"])
        except Exception as exc:
            return _json_error(exc)
    @flask_app.route("/api/v1/nas/sync_log.json")
    @api_auth_required
    def nas_sync_log():
        ident = re.sub(r"[^A-Za-z0-9_-]", "", request.args.get("id", ""))
        path = LOG_DIR / (ident + ".log")
        try:
            text = path.read_text(encoding="utf-8", errors="replace")[-100000:]
        except OSError:
            text = ""
        return _json_ok(log=text)

    @flask_app.route("/api/v1/nas/remotes.json", methods=["GET", "POST"])
    @api_auth_required
    def nas_remotes():
        try:
            state = _load_state()
            if request.method == "POST":
                data = file_json()
                action = data.get("action")
                if action in {"create", "update"}:
                    ident = re.sub(r"[^A-Za-z0-9_-]", "", str(data.get("id") or ""))[:40] or uuid.uuid4().hex[:10]
                    old = next((x for x in state["remotes"] if x.get("id") == ident), {})
                    item = {key: data.get(key, old.get(key, "")) for key in
                            ["type", "name", "host", "port", "user", "password", "path", "endpoint",
                             "bucket", "access_key", "secret_key", "enabled"]}
                    item.update({"id": ident, "state": "disconnected"})
                    for secret_key in ("password", "secret_key"):
                        if not data.get(secret_key) and old.get(secret_key):
                            item[secret_key] = old[secret_key]
                    state["remotes"] = [x for x in state["remotes"] if x.get("id") != ident] + [item]
                    _save_state(state)
                elif action == "delete":
                    old = next((x for x in state["remotes"] if x.get("id") == data.get("id")), None)
                    if old:
                        _remote_unmount(old)
                    state["remotes"] = [x for x in state["remotes"] if x.get("id") != data.get("id")]
                    _save_state(state)
                elif action == "test":
                    item = next((x for x in state["remotes"] if x.get("id") == data.get("id")), None)
                    if not item:
                        raise ValueError("配置不存在")
                    if item.get("type") == "sshfs":
                        with socket.create_connection((item.get("host"), int(item.get("port") or 22)), timeout=5):
                            pass
                    elif item.get("type") == "s3":
                        if not _which("rclone"):
                            raise RuntimeError("rclone 未安装，无法测试 S3")
                elif action in {"mount", "unmount"}:
                    item = next((x for x in state["remotes"] if x.get("id") == data.get("id")), None)
                    if not item:
                        raise ValueError("配置不存在")
                    _remote_mount(item) if action == "mount" else _remote_unmount(item)
            return _json_ok(remotes=_public_remotes(state), tools=_tool_status())
        except Exception as exc:
            return _json_error(exc)


def _main():
    if len(os.sys.argv) == 3 and os.sys.argv[1] == "--run-sync":
        ident = re.sub(r"[^A-Za-z0-9_-]", "", os.sys.argv[2])[:40]
        item = next((x for x in _load_state().get("sync", []) if x.get("id") == ident), None)
        if not item or not item.get("enabled"):
            raise SystemExit("同步任务不存在或已停用")
        _run_sync_item(item)
        return
    raise SystemExit("usage: pcat_nas.py --run-sync ID")


if __name__ == "__main__":
    _main()
