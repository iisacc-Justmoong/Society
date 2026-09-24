"""Check mobile SDK build ordering and the generated Android photo bridge package."""
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SDK = ROOT.parents[1] / 'SDK/iiPhotoLibrary'


class PhotoLibraryIntegrationTests(unittest.TestCase):
    def test_ios_builds_target_sdk_before_app(self):
        spec = importlib.util.spec_from_file_location('society_build_ios', ROOT / 'tools/build_ios.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        for mode in ('ios-device', 'ios-simulator'):
            steps = module.commands(mode, Path('/fixture/Qt'), 'FIXTURETEAM', 'Release')
            sdk_configure = next(i for i, step in enumerate(steps) if str(SDK) in step)
            app_configure = next(i for i, step in enumerate(steps) if str(ROOT) in step)
            self.assertLess(sdk_configure, app_configure)
            self.assertIn(f'-DiiPhotoLibrary_DIR={ROOT.parents[1]}/build/{mode}/install/lib/cmake/iiPhotoLibrary', steps[app_configure])
            self.assertIn(f'-DiiSocietyContainer_DIR={ROOT.parents[1]}/build/{mode}/install/lib/cmake/iiSocietyContainer', steps[sdk_configure])
        presets = json.loads((ROOT / 'CMakePresets.json').read_text())
        for preset in presets['configurePresets']:
            variables = preset.get('cacheVariables', {})
            if 'iiSocietySync_DIR' in variables:
                self.assertIn('iiPhotoLibrary_DIR', variables)

    def test_android_packages_sdk_bridge_and_replaces_old_generated_copy(self):
        with tempfile.TemporaryDirectory(prefix='photo-package-', dir=ROOT / 'build') as directory:
            base = Path(directory)
            package = base / 'package'
            package.mkdir()
            (package / 'AndroidManifest.xml').write_text('<manifest><application><activity android:name="org.qtproject.qt.android.bindings.QtActivity"></activity></application></manifest>')
            obsolete = package / 'src/com/iisacc/society/SocietyPhotoLibrary.java'
            obsolete.parent.mkdir(parents=True)
            obsolete.write_text('old generated bridge')
            templates = base / 'qt/src/android/templates'
            templates.mkdir(parents=True)
            (templates / 'build.gradle').write_text('implementation fileTree')
            # Only target registration and Qt resource registration are mocked;
            # the real CMake helper performs manifest and Java file packaging.
            script = base / 'package.cmake'
            script.write_text(f'''set(CMAKE_CURRENT_SOURCE_DIR "{ROOT}")
set(Qt6_DIR "{base}/qt/lib/cmake/Qt6")
set(iiPhotoLibrary_ANDROID_SOURCE_DIR "{SDK}/platform/android/src")
function(get_target_property output target property)
  set(${{output}} "{package}" PARENT_SCOPE)
endfunction()
function(target_sources)
endfunction()
function(qt_add_resources)
endfunction()
include("{ROOT}/cmake/AndroidQr.cmake")
society_add_android_qr(Fixture)
''')
            subprocess.run(['cmake', '-P', str(script)], check=True, capture_output=True, text=True)
            bridge = package / 'src/com/iisacc/iiphotolibrary/PhotoLibrary.java'
            self.assertEqual(bridge.read_bytes(), (SDK / 'platform/android/src/com/iisacc/iiphotolibrary/PhotoLibrary.java').read_bytes())
            self.assertFalse(obsolete.exists())
            activity = (package / 'src/com/iisacc/society/SocietyActivity.java').read_text()
            self.assertIn('import com.iisacc.iiphotolibrary.PhotoLibrary;', activity)
            self.assertIn('PhotoLibrary.photoAccessFinished()', activity)
            self.assertIn('PhotoLibrary.consentFinished(result)', activity)
            manifest = (package / 'AndroidManifest.xml').read_text()
            self.assertIn('READ_MEDIA_VISUAL_USER_SELECTED', manifest)
            self.assertIn('READ_MEDIA_VIDEO', manifest)


if __name__ == '__main__':
    unittest.main()
