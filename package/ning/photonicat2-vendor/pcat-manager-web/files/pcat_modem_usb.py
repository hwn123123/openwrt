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
ONBOARD_HUB_DRIVER = "/sys/bus/platform/drivers/onboard-usb-dev"
ONBOARD_HUB_DEVICE = "23000000.usb:hub@1"
ONBOARD_HUB_VENDOR = "05e3"
ONBOARD_HUB_PRODUCT = "0610"
FM350_USB_SETTLE_SECONDS = 5
DIAL_STATE_PATH = "/tmp/pcat-fm350-dial.state"
DIAL_WEB_STATES = frozenset(("online", "failed"))
HUB_RESET_RETRY_SECONDS = 15
HUB_RESET_MAX_ATTEMPTS = 3


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


def fm350_present():
    """Return True when the final FM350 USB composition is enumerated."""
    return bool(_fm350_usb_device())


def dial_state():
    """Read the data dialer's atomic lifecycle record."""
    try:
        import json
        with open(DIAL_STATE_PATH, "r", encoding="ascii") as handle:
            value = json.load(handle)
    except (OSError, ValueError, TypeError):
        return {}
    return value if isinstance(value, dict) else {}


def web_at_allowed():
    """Keep Web, telemetry, SMS and eSIM AT work behind data dialing."""
    if not fm350_present():
        return True
    return dial_state().get("state") in DIAL_WEB_STATES


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
        bus = _read(os.path.join(usb_path, "busnum"))
        device = _read(os.path.join(usb_path, "devnum"))
        if bus.isdigit() and device.isdigit():
            return "/dev/bus/usb/{:03d}/{:03d}".format(int(bus), int(device))
    return ""


def _onboard_hub_present():
    for vendor_path in glob.glob("/sys/bus/usb/devices/*/idVendor"):
        usb_path = os.path.dirname(vendor_path)
        if _read(vendor_path).lower() != ONBOARD_HUB_VENDOR:
            continue
        if _read(os.path.join(usb_path, "idProduct")).lower() == ONBOARD_HUB_PRODUCT:
            return True
    return False


def _reset_onboard_hub():
    """Re-run the kernel hub power/reset sequence after a failed warm boot."""
    device_path = os.path.join(ONBOARD_HUB_DRIVER, ONBOARD_HUB_DEVICE)
    if not os.path.islink(device_path):
        raise RuntimeError("onboard USB hub driver is not bound")
    with open(os.path.join(ONBOARD_HUB_DRIVER, "unbind"), "w", encoding="ascii") as handle:
        handle.write(ONBOARD_HUB_DEVICE)
    time.sleep(2)
    with open(os.path.join(ONBOARD_HUB_DRIVER, "bind"), "w", encoding="ascii") as handle:
        handle.write(ONBOARD_HUB_DEVICE)


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


def boot_guard(wait_seconds=180, force_reset=False):
    """Recover a missing onboard hub, then check auxiliary ADB after dialing.

    Automatic boot checks never reset an enumerated FM350: a whole-device USB
    reset tears down RNDIS, AT and SIM state together.  A deliberate
    --force-reset remains available for manual recovery.
    """
    deadline = time.monotonic() + max(30, wait_seconds)
    offline_checks = 0
    reset_attempted = False
    services_need_restart = False
    hub_reset_attempts = 0
    next_hub_reset_at = 0
    usb_present_since = None

    while time.monotonic() < deadline:
        if not _fm350_usb_device():
            usb_present_since = None
            now = time.monotonic()
            if (
                    hub_reset_attempts < HUB_RESET_MAX_ATTEMPTS
                    and now >= next_hub_reset_at
                    and not _onboard_hub_present()):
                print(
                    "pcat-modem-health: onboard USB hub is absent; resetting it",
                    flush=True,
                )
                try:
                    _reset_onboard_hub()
                except (OSError, RuntimeError) as error:
                    # During a first cold boot this guard can run before the
                    # platform driver has bound the hub node.  Do not consume
                    # the only recovery attempt; follow the driver until it is
                    # ready and try again.
                    print(
                        "pcat-modem-health: onboard USB hub reset failed: {}".format(error),
                        flush=True,
                    )
                    next_hub_reset_at = time.monotonic() + 2
                else:
                    hub_reset_attempts += 1
                    # Enumeration takes several seconds.  If a reset completed
                    # but the hub still did not return, allow two later tries
                    # rather than leaving all onboard USB devices absent.
                    next_hub_reset_at = (
                        time.monotonic() + HUB_RESET_RETRY_SECONDS)
            offline_checks = 0
            time.sleep(2)
            continue

        now = time.monotonic()
        if usb_present_since is None:
            usb_present_since = now
        if now - usb_present_since < FM350_USB_SETTLE_SECONDS:
            # Some cold boots expose 0e8d:7127 for about four seconds before
            # returning through the preloader. Do not probe ADB or reset USB
            # until the final runtime has remained continuously present.
            offline_checks = 0
            time.sleep(1)
            continue

        # Data service has priority over ADB and all Web-side AT traffic.
        # During a normal boot, do not even probe ADB until the dialer has
        # reached either a usable session or a final failure.
        if not force_reset and not web_at_allowed():
            time.sleep(2)
            continue

        # A successful manual USB reset invalidates the AT file descriptors
        # held by both consumers. Restart them once interface 06 has returned.
        if services_need_restart and os.path.exists(
                resolve_primary_at_port(default="")):
            _run_service("pcat-manager", "restart")
            _run_service("pcat-manager-web", "restart")
            services_need_restart = False

        if adb_runtime_available():
            # ADB can answer slightly before the option driver has created the
            # primary AT tty.  Do not leave the dialer holding a deleted file
            # descriptor after our reset; wait for the promised restart first.
            if services_need_restart:
                time.sleep(1)
                continue
            message = (
                "pcat-modem-health: FM350 ADB recovered after USB reset"
                if reset_attempted else
                "pcat-modem-health: FM350 ADB runtime is ready")
            print(message, flush=True)
            return 0

        offline_checks += 1
        if offline_checks in (2, 4):
            _kill_adb_server()

        if not force_reset and offline_checks >= 6:
            print(
                "pcat-modem-health: FM350 ADB is offline; leaving the active "
                "USB data and AT session untouched",
                flush=True,
            )
            return 0

        if not force_reset or reset_attempted:
            time.sleep(3)
            continue

        print(
            "pcat-modem-health: manual FM350 USB reset requested",
            flush=True,
        )
        try:
            reset_fm350_usb()
        except (OSError, RuntimeError) as error:
            # The modem can vanish between discovery and open().  Return to the
            # discovery loop instead of abandoning the remainder of this boot.
            print(
                "pcat-modem-health: USB reset deferred; waiting for FM350 to "
                "reappear: {}".format(error),
                flush=True,
            )
            offline_checks = 0
            time.sleep(2)
            continue

        reset_attempted = True
        services_need_restart = True
        offline_checks = 0
        time.sleep(2)

    if services_need_restart and os.path.exists(
            resolve_primary_at_port(default="")):
        _run_service("pcat-manager", "restart")
        _run_service("pcat-manager-web", "restart")
    print(
        "pcat-modem-health: boot guard ended without touching the FM350 data session",
        flush=True,
    )
    return 0


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
