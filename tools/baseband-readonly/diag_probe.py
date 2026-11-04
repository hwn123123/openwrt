#!/usr/bin/env python3
"""Probe this phone's Qualcomm DIAG NV read path; never send a write request.

Requires PyUSB, pyserial, colorama and a local checkout of bkerler/edl.
The USB configuration is temporary and is restored on the phone by a timer.
"""

import argparse
import contextlib
import io
import logging
from pathlib import Path
import shlex
import subprocess
import sys
import time


DIAG_VID = 0x05C6
DIAG_PID = 0x901D


def adb(*args: str, timeout: int = 15, check: bool = True) -> subprocess.CompletedProcess:
    return subprocess.run(
        ["adb", *args], text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        timeout=timeout, check=check,
    )


def root(command: str, *, check: bool = True) -> subprocess.CompletedProcess:
    # adb shell joins arguments into one remote command. Quote the complete
    # su payload so Android's shell cannot split it at the first space.
    return adb("shell", "su -c " + shlex.quote(command), check=check)


def device_ready() -> bool:
    result = adb("get-state", check=False)
    return result.returncode == 0 and result.stdout.strip() == "device"


def wait_for_phone(seconds: int) -> bool:
    until = time.monotonic() + seconds
    while time.monotonic() < until:
        if device_ready():
            return True
        time.sleep(1)
    return False


def start_background(command: str) -> int:
    result = root(command + " </dev/null >/dev/null 2>&1 & echo $!")
    return int(result.stdout.strip().splitlines()[-1])


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--edl", type=Path, required=True, help="local bkerler/edl checkout")
    parser.add_argument("--libusb", type=Path, required=True, help="local libusb dylib/so")
    args = parser.parse_args()

    if not (args.edl / "edlclient/Tools/qc_diag.py").is_file():
        parser.error("--edl does not contain edlclient/Tools/qc_diag.py")
    if not args.libusb.is_file():
        parser.error("--libusb library does not exist")
    if not device_ready():
        parser.error("ADB device is not ready")
    if adb("shell", "getprop", "ro.boot.slot_suffix").stdout.strip() != "_a":
        parser.error("this probe was reviewed for Android slot A only")
    if root("id -u").stdout.strip() != "0":
        parser.error("root is unavailable")
    if adb("shell", "getprop", "sys.usb.config").stdout.strip() != "adb":
        parser.error("USB must initially be in adb mode")
    if adb("shell", "getprop", "sys.usb.configfs").stdout.strip() != "2":
        parser.error("USB configfs mode differs from the reviewed phone")

    import usb.backend.libusb1 as libusb1
    import usb.core

    original_get_backend = libusb1.get_backend
    libusb1.get_backend = lambda *a, **k: original_get_backend(
        find_library=lambda _: str(args.libusb.resolve())
    )
    backend = libusb1.get_backend()
    if backend is None:
        parser.error("libusb could not be loaded")

    sys.path.insert(0, str(args.edl.resolve()))
    with contextlib.redirect_stdout(io.StringIO()):
        from edlclient.Tools.qc_diag import qcdiag, hdlc

    # qcdiag.connect() calls receive_reply(timeout=0) as a drain. In PyUSB,
    # zero means an unlimited wait, so skip that drain instead of waiting for
    # the USB disconnect and mistaking it for a successful connection.
    original_receive = hdlc.receive_reply
    def bounded_receive(self, timeout=None):
        if timeout == 0:
            return b""
        return original_receive(self, timeout)
    hdlc.receive_reply = bounded_receive

    # Both helpers detach from ADB. The second one restores USB even if ADB drops.
    restore_pid = start_background(
        "setsid sh -c 'sleep 45; setprop sys.usb.config adb; "
        "setprop sys.usb.configfs 2; setprop sys.usb.config adb'"
    )
    router_pid = start_background(
        "setsid sh -c 'timeout 40 /vendor/bin/diag-router -d 7'"
    )
    time.sleep(2)
    router_started = root("pidof diag-router", check=False).returncode == 0
    print("ROUTER_STARTED=" + ("yes" if router_started else "no"))

    connected = False
    diag = None
    try:
        # The first property may momentarily disconnect ADB, but the shell
        # continues to set the DIAG + ADB composition on the device.
        root("setprop sys.usb.configfs 1; setprop sys.usb.config diag,adb", check=False)
        until = time.monotonic() + 25
        candidate = None
        while time.monotonic() < until:
            candidate = usb.core.find(idVendor=DIAG_VID, idProduct=DIAG_PID, backend=backend)
            if candidate is not None:
                break
            time.sleep(1)
        if candidate is None:
            print("DIAG_USB=unavailable")
            return 2
        interfaces = [i.bInterfaceNumber for c in candidate for i in c]
        print("DIAG_USB=present")
        print("INTERFACE_COUNT=" + str(len(interfaces)))
        if device_ready():
            router_alive = root("pidof diag-router", check=False).returncode == 0
            endpoint_list = root("ls /dev/ffs-diag/ep* 2>/dev/null", check=False)
            print("ROUTER_ACTIVE=" + ("yes" if router_alive else "no"))
            print("DIAG_FFS_ENDPOINT_COUNT=" + str(len(endpoint_list.stdout.splitlines())))

        diag = qcdiag(loglevel=logging.ERROR, portconfig=[[DIAG_VID, DIAG_PID, 0]])
        connected = diag.connect()
        print("DIAG_USB_CLAIMED=" + ("yes" if connected else "no"))
        if not connected:
            return 3

        version = diag.send(b"\x00")  # DIAG_VERNO_F, read-only
        print("DIAG_VERSION_RESPONSE=" + ("received" if version else "empty"))
        if version:
            print("DIAG_VERSION_COMMAND=" + str(version[0]))
        ok, response = diag.read_nvitem(550)  # DIAG_NV_READ_F only
        print("NV550_RESPONSE=" + ("received" if ok else "error"))
        if ok:
            print("NV550_STATUS=" + str(response.status))
            print("NV550_DATA_PRESENT=" + ("yes" if bool(response.data) else "no"))
        elif isinstance(response, str):
            print("NV550_ERROR=" + response)
        return 0 if ok and response.status == 0 else 4
    finally:
        if connected and diag is not None:
            try:
                diag.disconnect()
            except Exception:
                pass
        if wait_for_phone(8):
            root("setprop sys.usb.config adb; setprop sys.usb.configfs 2; "
                 "setprop sys.usb.config adb", check=False)
        if not wait_for_phone(95):
            print("ADB_RESTORED=no; check USB connection", file=sys.stderr)
        else:
            time.sleep(1)
            restored = (
                adb("shell", "getprop", "sys.usb.config").stdout.strip() == "adb"
                and adb("shell", "getprop", "sys.usb.configfs").stdout.strip() == "2"
            )
            print("ADB_RESTORED=" + ("yes" if restored else "no"))
            if restored:
                root(f"kill {router_pid} {restore_pid} 2>/dev/null || true", check=False)


if __name__ == "__main__":
    raise SystemExit(main())
