"""Exercise relocation with real Mach-O libraries, without loader overrides."""
import importlib.util
import os
from pathlib import Path
import plistlib
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("deployment", ROOT / "tools/deploy_macos_runtime.py")
deployment = importlib.util.module_from_spec(spec)
spec.loader.exec_module(deployment)


@unittest.skipUnless(sys.platform == "darwin", "Mach-O deployment is macOS-only")
class RuntimeDeploymentTests(unittest.TestCase):
    def setUp(self):
        self.fixture = tempfile.TemporaryDirectory(prefix="runtime-deployment-", dir=ROOT / "build")
        self.addCleanup(self.fixture.cleanup)
        self.root = Path(self.fixture.name)
        self.app = self.root / "Fixture.bundle"
        self.executable = self.app / "Contents/MacOS/Fixture"
        self.executable.parent.mkdir(parents=True)
        with (self.app / "Contents/Info.plist").open("wb") as file:
            plistlib.dump({"CFBundleIdentifier": "com.iisacc.society.runtime-test",
                          "CFBundleExecutable": "Fixture", "CFBundlePackageType": "APPL"}, file)

    def library(self, directory, name, value, install_name=None):
        directory.mkdir(parents=True, exist_ok=True)
        source = directory / (name + ".c")
        source.write_text(f"int {name}(void) {{ return {value}; }}\n")
        library = directory / ("lib" + name + ".dylib")
        deployment.run("cc", "-dynamiclib", source, "-o", library,
                       "-Wl,-headerpad_max_install_names", "-install_name",
                       install_name or "@rpath/" + library.name)
        source.unlink()
        return library

    def link(self, library, expected):
        source = self.root / "main.c"
        source.write_text(f"extern int choice(void); int main(void) {{ return choice() != {expected}; }}")
        deployment.run("cc", source, library, "-o", self.executable,
                       "-Wl,-headerpad_max_install_names", "-Wl,-rpath," + str(library.parent))

    def execute(self):
        env = {key: value for key, value in os.environ.items() if not key.startswith("DYLD_")}
        return subprocess.run([str(self.executable)], env=env, capture_output=True, text=True)

    def seal(self):
        deployment.localize(self.app)
        entitlements = self.root / "entitlements.plist"
        with entitlements.open("wb") as file:
            plistlib.dump({}, file)
        deployment.sign(self.app, "-", entitlements)

    def test_stale_signing_temporaries_are_removed_before_binary_discovery(self):
        self.link(self.library(self.root / "vendor", "choice", 3), 3)
        stale = self.executable.with_name("Fixture.cstemp")
        stale.write_bytes(bytes.fromhex("cffaedfe"))
        resource = self.app / "Contents/Resources/old.cstemp"
        resource.parent.mkdir(); resource.write_text("interrupted signature")
        outside = self.root / "preserved.cstemp"; outside.write_text("outside bundle")
        entitlements = self.root / "entitlements.plist"
        entitlements.write_bytes(plistlib.dumps({}))
        deployment.sign(self.app, "-", entitlements)
        self.assertFalse(stale.exists())
        self.assertFalse(resource.exists())
        self.assertEqual(outside.read_text(), "outside bundle")
        self.assertEqual(self.execute().returncode, 0)

    def test_plugin_framework_is_copied_without_redeploying_the_app(self):
        qt = self.root / "qt"
        source = self.library(qt / "lib/QtFixture.framework/Versions/A", "choice", 5,
                              "@rpath/QtFixture.framework/Versions/A/QtFixture")
        framework_binary = source.with_name("QtFixture")
        source.rename(framework_binary)
        framework = qt / "lib/QtFixture.framework"
        (framework / "Versions/Current").symlink_to("A")
        (framework / "QtFixture").symlink_to("Versions/Current/QtFixture")
        (framework / "Resources").symlink_to("Versions/Current/Resources")
        resources = framework / "Versions/A/Resources"
        resources.mkdir()
        with (resources / "Info.plist").open("wb") as file:
            plistlib.dump({"CFBundleIdentifier": "com.iisacc.fixture.framework",
                          "CFBundleExecutable": "QtFixture", "CFBundlePackageType": "FMWK",
                          "CFBundleVersion": "1"}, file)
        for name in ("Headers", "PrivateHeaders", "Modules"):
            directory = framework / "Versions/A" / name
            directory.mkdir()
            (directory / "development.txt").write_text("build-time metadata")
            (framework / name).symlink_to("Versions/Current/" + name)
        (resources / "LICENSE.txt").write_text("Runtime license fixture")
        self.link(framework_binary, 5)
        deployment.complete_frameworks(self.app, qt)
        deployment.prune_framework_development_files(self.app)
        destination = self.app / "Contents/Frameworks/QtFixture.framework/Versions/A/QtFixture"
        self.assertTrue(destination.is_file())
        deployed = destination.parents[2]
        for name in ("Headers", "PrivateHeaders", "Modules"):
            self.assertFalse((deployed / name).is_symlink())
            self.assertFalse((deployed / "Versions/A" / name).exists())
            self.assertTrue((framework / "Versions/A" / name / "development.txt").is_file())
        self.assertEqual((deployed / "Resources/LICENSE.txt").read_text(), "Runtime license fixture")
        deployment.localize(self.app)
        self.seal()
        framework_binary.unlink()
        self.assertEqual(self.execute().returncode, 0)

    def test_plain_helper_is_relocated_and_signed_without_nested_app(self):
        library = self.library(self.root / "vendor", "choice", 4)
        self.link(library, 4)
        helper = self.executable.with_name("Helper")
        import shutil
        shutil.copy2(self.executable, helper)
        deployment.prepare_native(self.app, [], [], helper)
        deployment.prepare_native(self.app, [], [])
        self.seal()
        library.unlink()
        result = subprocess.run([str(helper)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(list(self.app.rglob("*.app")))
        deployment.run("codesign", "--verify", "--strict", helper)

    def test_unused_runtime_is_pruned_but_plugins_and_helpers_remain_loadable(self):
        library = self.library(self.root / "vendor", "choice", 4)
        self.link(library, 4)
        import shutil
        helper = self.executable.with_name("Helper")
        shutil.copy2(self.executable, helper)
        deployment.prepare_native(self.app, [], [])
        deployment.prepare_native(self.app, [], [], helper)
        unused = self.library(self.app / "Contents/Frameworks", "unused", 99)
        plugin_dependency = self.library(self.app / "Contents/Frameworks", "plugin_dependency", 9,
            "@loader_path/../../Frameworks/libplugin_dependency.dylib")
        plugin = self.app / "Contents/PlugIns/example/libplugin.dylib"
        plugin.parent.mkdir(parents=True)
        source = self.root / "plugin.c"
        source.write_text("extern int plugin_dependency(void); int plugin(void) { return plugin_dependency(); }")
        deployment.run("cc", "-dynamiclib", source, plugin_dependency, "-o", plugin,
                       "-Wl,-headerpad_max_install_names", "-install_name", "@rpath/libplugin.dylib")
        removed = deployment.prune_native(self.app)
        self.assertIn(str(unused.relative_to(self.app)), removed)
        self.assertFalse(unused.exists())
        self.assertTrue((self.app / "Contents/Frameworks/libchoice.dylib").is_file())
        self.assertTrue(plugin_dependency.is_file())
        deployment.localize(self.app)
        deployment.strip_native(self.app)
        self.seal()
        library.unlink()
        self.assertEqual(self.execute().returncode, 0)
        self.assertEqual(subprocess.run([str(helper)]).returncode, 0)
        import ctypes
        self.assertEqual(ctypes.CDLL(str(plugin)).plugin(), 9)

    def test_selected_sdk_replaces_an_older_transitive_copy(self):
        old = self.library(self.root / "old", "choice", 1)
        selected = self.library(self.root / "selected", "choice", 2)
        self.link(old, 2)
        deployment.prepare_native(self.app, [selected], [old.parent])
        self.seal()
        self.assertEqual(self.execute().returncode, 0)
        self.assertFalse(deployment.rpaths(self.executable))

    def test_plugin_install_name_is_not_a_missing_framework_dependency(self):
        selected = self.library(self.root / "selected", "choice", 2)
        self.link(selected, 2)
        plugin = self.library(self.app / "Contents/PlugIns/example", "plugin", 0)
        deployment.prepare_native(self.app, [selected], [])
        self.seal()
        self.assertEqual(deployment.dependencies(plugin), ["/usr/lib/libSystem.B.dylib"])
        self.assertEqual(self.execute().returncode, 0)

    def test_gpu_resources_are_sealed_and_remain_colocated_for_mlx(self):
        library = self.library(self.root / "vendor", "choice", 3, "@rpath/libmlx.dylib")
        library = library.rename(library.with_name("libmlx.dylib"))
        (library.parent / "mlx.metallib").write_bytes(b"GPU library fixture")
        self.link(library, 3)
        deployment.prepare_native(self.app, [library], [])
        self.seal()
        resource = self.app / "Contents/Frameworks/mlx.metallib"
        self.assertTrue(resource.is_symlink())
        self.assertEqual(resource.read_bytes(), b"GPU library fixture")
        self.assertTrue(resource.resolve().is_relative_to(self.app / "Contents/Resources"))
        self.assertEqual(self.execute().returncode, 0)

    def test_bundle_relative_missing_crypto_is_found_only_from_explicit_sources(self):
        library = self.library(self.root / "openssl", "choice", 7,
                               "@executable_path/../Frameworks/libcrypto.3.dylib")
        crypto = library.with_name("libcrypto.3.dylib")
        library.rename(crypto)
        self.link(crypto, 7)
        self.assertNotEqual(self.execute().returncode, 0)
        with self.assertRaisesRegex(RuntimeError, "Missing runtime dependency"):
            deployment.prepare_native(self.app, [], [])
        deployment.prepare_native(self.app, [crypto], [])
        (self.app / "Contents/Frameworks/libcrypto.3.dylib").chmod(0o444)
        deployment.prepare_native(self.app, [crypto], [])
        crypto.unlink()
        self.seal()
        self.assertEqual(self.execute().returncode, 0)

    def test_removed_homebrew_transitive_dependency_uses_explicit_runtime_copy(self):
        stale = self.root / "removed-homebrew/lib/libchild.dylib"
        child = self.library(self.root / "runtime", "child", 9, str(stale))
        source = self.root / "choice.c"
        source.write_text("extern int child(void); int choice(void) { return child(); }")
        library = self.root / "libchoice.dylib"
        deployment.run("cc", "-dynamiclib", source, child, "-o", library,
                       "-Wl,-headerpad_max_install_names", "-install_name", "@rpath/libchoice.dylib")
        self.link(library, 9)
        with self.assertRaisesRegex(RuntimeError, "Missing runtime dependency"):
            deployment.prepare_native(self.app, [], [])
        deployment.prepare_native(self.app, [], [child.parent])
        self.seal()
        child.unlink()
        library.unlink()
        self.assertEqual(self.execute().returncode, 0)
        bundled = self.app / "Contents/Frameworks/libchoice.dylib"
        self.assertNotIn(str(stale), deployment.dependencies(bundled))
        self.assertIn("@loader_path/libchild.dylib", deployment.dependencies(bundled))

    def test_transitive_loader_paths_survive_removing_the_original_directory(self):
        child = self.library(self.root / "vendor", "child", 9)
        source = child.parent / "choice.c"
        source.write_text("extern int child(void); int choice(void) { return child(); }")
        library = child.parent / "libchoice.dylib"
        deployment.run("cc", "-dynamiclib", source, child, "-o", library,
                       "-Wl,-headerpad_max_install_names", "-install_name", "@rpath/libchoice.dylib",
                       "-Wl,-rpath,@loader_path")
        self.link(library, 9)
        deployment.prepare_native(self.app, [], [])
        child.unlink()
        library.unlink()
        self.seal()
        self.assertEqual(self.execute().returncode, 0)


if __name__ == "__main__":
    unittest.main()
