import importlib.util
import sys
import tempfile
import types
import unittest
from pathlib import Path


FLASK = types.ModuleType("flask")
FLASK.jsonify = lambda value: value
FLASK.redirect = lambda value: value
FLASK.render_template = lambda *args, **kwargs: (args, kwargs)
FLASK.session = {}
sys.modules.setdefault("flask", FLASK)

SOURCE = Path(__file__).resolve().parents[1] / "files" / "pcat_thermal.py"
SPEC = importlib.util.spec_from_file_location("pcat_thermal_tested", SOURCE)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def write(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(str(value), encoding="utf-8")


class DummyModem:
    def __init__(self):
        import threading
        self.mutex = threading.Lock()
        self.basic = {
            "fm350_temperature_sensors": {"soc_max": 48.3, "md_5g": 47.1}
        }


class DummyApp:
    modem_client = DummyModem()
    socket_client = None


class ThermalSamplerTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.sys = self.root / "sys"
        self.proc = self.root / "proc"
        write(self.proc / "stat", "cpu 100 0 100 800\ncpu0 50 0 50 400\n")
        write(self.proc / "uptime", "100.0 50.0")

        zone = self.sys / "class/thermal/thermal_zone0"
        write(zone / "type", "package-thermal")
        write(zone / "temp", "45000")
        write(zone / "trip_point_0_temp", "85000")
        write(zone / "trip_point_0_type", "passive")
        write(zone / "trip_point_1_temp", "115000")
        write(zone / "trip_point_1_type", "critical")

        duplicate = self.sys / "class/hwmon/hwmon0"
        write(duplicate / "name", "package_thermal")
        write(duplicate / "temp1_input", "45000")
        wifi = self.sys / "class/hwmon/hwmon1"
        write(wifi / "name", "mt7925_phy0")
        write(wifi / "temp1_input", "42000")
        fan = self.sys / "class/hwmon/hwmon2"
        write(fan / "name", "pcat_pm_hwmon_speed_fan")
        write(fan / "fan1_input", "900")

        battery = self.sys / "class/power_supply/battery"
        write(battery / "voltage_now", "8335000")
        write(battery / "current_now", "2000000")
        write(battery / "power_now", "16670000")
        write(battery / "capacity", "80")
        write(battery / "status", "Discharging")
        charger = self.sys / "class/power_supply/charger"
        write(charger / "online", "0")
        write(charger / "voltage_now", "0")

        policy = self.sys / "devices/system/cpu/cpufreq/policy0"
        write(policy / "related_cpus", "0")
        write(policy / "scaling_cur_freq", "1416000")
        write(policy / "scaling_min_freq", "408000")
        write(policy / "scaling_max_freq", "2016000")
        write(policy / "scaling_governor", "schedutil")

    def tearDown(self):
        self.temporary.cleanup()

    def test_collects_and_classifies_real_sources(self):
        sampler = MODULE.ThermalSampler(DummyApp(), self.sys, self.proc)
        write(self.proc / "stat", "cpu 200 0 200 1000\ncpu0 100 0 100 500\n")
        result = sampler.read()

        sensors = result["temperatures"]
        self.assertEqual(4, len(sensors))
        self.assertEqual(
            {"processor", "network", "modem"},
            {item["category"] for item in sensors},
        )
        self.assertEqual(1, sum(item["label"] == "主控封装" for item in sensors))
        self.assertEqual(48.3, result["temperature_summary"]["highest_c"])
        self.assertEqual(50.0, result["cpu"]["usage"])
        self.assertEqual(100.0, result["uptime_seconds"])
        self.assertEqual(1416.0, result["cpu"]["policies"][0]["current_mhz"])
        self.assertEqual(8.335, result["power"]["battery_voltage_v"])
        self.assertEqual(16.67, result["power"]["battery_power_w"])
        self.assertEqual(900, result["fans"][0]["rpm"])

    def test_temperature_scaling_and_validation(self):
        self.assertEqual(42.5, MODULE._temperature(42500))
        self.assertEqual(42.5, MODULE._temperature(42.5))
        self.assertIsNone(MODULE._temperature(999000))
        self.assertEqual("warning", MODULE._sensor_state(85, 80, 100))
        self.assertEqual("critical", MODULE._sensor_state(101, 80, 100))


if __name__ == "__main__":
    unittest.main()
