"""Check the product's six independent source entry points without launching them."""

from pathlib import Path
import re
import subprocess
import sys
import unittest


ROOT = Path(__file__).resolve().parents[1]
APPS = ("Uninstaller", "Daemon", "platform", "Shared", "Society", "SocietyCommunity")


class AppEntryTests(unittest.TestCase):
    """Keep the product source layout aligned with Builder's app discovery."""

    def test_each_immediate_app_directory_has_its_own_entry(self):
        """Every product app has an explicit main and the old shared entry is gone."""
        directories = {p.name for p in (ROOT / "App").iterdir() if p.is_dir() and not p.name.startswith(".")}
        self.assertEqual(directories, set(APPS))
        for name in APPS:
            with self.subTest(application=name):
                self.assertTrue((ROOT / "App" / name / "main.cpp").is_file())
        self.assertFalse((ROOT / "App/main.cpp").exists())

    def test_primary_target_builds_the_society_entry(self):
        """The existing product target must compile the relocated source file."""
        cmake = (ROOT / "CMakeLists.txt").read_text()
        self.assertTrue("App/Society/main.cpp" in cmake, "Society entry is absent from CMake")
        self.assertFalse("App/main.cpp" in cmake, "CMake still uses the old product-root entry")

    def test_product_declares_its_core_dependency(self):
        """The product explicitly resolves the shared iiSDKCore package."""
        cmake = (ROOT / "CMakeLists.txt").read_text()
        self.assertRegex(cmake, r"find_package\(\s*iiSDKCore\b[^)]*\bREQUIRED\s*\)")

    def test_tree_daemon_stages_its_runtime_only_for_distribution_builds(self):
        """The standalone daemon needs its own closure, except in compile-only builds."""
        build = ROOT / "build"
        cache_file = build / "CMakeCache.txt"
        if sys.platform != "darwin" or not cache_file.exists() or not (build / "build.ninja").exists():
            self.skipTest("Requires a configured macOS Ninja build")
        cache = cache_file.read_text()
        if "IISDKBUILDER_APP_TREE_ENTRYPOINTS_ONLY:UNINITIALIZED=ON" not in cache and \
           "IISDKBUILDER_APP_TREE_ENTRYPOINTS_ONLY:BOOL=ON" not in cache:
            self.skipTest("Requires iiSDKBuilder application-tree discovery")
        commands = subprocess.run(["ninja", "-C", str(build), "-t", "commands", "SocietyDaemon"],
                                  check=True, capture_output=True, text=True).stdout.splitlines()
        links = [line for line in commands if re.search(r"-o\s+\S*Daemon\.app/Contents/MacOS/Daemon(?:\s|$)", line)]
        self.assertEqual(len(links), 1)
        compile_only = "SOCIETY_COMPILE_ONLY:BOOL=ON" in cache
        self.assertEqual("deploy_macos_runtime.py" in links[0], not compile_only)
        self.assertNotIn("verify_macos_runtime.py", links[0])
        if not compile_only:
            society_commands = subprocess.run(["ninja", "-C", str(build), "-t", "commands", "Society"],
                                              check=True, capture_output=True, text=True).stdout.splitlines()
            society_links = [line for line in society_commands
                             if re.search(r"-o\s+\S*Society\.app/Contents/MacOS/Society(?:\s|$)", line)]
            self.assertEqual(len(society_links), 1)
            self.assertRegex(society_links[0], r"--search-path\s+\S*Daemon\.app/Contents/Frameworks")


if __name__ == "__main__":
    unittest.main()
