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
        self.app = self.root / "Fixture.app"
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
