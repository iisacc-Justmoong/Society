"""Exercise missing-toolchain gates and separate device/simulator build plans."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

SOURCE = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('society_ios_build', SOURCE / 'tools/build_ios.py')
build_ios = importlib.util.module_from_spec(spec)
spec.loader.exec_module(build_ios)


class IosBuildTest(unittest.TestCase):
    def test_container_is_installed_before_helper_configures(self):
        for mode in ('ios-device', 'ios-simulator'):
            plan = build_ios.commands(mode, Path('/Volumes/Storage/Qt/6.8.3'), '', 'Debug')
            container_install = next(i for i, step in enumerate(plan)
                                     if '--install' in step and 'iiSocietyContainer' in step[2])
            helper_index = next(i for i, step in enumerate(plan)
                                if '-S' in step and Path(step[step.index('-S') + 1]).name == 'iiSocietyHelper')
            self.assertLess(container_install, helper_index)
            prefix = build_ios.WORKSPACE / 'build' / mode / 'install'
            self.assertIn(f'-DiiSocietyContainer_DIR={prefix}/lib/cmake/iiSocietyContainer', plan[helper_index])

    def test_command_line_tools_do_not_count_as_an_ios_sdk(self):
        with tempfile.TemporaryDirectory(dir=SOURCE / 'build') as directory:
            qt = Path(directory)
            for name in ('ios/lib/cmake/Qt6/qt.toolchain.cmake', 'macos/lib/cmake/Qt6/Qt6Config.cmake',
                         'ios/plugins/sqldrivers/libqsqlite.a'):
                path = qt / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.touch()
            with patch.object(build_ios, 'probe', return_value=None):
                result = build_ios.preflight(qt, 'iphonesimulator', '')
            self.assertTrue(any('Full Xcode' in failure for failure in result['problems']))
            self.assertIsNone(result['sdkPath'])

    def test_presets_and_bootstrap_use_the_same_abi_and_storage_sdks(self):
        presets = json.loads((SOURCE / 'CMakePresets.json').read_text())['configurePresets']
        for mode, sdk in (('ios-device', 'iphoneos'), ('ios-simulator', 'iphonesimulator')):
            plan = build_ios.commands(mode, Path('/Volumes/Storage/Qt/6.8.3'), 'TESTTEAM', 'Debug')
            configurations = [step for step in plan if '-S' in step]
            self.assertEqual(len(configurations), 4)
            for command in configurations:
                self.assertIn(f'-DCMAKE_OSX_SYSROOT={sdk}', command)
                output = Path(command[command.index('-B') + 1])
                self.assertEqual(output.name, mode)
                self.assertEqual(output.parent.name, 'build')
                self.assertNotIn('/Users/', ' '.join(command))
            self.assertIn('-DLVRS_BUILD_SHARED_LIBS=OFF', configurations[0])
            variables = next(p['cacheVariables'] for p in presets if p['name'] == mode)
            self.assertEqual(variables['CMAKE_OSX_SYSROOT'], sdk)
            self.assertEqual({k for k in variables if k.startswith('ii')},
                             {'iiSocietyContainer_DIR', 'iiSocietyHelper_DIR'})
            self.assertIn(mode, variables['LVRS_DIR'])


if __name__ == '__main__':
    unittest.main()
