"""Verify that project-local includes and QML imports survive source moves."""
from pathlib import Path
import re
import sys
import unittest
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parents[1]
INCLUDE = re.compile(r'^\s*#\s*(?:include|import)\s+"([^"\n]+)"', re.MULTILINE)
QML_IMPORT = re.compile(r'^\s*import\s+"(\.[^"\n]*)"', re.MULTILINE)
PLATFORM_SOURCE = re.compile(r'\$\{CMAKE_CURRENT_SOURCE_DIR\}/(App/[^"\n]+)')


class SourceLayoutTests(unittest.TestCase):
    def test_platform_packaging_sources_exist(self):
        checked = []
        for helper in ("cmake/AndroidQr.cmake", "cmake/AppIcons.cmake"):
            for relative in PLATFORM_SOURCE.findall((ROOT / helper).read_text(encoding="utf-8")):
                checked.append(relative)
                self.assertTrue((ROOT / relative).is_file(), f"{helper}: {relative}")
        self.assertGreaterEqual(len(checked), 5)

    def test_macos_build_only_launches_the_app_when_explicitly_enabled(self):
        cache = ROOT / "build/CMakeCache.txt"
        ninja = ROOT / "build/build.ninja"
        if sys.platform != "darwin" or not cache.is_file() or not ninja.is_file():
            self.skipTest("Requires a configured macOS Ninja build")
        cache_text = cache.read_text(encoding="utf-8")
        system = re.search(r'^CMAKE_SYSTEM_NAME:[^=]*=(.*)$', cache_text, re.MULTILINE)
        if system and system.group(1) != "Darwin":
            self.skipTest("Requires a native macOS target")
        option = re.search(r'^SOCIETY_VERIFY_RUNTIME_ON_BUILD:BOOL=(ON|OFF)$',
                           cache_text, re.MULTILINE)
        self.assertIsNotNone(option, "The application launch check must be an explicit build option")
        build = ninja.read_text(encoding="utf-8")
        block = re.search(r'^build bin/Society\.app/Contents/MacOS/Society:.*?\n(?=\S)',
                          build, re.MULTILINE | re.DOTALL)
        self.assertIsNotNone(block, "Society's app link rule must exist")
        post_build = re.search(r'^  POST_BUILD = (.*)$', block.group(), re.MULTILINE)
        self.assertIsNotNone(post_build)
        self.assertEqual("tests/verify_macos_runtime.py" in post_build.group(1),
                         option.group(1) == "ON")

    def test_generated_qml_aliases_keep_the_existing_resource_urls(self):
        resource = ROOT / "build/.qt/rcc/Society_raw_qml_0.qrc"
        if not resource.is_file():
            self.skipTest("Configure the application in build/ to generate the QML resource map")
        qresource = ET.parse(resource).getroot().find("qresource")
        self.assertIsNotNone(qresource)
        self.assertEqual(qresource.get("prefix").rstrip("/"), "/qt/qml/Society")
        source_root = ROOT / "App/Society"
        aliases = {}
        for item in qresource.findall("file"):
            source = Path(item.text or "")
            try:
                relative = source.relative_to(source_root)
            except ValueError:
                continue
            alias = item.get("alias")
            self.assertEqual(alias, "App/" + relative.as_posix())
            self.assertTrue(source.is_file(), source)
            aliases[alias] = source
        self.assertIn("App/Main.qml", aliases)
        self.assertIn("App/Dashboard/Dashboard.qml", aliases)

    def test_project_header_includes_resolve(self):
        checked = 0
        failures = []
        for directory in (ROOT / "App", ROOT / "tests"):
            for path in directory.rglob("*"):
                if path.suffix not in {".h", ".hpp", ".cpp", ".mm"}:
                    continue
                for include in INCLUDE.findall(path.read_text(encoding="utf-8")):
                    if not include.startswith(("App/", "Society/", "Daemon/", "../src/", "../App/")):
                        continue
                    checked += 1
                    if not (path.parent / include).is_file() and not (ROOT / "App" / include).is_file():
                        failures.append(f"{path.relative_to(ROOT)}: {include}")
        self.assertGreater(checked, 0)
        self.assertEqual(failures, [], "Unresolved project headers:\n" + "\n".join(failures))

    def test_relative_qml_imports_resolve(self):
        checked = 0
        failures = []
        for directory in (ROOT / "App", ROOT / "tests"):
            for path in directory.rglob("*.qml"):
                for relative in QML_IMPORT.findall(path.read_text(encoding="utf-8")):
                    checked += 1
                    if not (path.parent / relative).is_dir():
                        failures.append(f"{path.relative_to(ROOT)}: {relative}")
        self.assertGreater(checked, 0)
        self.assertEqual(failures, [], "Unresolved QML imports:\n" + "\n".join(failures))


if __name__ == "__main__":
    unittest.main()
