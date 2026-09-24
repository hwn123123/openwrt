#!/usr/bin/env python3
"""Tests for demand-driven FM350 runtime collection."""

import importlib.util
from pathlib import Path
import sys
import unittest
from unittest import mock


SOURCE = Path(sys.argv.pop(1)).resolve()
SPEC = importlib.util.spec_from_file_location("pcat_dashboard_tested", SOURCE)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class ModemRuntimeSamplerTest(unittest.TestCase):
    def test_adb_failure_only_returns_empty_values(self):
        failed = mock.Mock(returncode=1, stdout="", stderr="")
        sampler = MODULE.ModemRuntimeSampler()

        with mock.patch.object(MODULE.subprocess, "run", return_value=failed), \
                mock.patch.object(MODULE.subprocess, "Popen") as spawn:
            results = [sampler.read() for _ in range(3)]

        for result in results:
            self.assertFalse(result["available"])
            self.assertFalse(result["recovering"])
            self.assertIsNone(result["cpu_usage"])
            self.assertIsNone(result["memory_usage"])
            self.assertIsNone(result["temperature"])
        spawn.assert_not_called()

    def test_adb_success_returns_runtime_values(self):
        outputs = [
            mock.Mock(
                returncode=0,
                stdout=(
                    "cpu 100 0 100 800\n__PCAT_MODEM_MEMORY__\n"
                    "MemTotal: 1000 kB\nMemAvailable: 400 kB\n"
                    "__PCAT_MODEM_THERMAL__\nmd_5g|42000\n"),
                stderr="",
            ),
            mock.Mock(
                returncode=0,
                stdout=(
                    "cpu 130 0 120 850\n__PCAT_MODEM_MEMORY__\n"
                    "MemTotal: 1000 kB\nMemAvailable: 400 kB\n"
                    "__PCAT_MODEM_THERMAL__\nmd_5g|43000\n"),
                stderr="",
            ),
        ]
        sampler = MODULE.ModemRuntimeSampler()

        with mock.patch.object(MODULE.subprocess, "run", side_effect=outputs):
            sampler.read()
            result = sampler.read()

        self.assertTrue(result["available"])
        self.assertEqual(result["cpu_usage"], 50.0)
        self.assertEqual(result["memory_usage"], 60.0)
        self.assertEqual(result["temperature"], 43.0)


if __name__ == "__main__":
    unittest.main(verbosity=2)
