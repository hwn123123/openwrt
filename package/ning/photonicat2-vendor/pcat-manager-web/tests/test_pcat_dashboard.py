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
    def test_recovery_starts_only_after_two_visible_requests_fail(self):
        failed = mock.Mock(returncode=1, stdout="", stderr="")
        recovery = mock.Mock()
        recovery.poll.return_value = None
        sampler = MODULE.ModemRuntimeSampler()

        with mock.patch.object(MODULE.subprocess, "run", return_value=failed), \
                mock.patch.object(
                    MODULE.subprocess, "Popen", return_value=recovery) as spawn:
            first = sampler.read()
            second = sampler.read()
            third = sampler.read()

        self.assertFalse(first["available"])
        self.assertFalse(first["recovering"])
        self.assertTrue(second["recovering"])
        self.assertTrue(third["recovering"])
        spawn.assert_called_once()


if __name__ == "__main__":
    unittest.main(verbosity=2)
