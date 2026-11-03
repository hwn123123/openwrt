#!/usr/bin/env python3
"""Read-only shennong modem-partition capture and local integrity check.

This program has no modem write, QMI write, restore, or flash operation.
"""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import sys


PARTITIONS = ("modemst1", "modemst2", "fsg")
EXPECTED_BYTES = 8 * 1024 * 1024
CHUNK = 1024 * 1024
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")


class BackupError(Exception):
    pass


def ensure_private_file(path: Path) -> None:
    mode = path.lstat().st_mode
    if not stat.S_ISREG(mode) or stat.S_IMODE(mode) != 0o600:
        raise BackupError(f"{path.name}: expected a regular file with mode 0600")


def verify(directory: Path) -> None:
    directory = directory.resolve(strict=True)
    if not directory.is_dir() or stat.S_IMODE(directory.stat().st_mode) != 0o700:
        raise BackupError("backup directory must have mode 0700")
    manifest_path = directory / "manifest.json"
    ensure_private_file(manifest_path)
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    entries = manifest.get("partitions")
    if not isinstance(entries, list) or len(entries) != len(PARTITIONS):
        raise BackupError("manifest partition list is incomplete")
    if {entry.get("partition") for entry in entries} != set(PARTITIONS):
        raise BackupError("manifest partition names do not match the allowlist")
    for entry in entries:
        name = entry["partition"]
        if entry.get("file") != f"{name}.img" or entry.get("bytes") != EXPECTED_BYTES:
            raise BackupError(f"{name}: unexpected manifest filename or size")
        expected_hash = entry.get("sha256")
        if not isinstance(expected_hash, str) or not SHA256_RE.fullmatch(expected_hash):
            raise BackupError(f"{name}: malformed SHA-256 in manifest")
        file_path = directory / entry["file"]
        ensure_private_file(file_path)
        if file_path.stat().st_size != EXPECTED_BYTES:
            raise BackupError(f"{name}: image size mismatch")
        digest = hashlib.sha256()
        with file_path.open("rb") as image:
            while chunk := image.read(CHUNK):
                digest.update(chunk)
        if digest.hexdigest() != expected_hash:
            raise BackupError(f"{name}: SHA-256 mismatch")
    print("OK: 3 images, sizes, SHA-256 and private permissions verified")


def ssh_command(host: str, known_hosts: Path | None, remote_command: str) -> list[str]:
    command = ["ssh", "-T", "-o", "BatchMode=yes", "-o", "ConnectTimeout=8"]
    if known_hosts is not None:
        command.extend(["-o", f"UserKnownHostsFile={known_hosts}"])
    command.extend([host, remote_command])
    return command


def remote_text(host: str, known_hosts: Path | None, command: str) -> str:
    result = subprocess.run(
        ssh_command(host, known_hosts, command),
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        check=False,
        timeout=60,
    )
    if result.returncode:
        raise BackupError("SSH read-only metadata query failed")
    return result.stdout.decode("ascii", errors="strict").strip()


def refuse_git_tree(path: Path) -> None:
    for parent in (path, *path.parents):
        if (parent / ".git").exists():
            raise BackupError("backup output must be outside a Git worktree")


def capture(host: str, known_hosts: Path | None, output: Path) -> None:
    requested = output.expanduser().absolute()
    output = requested.parent.resolve(strict=True) / requested.name
    refuse_git_tree(output)
    if output.exists():
        raise BackupError("output already exists; capture never overwrites a backup")
    os.umask(0o077)
    output.mkdir(parents=True, mode=0o700)
    try:
        entries = []
        for name in PARTITIONS:
            source = f"/dev/block/by-name/{name}"
            # All remote commands are fixed, read-only, and restricted to allowlisted names.
            target = remote_text(host, known_hosts, f"test -b {source} && readlink -f {source}")
            if not re.fullmatch(r"/dev/(?:block/)?[A-Za-z0-9._-]+", target):
                raise BackupError(f"{name}: unexpected block-device mapping")
            # Linux sysfs block-device size is always expressed in 512-byte sectors.
            sectors_text = remote_text(
                host, known_hosts, f"cat /sys/class/block/{Path(target).name}/size"
            )
            if not sectors_text.isdecimal() or int(sectors_text) * 512 != EXPECTED_BYTES:
                raise BackupError(f"{name}: expected an 8 MiB partition")

            tmp_path = output / f".{name}.img.tmp"
            digest = hashlib.sha256()
            count = 0
            process = subprocess.Popen(
                ssh_command(host, known_hosts, f"cat {source}"),
                stdout=subprocess.PIPE,
                stderr=subprocess.DEVNULL,
            )
            try:
                with tmp_path.open("xb", buffering=0) as image:
                    os.chmod(tmp_path, 0o600)
                    assert process.stdout is not None
                    while chunk := process.stdout.read(CHUNK):
                        image.write(chunk)
                        digest.update(chunk)
                        count += len(chunk)
                        if count > EXPECTED_BYTES:
                            raise BackupError(f"{name}: stream exceeded expected size")
                    os.fsync(image.fileno())
                if process.wait(timeout=60) or count != EXPECTED_BYTES:
                    raise BackupError(f"{name}: incomplete read-only capture")
            finally:
                if process.poll() is None:
                    process.kill()
                    process.wait()
                if process.stdout is not None:
                    process.stdout.close()

            device_hash_line = remote_text(host, known_hosts, f"sha256sum {source}")
            device_hash = device_hash_line.split(maxsplit=1)[0]
            if not SHA256_RE.fullmatch(device_hash) or device_hash != digest.hexdigest():
                raise BackupError(f"{name}: device/local SHA-256 mismatch; retry later")
            os.replace(tmp_path, output / f"{name}.img")
            entries.append({
                "partition": name,
                "file": f"{name}.img",
                "bytes": count,
                "sha256": digest.hexdigest(),
                "device_sha256_match": True,
                "source_mapping": target,
            })

        manifest = {
            "device": "Xiaomi 14 Pro shennong",
            "created_at": dt.datetime.now(dt.timezone.utc).isoformat(),
            "source": "/dev/block/by-name/{modemst1,modemst2,fsg}",
            "read_only_capture": True,
            "partitions": entries,
        }
        manifest_path = output / "manifest.json"
        with manifest_path.open("x", encoding="utf-8") as file:
            os.chmod(manifest_path, 0o600)
            json.dump(manifest, file, ensure_ascii=False, indent=2)
            file.write("\n")
            file.flush()
            os.fsync(file.fileno())
        verify(output)
    except Exception:
        shutil.rmtree(output)
        raise


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subcommands = parser.add_subparsers(dest="action", required=True)
    verify_parser = subcommands.add_parser("verify", help="check an existing local backup")
    verify_parser.add_argument("directory", type=Path)
    capture_parser = subcommands.add_parser("capture", help="read only: copy three modem partitions over SSH")
    capture_parser.add_argument("--host", default="root@192.168.66.1")
    capture_parser.add_argument("--known-hosts", type=Path)
    capture_parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.action == "verify":
            verify(args.directory)
        else:
            capture(args.host, args.known_hosts, args.output)
        return 0
    except (BackupError, OSError, ValueError, json.JSONDecodeError, subprocess.TimeoutExpired) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
