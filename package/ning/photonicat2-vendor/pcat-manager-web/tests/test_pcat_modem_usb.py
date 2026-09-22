#!/usr/bin/env python3
"""Deterministic tests for the FM350 boot guard state machine."""

import importlib.util
from pathlib import Path
import sys
import unittest


SOURCE = Path(sys.argv.pop(1)).resolve()
SPEC = importlib.util.spec_from_file_location("pcat_modem_usb_tested", SOURCE)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class FakeClock:
    def __init__(self):
        self.now = 0

    def monotonic(self):
        return self.now

    def sleep(self, seconds):
        self.now += seconds


class BootGuardTest(unittest.TestCase):
    def setUp(self):
        self.clock = FakeClock()
        MODULE.time.monotonic = self.clock.monotonic
        MODULE.time.sleep = self.clock.sleep
        MODULE._kill_adb_server = lambda: None
        MODULE._onboard_hub_present = lambda: True

    def test_missing_onboard_hub_is_reset_before_modem_discovery(self):
        hub_resets = []
        MODULE._onboard_hub_present = lambda: bool(hub_resets)
        MODULE._reset_onboard_hub = lambda: hub_resets.append(True)
        MODULE._fm350_usb_device = lambda: (
            "/dev/bus/usb/002/003" if self.clock.now >= 4 else "")
        MODULE.adb_runtime_available = lambda timeout=4: self.clock.now >= 4

        self.assertEqual(MODULE.boot_guard(wait_seconds=40), 0)
        self.assertEqual(hub_resets, [True])

    def test_missing_hub_retries_until_platform_driver_is_bound(self):
        hub_reset_calls = []
        hub_recovered = []
        MODULE._onboard_hub_present = lambda: bool(hub_recovered)

        def reset_hub():
            hub_reset_calls.append(self.clock.now)
            if len(hub_reset_calls) < 3:
                raise RuntimeError("onboard USB hub driver is not bound")
            hub_recovered.append(True)

        MODULE._reset_onboard_hub = reset_hub
        MODULE._fm350_usb_device = lambda: (
            "/dev/bus/usb/002/003" if hub_recovered else "")
        MODULE.adb_runtime_available = lambda timeout=4: True

        self.assertEqual(MODULE.boot_guard(wait_seconds=40), 0)
        self.assertEqual(hub_reset_calls, [0, 2, 4])

    def test_usb_disappearance_returns_to_discovery(self):
        resets = []
        adb_checks = []

        def device():
            now = self.clock.now
            if now < 4 or 9 <= now < 15:
                return ""
            return "/dev/bus/usb/002/003"

        MODULE._fm350_usb_device = device
        def adb_available(timeout=4):
            adb_checks.append(self.clock.now)
            return self.clock.now >= 15

        MODULE.adb_runtime_available = adb_available
        MODULE.reset_fm350_usb = lambda: resets.append(True)

        self.assertEqual(MODULE.boot_guard(wait_seconds=40), 0)
        self.assertEqual(resets, [])
        self.assertGreaterEqual(min(adb_checks), 20)

    def test_one_reset_restarts_at_consumers_after_port_returns(self):
        reset_done = []
        services = []
        original_exists = MODULE.os.path.exists
        MODULE._fm350_usb_device = lambda: "/dev/bus/usb/002/003"
        MODULE.adb_runtime_available = lambda timeout=4: bool(reset_done)
        MODULE.reset_fm350_usb = lambda: reset_done.append(True)
        MODULE.resolve_primary_at_port = lambda default="": (
            "/dev/ttyUSB3" if self.clock.now >= 19 else default)
        MODULE.os.path.exists = lambda path: path == "/dev/ttyUSB3"
        MODULE._run_service = lambda name, action: services.append((name, action))

        try:
            self.assertEqual(MODULE.boot_guard(wait_seconds=60), 0)
            self.assertEqual(len(reset_done), 1)
            self.assertGreaterEqual(self.clock.now, 19)
            self.assertEqual(services, [
                ("pcat-manager", "restart"),
                ("pcat-manager-web", "restart"),
            ])
        finally:
            MODULE.os.path.exists = original_exists


if __name__ == "__main__":
    unittest.main(verbosity=2)
