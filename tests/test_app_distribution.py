"""Check the three-app distribution without launching or installing any app."""

import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parents[1]
BUILDER_ROOT = ROOT.parents[1] / "SDK/iiSDKBuilder"
HOOK = BUILDER_ROOT / "cmake/iiSDKBuilderAppTree.cmake"
BUILDER = BUILDER_ROOT / "build/iisdk-builder"
POLICY = ROOT / "cmake/AppDistribution.cmake"
REQUIRED = {"Society", "Daemon", "Uninstaller"}
OTHER = {"Shared", "platform", "SocietyCommunity"}


class AppDistributionTests(unittest.TestCase):
    """Exercise Society's selection together with Builder's discovery and installer."""

    def setUp(self):
        """Create disposable source inputs and separately owned build outputs."""
        # Inputs live under resources; all generated build products live under build/.
        resources = ROOT / "resources/.state"
        builds = ROOT / "build/app-distribution-tests"
        resources.mkdir(parents=True, exist_ok=True)
        builds.mkdir(parents=True, exist_ok=True)
        self.input_tmp = tempfile.TemporaryDirectory(prefix="inputs-", dir=resources)
        self.build_tmp = tempfile.TemporaryDirectory(prefix="outputs-", dir=builds)
        self.addCleanup(self.input_tmp.cleanup)
        self.addCleanup(self.build_tmp.cleanup)
        self.inputs = Path(self.input_tmp.name)
        self.builds = Path(self.build_tmp.name)
        self.product = self.inputs / "product"
        self.product.mkdir()
        for name in REQUIRED | OTHER:
            app = self.product / "App" / name
            app.mkdir(parents=True)
            (app / "main.cpp").write_text("int main() { return 0; }\n")
        icon = ROOT / "resources/Appicon/generated/macos/Society.icns"
        shutil.copyfile(icon, self.product / "Society.icns")
        (self.product / "CMakeLists.txt").write_text(f'''cmake_minimum_required(VERSION 3.31)
project(Society VERSION 1.2.3 LANGUAGES CXX)
include([=[{POLICY}]=])
file(WRITE "${{CMAKE_BINARY_DIR}}/exclusions.txt" "${{IISDKBUILDER_APP_TREE_EXCLUDE}}")
add_executable(Society App/Society/main.cpp)
add_executable(SocietyDaemon App/Daemon/main.cpp)
if(APPLE)
  target_sources(Society PRIVATE Society.icns)
  set_source_files_properties(Society.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
  set_target_properties(Society PROPERTIES MACOSX_BUNDLE_ICON_FILE Society.icns)
endif()
''')

    def configure(self, *extra):
        """Configure a disposable project using Builder's actual tree hook."""
        return subprocess.run(
            ["cmake", "-S", str(self.product), "-B", str(self.builds / "build"),
             "-DIISDKBUILDER_APP_TREE_ENTRYPOINTS_ONLY=ON",
             f"-DCMAKE_PROJECT_TOP_LEVEL_INCLUDES={HOOK}", *extra],
            capture_output=True, text=True, timeout=90)

    def test_product_includes_policy(self):
        """The production project must apply the same policy as the fixture."""
        self.assertTrue("include(cmake/AppDistribution.cmake)" in (ROOT / "CMakeLists.txt").read_text(),
                        "The production project must include its app-distribution policy")

    def test_discovery_selects_three_apps_and_keeps_existing_exclusions(self):
        """Select only the contract apps, including when another direct entry appears."""
        unexpected = self.product / "App/NewProduct"
        unexpected.mkdir()
        (unexpected / "main.cpp").write_text("int main() { return 0; }\n")
        result = self.configure("-DIISDKBUILDER_APP_TREE_EXCLUDE=ExistingExclusion")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        exclusions = (self.builds / "build/exclusions.txt").read_text().split(";")
        self.assertIn("ExistingExclusion", exclusions)
        self.assertIn("NewProduct", exclusions)
        manifest = (self.builds / "build/iisdkbuilder-app-tree.txt").read_text()
        self.assertEqual({line.split("\t")[0] for line in manifest.splitlines()}, REQUIRED)
        built = subprocess.run(["cmake", "--build", str(self.builds / "build"),
                                "--target", "iisdkbuilder-app-tree", "--parallel", "2"],
                               capture_output=True, text=True, timeout=120)
        self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
        if sys.platform == "darwin":
            bundles = (self.builds / "build/iiSDKBuilderApps").glob("*.app")
            self.assertEqual({p.stem for p in bundles}, REQUIRED)

    def test_missing_or_excluded_required_app_fails(self):
        """Reject a distribution that would silently omit a required direct entry."""
        (self.product / "App/Uninstaller/main.cpp").unlink()
        missing = self.configure()
        self.assertNotEqual(missing.returncode, 0)
        self.assertIn("Uninstaller", missing.stdout + missing.stderr)
        (self.product / "App/Uninstaller/main.cpp").write_text("int main() { return 0; }\n")
        excluded = self.configure("-DIISDKBUILDER_APP_TREE_EXCLUDE=Daemon")
        self.assertNotEqual(excluded.returncode, 0)
        self.assertIn("Daemon", excluded.stdout + excluded.stderr)

    def test_unselected_standalone_project_fails_closed(self):
        """Do not let Builder independently package an unapproved CMake project."""
        (self.product / "App/Shared/CMakeLists.txt").write_text("add_executable(Shared main.cpp)\n")
        result = self.configure()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Shared", result.stdout + result.stderr)
        self.assertIn("standalone", (result.stdout + result.stderr).lower())

    def test_selected_standalone_is_left_to_builder(self):
        """Honor Builder's exclusion when it owns the selected standalone app."""
        (self.product / "App/Daemon/CMakeLists.txt").write_text("add_executable(Daemon main.cpp)\n")
        result = self.configure("-DIISDKBUILDER_APP_TREE_EXCLUDE=Daemon")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        manifest = (self.builds / "build/iisdkbuilder-app-tree.txt").read_text()
        self.assertEqual({line.split("\t")[0] for line in manifest.splitlines()}, REQUIRED - {"Daemon"})

    @unittest.skipUnless(sys.platform == "darwin", "The installer payload and Finder icon check require macOS")
    def test_builder_installer_contains_three_apps_and_product_folder_icon(self):
        """Inspect the real Builder's output and payload, never executing the fixture apps."""
        if not BUILDER.is_file():
            self.skipTest("Build iiSDKBuilder in its build/ directory to verify the installer")
        core = self.inputs / "sdk/iiSDKCore"
        core.mkdir(parents=True)
        (core / "CMakeLists.txt").write_text('''cmake_minimum_required(VERSION 3.31)
project(Core LANGUAGES CXX)
add_library(Core INTERFACE)
install(TARGETS Core EXPORT CoreTargets)
install(EXPORT CoreTargets NAMESPACE iiSDKCore:: DESTINATION lib/cmake/iiSDKCore)
''')
        manifest = self.inputs / "manifest.json"
        manifest.write_text(json.dumps({
            "schema": 1, "mode": "application", "project": {"name": "Society", "source": str(self.product)},
            "sdkRoot": str(core.parent), "buildRoot": str(self.builds / "build"),
            "kits": [{"id": "host", "platform": "macos"}],
            "installer": {"identifier": "com.example.society.distribution-fixture"},
        }))
        result = subprocess.run([str(BUILDER), "build", "--manifest", str(manifest)],
                                capture_output=True, text=True, timeout=240)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        report = json.loads(result.stdout)
        self.assertTrue(report["succeeded"])
        kit = report["kits"][0]
        self.assertEqual(kit["execution"], "not_requested")
        folder = Path(kit["packages"][0])
        self.assertEqual({p.stem for p in folder.glob("*.app")}, REQUIRED)

        expanded = self.builds / "expanded"
        unpack = subprocess.run(["/usr/sbin/pkgutil", "--expand-full", kit["installers"][0], str(expanded)],
                                capture_output=True, text=True, timeout=60)
        self.assertEqual(unpack.returncode, 0, unpack.stdout + unpack.stderr)
        location = ET.parse(expanded / "PackageInfo").getroot().attrib["install-location"]
        payload = expanded / "Payload"
        if location == "/Applications":
            payload = payload / "Society"
        else:
            self.assertEqual(location, "/Applications/Society")
        self.assertEqual({p.stem for p in payload.glob("*.app")}, REQUIRED)
        for product_folder in (folder, payload):
            finder = subprocess.run(["/usr/bin/xattr", "-px", "com.apple.FinderInfo", str(product_folder)],
                                    capture_output=True, text=True, check=True)
            self.assertTrue(bytes.fromhex(finder.stdout)[8] & 0x04)
            icon_file = product_folder / "Icon\r"
            self.assertTrue(icon_file.is_file())
            fork = subprocess.run(["/usr/bin/xattr", "-px", "com.apple.ResourceFork", str(icon_file)],
                                  capture_output=True, text=True, check=True)
            self.assertTrue((self.product / "Society.icns").read_bytes() in bytes.fromhex(fork.stdout),
                            "The Society icon data is absent from the Finder resource fork")


if __name__ == "__main__":
    unittest.main()
