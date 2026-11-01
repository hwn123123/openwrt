#!/usr/bin/env python3
"""Patch PixelOS recovery into the NWRT/OpenWrt maintenance recovery v2.8."""

from __future__ import annotations

import argparse
from pathlib import Path


def replace_once(path: Path, old: str, new: str) -> None:
    data = path.read_text(encoding="utf-8")
    if data.count(old) != 1:
        raise SystemExit(f"unexpected occurrence count in {path}: {old!r}")
    path.write_text(data.replace(old, new), encoding="utf-8")


def patch_props(path: Path) -> None:
    replacements = (
        ("ro.debuggable=0", "ro.debuggable=1"),
        ("ro.force.debuggable=0", "ro.force.debuggable=1"),
    )
    data = path.read_text(encoding="utf-8")
    if data.count("ro.secure=1") != 1 or data.count("ro.adb.secure=1") != 2:
        raise SystemExit(f"refusing insecure recovery ADB properties in {path}")
    if "ro.secure=0" in data or "ro.adb.secure=0" in data:
        raise SystemExit(f"unexpected unauthenticated recovery ADB properties in {path}")
    old_counts = tuple(data.count(old) for old, _ in replacements)
    new_counts = tuple(data.count(new) for _, new in replacements)
    expected = (1, 1)
    marker = "ro.recovery.multiboot=1"
    if old_counts == (0, 0) and new_counts == expected and marker in data:
        return
    if old_counts != expected or new_counts != (0, 0) or marker in data:
        raise SystemExit(
            f"unexpected recovery property state in {path}: "
            f"old={old_counts}, new={new_counts}"
        )
    for old, new in replacements:
        data = data.replace(old, new)
    data += "\nro.recovery.multiboot=1\nro.recovery.linux_root=/dev/block/by-name/linux_root\n"
    path.write_text(data, encoding="utf-8")


def patch_branding(path: Path) -> None:
    data = path.read_bytes()
    replacements = (
        (b"NWRT Recovery / v0.9", b"NWRT Recovery / v2.8"),
        (b"/tmp/xm14-rec.log", b"/multiboot-ui.log"),
        (b"NWRT / OpenWrt", b"OpenWrt       "),
        (b" 14 Pro", b"  nwrt "),
    )
    for old, new in replacements:
        if len(old) != len(new):
            raise SystemExit(f"branding replacement length mismatch: {old!r}")
        data = data.replace(old, new)
    required = (
        b"NWRT Recovery / v2.8",
        b"OpenWrt       ",
        b"  nwrt ",
    )
    for marker in required:
        if marker not in data:
            raise SystemExit(f"required branding is missing: {marker!r}")
    if b"Linux MultiBoot" in data or b"Linux / Kali" in data or b"Linux/Kali" in data:
        raise SystemExit("obsolete Linux/Kali branding remains in selector binary")
    if b" 14 Pro" in data:
        raise SystemExit("obsolete 14 Pro label remains in selector binary")
    # The selector logs a confirmed power action, then its direct reboot(2)
    # syscall fails under recovery policy.  The init bridge handles the action
    # from that log.  Return to the event loop without showing a false failure.
    offset = 0x11D0
    old = bytes.fromhex("20 00 80 52")  # mov w0, #1 (set error state)
    new = bytes.fromhex("09 ff ff 17")  # b 0x400df4 (event loop)
    if data[offset:offset + 4] != old:
        raise SystemExit("unexpected selector power-action code")
    data = data[:offset] + new + data[offset + 4:]
    path.write_bytes(data)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("base_root", type=Path)
    parser.add_argument("selector_binary", type=Path)
    args = parser.parse_args()

    base = args.base_root.resolve()
    init_rc = base / "system/etc/init/hw/init.rc"
    replace_once(init_rc, "service recovery /system/bin/recovery", "service recovery /nwrt/bin/recovery")
    replace_once(
        init_rc,
        "service recovery /nwrt/bin/recovery\n"
        "    socket recovery stream 422 system system\n"
        "    seclabel u:r:recovery:s0\n"
        "    user root\n",
        "service recovery /nwrt/bin/recovery\n"
        "    socket recovery stream 422 system system\n"
        "    seclabel u:r:recovery:s0\n"
        "    user root\n"
        "    capabilities SYS_BOOT\n",
    )
    replace_once(
        init_rc,
        "service fastbootd /system/bin/fastbootd\n"
        "    disabled\n"
        "    group system\n"
        "    seclabel u:r:fastbootd:s0\n"
        "    user root\n",
        "service fastbootd /system/bin/fastbootd\n"
        "    disabled\n"
        "    group system\n"
        "    seclabel u:r:fastbootd:s0\n"
        "    user root\n\n"
        "service multiboot-diagnostics /system/bin/sh /nwrt/bin/multiboot-diagnostics\n"
        "    class default\n"
        "    user root\n"
        "    group root shell log\n"
        "    seclabel u:r:recovery:s0\n"
        "    oneshot\n\n"
        "service multiboot-action-bridge /system/bin/sh /nwrt/bin/multiboot-action-bridge\n"
        "    class default\n"
        "    user root\n"
        "    group root shell log\n"
        "    seclabel u:r:recovery:s0\n\n"
        "service nwrt-rootd /system/bin/sh /nwrt/bin/nwrt-rootd\n"
        "    class default\n"
        "    user root\n"
        "    group root shell log input\n"
        "    seclabel u:r:recovery:s0\n",
    )
    replace_once(
        init_rc,
        "on boot\n"
        "    ifup lo\n"
        "    hostname localhost\n"
        "    domainname localdomain\n\n"
        "    class_start default\n"
        "    class_start hal\n",
        "on boot\n"
        "    ifup lo\n"
        "    hostname localhost\n"
        "    domainname localdomain\n\n"
        "    # The Qualcomm recovery Boot HAL is class early_hal.\n"
        "    class_start early_hal\n"
        "    class_start default\n"
        "    class_start hal\n"
        "    # Keep authenticated shell ADB available without sideload mode.\n"
        "    setprop sys.usb.config adb\n",
    )

    patch_props(base / "prop.default")
    patch_props(base / "default.prop")

    contexts = base / "plat_file_contexts"
    context_line = "/nwrt(/.*)?          u:object_r:system_file:s0"
    context_data = contexts.read_text(encoding="utf-8")
    if context_line not in context_data:
        contexts.write_text(context_data.rstrip("\n") + "\n" + context_line + "\n", encoding="utf-8")

    patch_branding(args.selector_binary)


if __name__ == "__main__":
    main()
