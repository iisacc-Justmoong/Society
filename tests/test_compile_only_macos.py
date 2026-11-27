"""Check the generated macOS build command without running or packaging an app."""

from pathlib import Path
import re
import subprocess
import sys
import unittest


class CompileOnlyMacosTest(unittest.TestCase):
    def setUp(self):
        cache = Path(__file__).resolve().parents[1] / "build/CMakeCache.txt"
        if not cache.exists() or "SOCIETY_COMPILE_ONLY:BOOL=ON" not in cache.read_text():
            self.skipTest("requires a compile-only build configuration")

    @unittest.skipUnless(sys.platform == "darwin", "macOS bundle staging applies only on macOS")
    def test_society_link_does_not_stage_runtime_resources(self):
        root = Path(__file__).resolve().parents[1]
        build = root / "build"
        cache = (build / "CMakeCache.txt").read_text()
        self.assertTrue("SOCIETY_COMPILE_ONLY:BOOL=ON" in cache, "build/ is not configured for compile-only verification")
        self.assertTrue("CMAKE_GENERATOR:INTERNAL=Ninja" in cache, "this command check requires Ninja")
        commands = subprocess.run(["ninja", "-C", str(build), "-t", "commands", "Society"],
                                  check=True, capture_output=True, text=True).stdout.splitlines()
        links = [line for line in commands if re.search(r"-o\s+\S*Society\.app/Contents/MacOS/Society(?:\s|$)", line)]
        self.assertEqual(len(links), 1)
        self.assertFalse("deploy_macos_runtime.py" in links[0], "link still deploys the runtime")
        self.assertFalse("share/iiGUI/resources/native" in links[0], "link still copies native resources")
        self.assertFalse("Contents/MacOS/SocietyDaemon" in links[0], "link still stages the daemon in the bundle")

    @unittest.skipUnless(sys.platform == "darwin", "macOS bundle staging applies only on macOS")
    def test_package_target_refuses_compile_only_output(self):
        root = Path(__file__).resolve().parents[1]
        build = root / "build"
        result = subprocess.run(["cmake", "--build", str(build), "--target", "SocietyDaemonPackage"],
                                capture_output=True, text=True, check=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("unavailable in compile-only mode", result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
