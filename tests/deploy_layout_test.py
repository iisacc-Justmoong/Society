"""Verify build deployment bookkeeping without executing an application."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import resource_workspace
import signal
import subprocess
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
source = ROOT / "cmake/single-app/deploy_macos.py"
spec = importlib.util.spec_from_file_location("whatson_deploy", source)
deploy = importlib.util.module_from_spec(spec)
spec.loader.exec_module(deploy)
verify_spec = importlib.util.spec_from_file_location(
    "verify_macos_sdk_identity", ROOT / "tests/verify_macos_sdk_identity.py")
verify_sdk = importlib.util.module_from_spec(verify_spec)
verify_spec.loader.exec_module(verify_sdk)


class DeploymentLayoutTests(unittest.TestCase):
    def test_bundled_sdk_must_match_the_selected_architecture_uuid(self):
        source = Path("/selected/libContainer.dylib")
        bundled = Path("/bundle/libContainer.dylib")
        with patch.object(verify_sdk, "architecture_uuids", side_effect=[
                {"arm64": "new", "x86_64": "other"}, {"arm64": "new"}]):
            verify_sdk.verify_pair(source, bundled)
        with patch.object(verify_sdk, "architecture_uuids", side_effect=[
                {"arm64": "new"}, {"arm64": "old"}]):
            with self.assertRaisesRegex(RuntimeError, "does not match"):
                verify_sdk.verify_pair(source, bundled)
        with patch.object(verify_sdk, "architecture_uuids", side_effect=[
                {"arm64": "new"}, {}]):
            with self.assertRaisesRegex(RuntimeError, "does not match"):
                verify_sdk.verify_pair(source, bundled)

    @staticmethod
    def lipo_failure(returncode):
        cause = subprocess.CalledProcessError(returncode, ["lipo", "-archs"], output="")
        error = RuntimeError("lipo failed")
        error.__cause__ = cause
        return error

    def test_lipo_sigkill_replaces_only_the_generated_file_and_retries(self):
        with resource_workspace.temporary(ROOT, "deployment-lipo-") as directory:
            binary = Path(directory) / "libFixture.dylib"
            binary.write_bytes(b"unchanged Mach-O fixture")
            binary.chmod(0o640)
            inode = binary.stat().st_ino
            with patch.object(deploy, "run", side_effect=[self.lipo_failure(-signal.SIGKILL), "arm64\n"]) as run:
                self.assertEqual(deploy.macho_architectures(binary, Path(directory)), {"arm64"})
            self.assertEqual(run.call_count, 2)
            self.assertNotEqual(binary.stat().st_ino, inode)
            self.assertEqual(binary.read_bytes(), b"unchanged Mach-O fixture")
            self.assertEqual(binary.stat().st_mode & 0o777, 0o640)
            self.assertEqual(list(Path(directory).iterdir()), [binary])

    def test_lipo_other_failure_is_not_retried(self):
        with resource_workspace.temporary(ROOT, "deployment-lipo-") as directory:
            binary = Path(directory) / "libFixture.dylib"
            binary.write_bytes(b"unchanged")
            inode = binary.stat().st_ino
            with patch.object(deploy, "run", side_effect=self.lipo_failure(1)) as run:
                with self.assertRaisesRegex(RuntimeError, "lipo failed"):
                    deploy.macho_architectures(binary, Path(directory))
            self.assertEqual(run.call_count, 1)
            self.assertEqual(binary.stat().st_ino, inode)
            self.assertEqual(binary.read_bytes(), b"unchanged")

    def test_lipo_restage_failure_preserves_original_and_cleans_temporary(self):
        with resource_workspace.temporary(ROOT, "deployment-lipo-") as directory:
            binary = Path(directory) / "libFixture.dylib"
            binary.write_bytes(b"unchanged")
            with patch.object(deploy, "run", side_effect=self.lipo_failure(-signal.SIGKILL)), \
                 patch.object(deploy.shutil, "copy2", side_effect=OSError("copy failed")):
                with self.assertRaisesRegex(OSError, "copy failed"):
                    deploy.macho_architectures(binary, Path(directory))
            self.assertEqual(binary.read_bytes(), b"unchanged")
            self.assertEqual(list(Path(directory).iterdir()), [binary])

    def test_lipo_restage_never_modifies_files_outside_bundle(self):
        with resource_workspace.temporary(ROOT, "deployment-lipo-") as directory:
            root = Path(directory)
            bundle = root / "fixture.app"
            bundle.mkdir()
            outside = root / "external.dylib"
            outside.write_bytes(b"external")
            inode = outside.stat().st_ino
            with patch.object(deploy, "run", side_effect=self.lipo_failure(-signal.SIGKILL)) as run:
                with self.assertRaisesRegex(RuntimeError, "lipo failed"):
                    deploy.macho_architectures(outside, bundle)
            self.assertEqual(run.call_count, 1)
            self.assertEqual(outside.stat().st_ino, inode)
            self.assertEqual(outside.read_bytes(), b"external")

    def test_lipo_is_retried_only_once(self):
        with resource_workspace.temporary(ROOT, "deployment-lipo-") as directory:
            binary = Path(directory) / "libFixture.dylib"
            binary.write_bytes(b"unchanged")
            with patch.object(deploy, "run", side_effect=self.lipo_failure(-signal.SIGKILL)) as run:
                with self.assertRaisesRegex(RuntimeError, "lipo failed"):
                    deploy.macho_architectures(binary, Path(directory))
            self.assertEqual(run.call_count, 2)
            self.assertEqual(binary.read_bytes(), b"unchanged")

    def test_report_is_console_output_by_default(self):
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            deploy.emit_report({"deployed": True})
        self.assertEqual(json.loads(output.getvalue()), {"deployed": True})

    def test_explicit_report_remains_supported(self):
        with resource_workspace.temporary(ROOT, "deployment-test-") as directory:
            target = Path(directory) / "report.json"
            deploy.emit_report({"deployed": True}, target)
            self.assertEqual(json.loads(target.read_text()), {"deployed": True})

    def test_locks_live_in_resource_directory_and_distinguish_bundle_paths(self):
        with resource_workspace.temporary(ROOT, "deployment-test-") as directory:
            root = Path(directory)
            lock_dir = root / "resources" / "locks"
            app1 = root / "first" / "build" / "WhatSon.app"
            app2 = root / "second" / "build" / "WhatSon.app"
            for app in (app1, app2):
                app.parent.mkdir(parents=True)
                with deploy.deployment_lock(app, lock_dir) as locked:
                    self.assertEqual(locked, app.resolve())
            self.assertEqual(len(list(lock_dir.iterdir())), 2)
            self.assertEqual(list(app1.parent.iterdir()), [])
            self.assertEqual(list(app2.parent.iterdir()), [])

    def test_framework_copy_keeps_runtime_resources_without_development_headers(self):
        with resource_workspace.temporary(ROOT, "deployment-test-") as directory:
            root = Path(directory)
            app = root / "build" / "WhatSon.app"
            main = app / "Contents/MacOS/WhatSon"
            main.parent.mkdir(parents=True)
            main.write_bytes(b"test executable")
            framework = root / "qt/lib/QtGui.framework"
            version = framework / "Versions/A"
            for folder in ("Headers", "PrivateHeaders", "Modules", "Resources"):
                (version / folder).mkdir(parents=True)
                (version / folder / "data").write_text(folder)
            (version / "QtGui").write_bytes(b"test framework")
            (framework / "Versions/Current").symlink_to("A")
            (framework / "QtGui").symlink_to("Versions/Current/QtGui")
            (framework / "Headers").symlink_to("Versions/Current/Headers")
            dependency = "@rpath/QtGui.framework/Versions/A/QtGui"
            with patch.object(deploy, "binaries", return_value=[main]), \
                 patch.object(deploy, "dependencies", side_effect=lambda p: [dependency] if p == main else []):
                deploy.complete_frameworks(app, root / "qt")
            installed = app / "Contents/Frameworks/QtGui.framework"
            self.assertEqual((installed / "QtGui").read_bytes(), b"test framework")
            self.assertEqual((installed / "Versions/A/Resources/data").read_text(), "Resources")
            for folder in ("Headers", "PrivateHeaders", "Modules"):
                self.assertFalse((installed / "Versions/A" / folder).exists())
            self.assertFalse((installed / "Headers").is_symlink())


if __name__ == "__main__":
    unittest.main()
