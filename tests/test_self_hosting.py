"""Check that the NAS service example respects Society's storage boundary."""
import configparser
import pathlib
import shlex
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class SelfHostingTests(unittest.TestCase):
    def setUp(self):
        self.unit = configparser.ConfigParser(interpolation=None)
        self.unit.read(ROOT / "deploy/self-hosted/society-host.service")

    def test_helper_and_diagnostics_stay_inside_the_selected_package(self):
        arguments = shlex.split(self.unit["Service"]["ExecStart"])
        self.assertIn("--sync", arguments)
        package = pathlib.PurePosixPath(arguments[arguments.index("--container") + 1])
        self.assertTrue(package.is_absolute())
        self.assertEqual(package.suffix, ".societycontainer")
        for option in ("--directory", "--status-file"):
            location = pathlib.PurePosixPath(arguments[arguments.index(option) + 1])
            relative = location.relative_to(package)
            self.assertEqual(relative.parts[0], ".society-runtime")
            self.assertNotIn("..", relative.parts)

    def test_systemd_does_not_create_external_app_data_directories(self):
        for option in ("StateDirectory", "CacheDirectory", "LogsDirectory", "RuntimeDirectory"):
            self.assertNotIn(option, self.unit["Service"])


if __name__ == "__main__":
    unittest.main()
