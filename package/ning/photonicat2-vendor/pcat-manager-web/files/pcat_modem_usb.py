#!/usr/bin/env python3
"""FM350 USB discovery and one-shot boot recovery helpers."""

import argparse
import fcntl
import glob
import os
import subprocess
import time


ADB = "/usr/bin/adb"
USBDEVFS_RESET = 0x5514
FM350_VENDOR = "0e8d"
FM350_PRODUCT = "7127"
PRIMARY_AT_INTERFACE = 0x06


def _read(path):
    try:
        with open(path, "r", encoding="ascii") as handle:
            return handle.read().strip()
    except OSError:
        return ""


def _usb_parent(path):
    current = os.path.realpath(path)
    while current and current != "/":
        if os.path.isfile(os.path.join(current, "idVendor")):
            return current
        current = os.path.dirname(current)
    return ""


def resolve_primary_at_port(default="/dev/ttyUSB3"):
    """Return the FM350 application AT tty by USB interface number.

    ttyUSB ordinals can change after a USB reset while an old file descriptor
    is still being released.  Interface 06 is stable in USB mode 41.
    """
    candidates = []
    for tty_path in glob.glob("/sys/class/tty/ttyUSB*"):
        interface_path = os.path.realpath(os.path.join(tty_path, "device", ".."))
        try:
            interface_number = int(
                _read(os.path.join(interface_path, "bInterfaceNumber")), 16)
        except ValueError:
            continue
        if interface_number != PRIMARY_AT_INTERFACE:
            continue
        usb_path = _usb_parent(interface_path)
        if _read(os.path.join(usb_path, "idVendor")).lower() != FM350_VENDOR:
            continue
        name = os.path.basename(tty_path)
        device = os.path.join("/dev", name)
        if os.path.exists(device):
            candidates.append(device)
    if candidates:
        return sorted(candidates)[0]
    return default


def _fm350_usb_device():
    for vendor_path in glob.glob("/sys/bus/usb/devices/*/idVendor"):
        usb_path = os.path.dirname(vendor_path)
        if ":" in os.path.basename(usb_path):
            continue
        if _read(vendor_path).lower() != FM350_VENDOR:
            continue
        product_id = _read(os.path.join(usb_path, "idProduct")).lower()
        product_name = _read(os.path.join(usb_path, "product")).upper()
        if product_id != FM350_PRODUCT and "FM350" not in product_name:
            continue
        has_adb = False
        for interface in glob.glob(usb_path + ":*"):
            has_adb = (
                _read(os.path.join(interface, "bInterfaceClass")).lower() == "ff"
                and _read(os.path.join(interface, "bInterfaceSubClass")).lower() == "42"
                and _read(os.path.join(interface, "bInterfaceProtocol")).lower() == "01"
            )
            if has_adb:
                break
        if not has_adb:
            continue
        bus = _read(os.path.join(usb_path, "busnum"))
        device = _read(os.path.join(usb_path, "devnum"))
        if bus.isdigit() and device.isdigit():
            return "/dev/bus/usb/{:03d}/{:03d}".format(int(bus), int(device))
    return ""


def adb_runtime_available(timeout=4):
    try:
        result = subprocess.run(
            [ADB, "shell", "head -n 1 /proc/stat"],
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
            timeout=timeout,
            check=False,
        )
    except (OSError, subprocess.SubprocessError):
        return False
    return result.returncode == 0 and result.stdout.lstrip().startswith("cpu ")


def _kill_adb_server():
    try:
        subprocess.run(
            [ADB, "kill-server"], stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL, timeout=5, check=False,
        )
    except (OSError, subprocess.SubprocessError):
        pass


def reset_fm350_usb():
    device = _fm350_usb_device()
    if not device:
        raise RuntimeError("FM350 USB device was not found")
    _kill_adb_server()
    descriptor = os.open(device, os.O_WRONLY)
    try:
        fcntl.ioctl(descriptor, USBDEVFS_RESET)
    finally:
        os.close(descriptor)


def _run_service(name, action):
    try:
        subprocess.run(
            ["/etc/init.d/" + name, action], timeout=20, check=False,
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        )
    except (OSError, subprocess.SubprocessError):
        pass


def boot_guard(wait_seconds=90, force_reset=False):
    deadline = time.monotonic() + max(10, wait_seconds)
    while time.monotonic() < deadline and not _fm350_usb_device():
        time.sleep(2)
    if not _fm350_usb_device():
        print("pcat-modem-health: FM350 did not enumerate; leaving dialer in control", flush=True)
        return 1

    if not force_reset:
        for attempt in range(6):
            if adb_runtime_available():
                print("pcat-modem-health: FM350 ADB runtime is ready", flush=True)
                return 0
            if attempt in (1, 3):
                _kill_adb_server()
            time.sleep(3)

    print("pcat-modem-health: FM350 ADB stayed offline; resetting its USB device", flush=True)
    try:
        reset_fm350_usb()
    except (OSError, RuntimeError) as error:
        print("pcat-modem-health: USB reset failed: {}".format(error), flush=True)
        return 1

    # The dialer can retain a deleted tty file descriptor across USB reset.
    # Wait for interface 06 to return, then restart both AT consumers so they
    # resolve the new ttyUSB ordinal from sysfs.
    at_deadline = time.monotonic() + 30
    while time.monotonic() < at_deadline:
        if os.path.exists(resolve_primary_at_port(default="")):
            break
        time.sleep(1)
    _run_service("pcat-manager", "restart")
    _run_service("pcat-manager-web", "restart")

    adb_deadline = time.monotonic() + 30
    while time.monotonic() < adb_deadline:
        if adb_runtime_available():
            print("pcat-modem-health: FM350 ADB recovered after USB reset", flush=True)
            return 0
        time.sleep(2)
    print("pcat-modem-health: FM350 ADB did not recover after one USB reset", flush=True)
    return 1


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--boot-guard", action="store_true")
    parser.add_argument("--force-reset", action="store_true")
    args = parser.parse_args()
    if args.boot_guard or args.force_reset:
        raise SystemExit(boot_guard(force_reset=args.force_reset))
    print(resolve_primary_at_port())


if __name__ == "__main__":
    main()
