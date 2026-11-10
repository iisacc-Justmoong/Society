"""Every test temporary directory must specify a project-owned parent."""
import ast
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import resource_workspace
import subprocess
import sys
import unittest
from unittest.mock import patch

TESTS = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('society_macos_inference', TESTS.parent / 'tools/build_macos_inference_sdk.py')
inference = importlib.util.module_from_spec(spec)
spec.loader.exec_module(inference)


class TempFixturePolicy(unittest.TestCase):
    def test_local_runtime_package_selection_precedes_package_discovery(self):
        cmake = (TESTS.parent / 'CMakeLists.txt').read_text()
        include = 'include(cmake/MacRuntimePackages.cmake)'
        select = 'society_use_local_macos_runtime_packages("${CMAKE_CURRENT_SOURCE_DIR}/resources/dependencies/macos-runtime")'
        self.assertIn(include, cmake)
        self.assertIn(select, cmake)
        self.assertLess(cmake.index(include), cmake.index(select))
        self.assertLess(cmake.index(select), cmake.index('find_package('))
        self.assertNotIn('/build/project-catalog-stage', cmake)
        self.assertNotIn('/build/stage-bridge', cmake)
        self.assertNotIn('${CMAKE_CURRENT_BINARY_DIR}/runtime-sdk/', cmake)
        self.assertIn('/resources/dependencies/society-compat/stage-bridge', cmake)

    def test_bundled_resource_probe_does_not_generate_a_manifest_in_build(self):
        cmake = (TESTS.parent / 'CMakeLists.txt').read_text()
        self.assertFalse('lvrs-embedded-resources.txt' in cmake, 'test inputs must not be written into build/')
        self.assertIn('"${LVRS_QML_IMPORT_PATH}/LVRS"', cmake)

    def test_deployment_report_is_not_left_in_the_build_directory(self):
        cmake = (TESTS.parent / 'CMakeLists.txt').read_text()
        self.assertNotIn('--log "${CMAKE_CURRENT_BINARY_DIR}/macos-runtime-deployment.log"', cmake)
        self.assertIn('--log "${society_deployment_log}"', cmake)
        self.assertIn('COMMAND "${CMAKE_COMMAND}" -E rm -f "${society_deployment_log}"', cmake)

    def test_temporary_directories_declare_their_parent(self):
        for path in TESTS.glob('*.py'):
            tree = ast.parse(path.read_text(), filename=str(path))
            for node in ast.walk(tree):
                if isinstance(node, ast.Call) and isinstance(node.func, ast.Attribute) and node.func.attr == 'TemporaryDirectory':
                    self.assertTrue(any(item.arg == 'dir' for item in node.keywords), f'{path.name}:{node.lineno} must specify dir')

    def test_test_input_directories_are_not_placed_in_build(self):
        for path in TESTS.glob('*.py'):
            tree = ast.parse(path.read_text(), filename=str(path))
            for node in ast.walk(tree):
                if isinstance(node, ast.Call) and isinstance(node.func, ast.Attribute) and node.func.attr == 'TemporaryDirectory':
                    for keyword in node.keywords:
                        if keyword.arg == 'dir':
                            self.assertFalse(any(isinstance(part, ast.Constant) and part.value == 'build'
                                                 for part in ast.walk(keyword.value)),
                                             f'{path.name}:{node.lineno} must keep test inputs in resources')

    def test_fixture_inputs_and_compiled_outputs_have_separate_owned_roots(self):
        project = TESTS.parent
        with resource_workspace.temporary(project, 'layout-input-') as inputs, \
             resource_workspace.build(project, 'layout-build-') as output:
            self.assertTrue(inputs.is_relative_to(project / 'resources/.state'))
            self.assertTrue(output.is_relative_to(project / 'build'))
            resource_workspace.provider.write_text(inputs / 'fixture.txt', 'test data')
        self.assertFalse(inputs.exists())
        self.assertFalse(output.exists())

    def test_inference_profile_builds_only_module_and_tests_and_reports_on_stdout(self):
        with resource_workspace.temporary(TESTS.parent, 'inference-layout-test-') as fixture:
            project = fixture / 'product'
            source = fixture / 'sdk/iiLocalDiffusion'
            resource_workspace.provider.mkdir(project, parents=True)
            resource_workspace.provider.mkdir(source, parents=True)
            resource_workspace.provider.write_text(source / 'CMakeLists.txt', 'fixture')
            output = io.StringIO()
            with patch.object(inference, 'ROOT', project), \
                 patch.object(inference.sys, 'platform', 'darwin'), \
                 patch.object(inference.sys, 'argv', ['build_macos_inference_sdk.py', '--sdk-source', str(source)]), \
                 patch.object(inference.subprocess, 'run') as run, \
                 patch.object(inference.subprocess, 'check_output', return_value='no optional runtime'), \
                 contextlib.redirect_stdout(output):
                inference.main()
            commands = [call.args[0] for call in run.call_args_list]
            self.assertEqual(Path(commands[0][commands[0].index('-B') + 1]), source / 'build/society-macos')
            prefix = project / 'resources/dependencies/macos-runtime/iiLocalDiffusion-apple'
            self.assertIn('-DCMAKE_INSTALL_PREFIX=' + str(prefix), commands[0])
            self.assertIn('-DIILD_BUILD_TOOLS=OFF', commands[0])
            self.assertNotIn('iild-run', commands[1])
            self.assertNotIn('iild-model-package', commands[1])
            self.assertEqual(json.loads(output.getvalue().splitlines()[-1])['package'],
                             str(prefix / 'lib/cmake/iiLocalDiffusion'))
            self.assertFalse(list((project / 'resources/.state').glob('macos-inference-*')))
            self.assertFalse((project / 'build').exists())

    def test_inference_pkgconfig_is_relocatable_and_preserves_other_fields(self):
        with resource_workspace.temporary(TESTS.parent, 'inference-pc-test-') as fixture:
            pc = fixture / 'lib/pkgconfig/stable-diffusion.pc'
            resource_workspace.provider.mkdir(pc.parent, parents=True)
            resource_workspace.provider.write_text(
                pc, 'prefix=/old/location\nexec_prefix=${prefix}\n'
                    'libdir=/old/location/lib\nincludedir=/old/location/include\nName: stable-diffusion\n')
            inference.normalize_pkgconfig_prefix(fixture, resource_workspace.provider)
            result = pc.read_text()
            self.assertIn('prefix=${pcfiledir}/../..\n', result)
            self.assertIn('libdir=${prefix}/lib\n', result)
            self.assertIn('includedir=${prefix}/include\n', result)
            self.assertIn('Name: stable-diffusion\n', result)
            self.assertNotIn('/old/location', result)

    @unittest.skipIf(sys.platform == 'win32', 'Directory symlinks require Windows developer privileges')
    def test_inference_pkgconfig_rejects_a_symlinked_parent(self):
        with resource_workspace.temporary(TESTS.parent, 'inference-pc-test-') as fixture:
            prefix = fixture / 'prefix'
            external = fixture / 'other'
            resource_workspace.provider.mkdir(prefix)
            resource_workspace.provider.mkdir(external)
            (prefix / 'lib').symlink_to(external, target_is_directory=True)
            with self.assertRaisesRegex(ValueError, 'cannot contain symlinks'):
                inference.normalize_pkgconfig_prefix(prefix, resource_workspace.provider)

    def test_local_macos_runtime_packages_preserve_explicit_overrides(self):
        with resource_workspace.temporary(TESTS.parent, 'runtime-packages-test-') as fixture:
            root = fixture / 'runtime'
            packages = {'iiLocalDiffusion': 'iiLocalDiffusion-apple', 'iiDecision': 'iiDecision-onnx-free',
                        'iiLocalLLM': 'iiLocalLLM-onnx-free', 'iiMultimodalAgent': 'iiMultimodalAgent-onnx-free',
                        'iiVoiceOver': 'iiVoiceOver-onnx-free'}
            for name, directory in packages.items():
                config = root / directory / 'lib/cmake' / name / (name + 'Config.cmake')
                resource_workspace.provider.mkdir(config.parent, parents=True, exist_ok=True)
                resource_workspace.provider.write_text(config, '# fixture')
            mlx = root / 'iiLocalDiffusion-apple'
            for relative in ('share/cmake/MLX/MLXConfig.cmake', 'lib/libmlx.dylib'):
                path = mlx / relative
                resource_workspace.provider.mkdir(path.parent, parents=True, exist_ok=True)
                resource_workspace.provider.write_text(path, 'fixture')
            local_runtime = fixture / 'macos-mlx-runtime'
            resource_workspace.provider.mkdir(local_runtime)
            for filename in ('libmlx.dylib', 'mlx.metallib'):
                resource_workspace.provider.write_text(local_runtime / filename, 'fixture')
            script = fixture / 'runtime-test.cmake'
            helper = TESTS.parent / 'cmake/MacRuntimePackages.cmake'
            resource_workspace.provider.write_text(script, f'''set(APPLE TRUE)
set(CMAKE_SYSTEM_NAME Darwin)
set(iiVoiceOver_DIR "/caller/preferred")
set(iiDecision_DIR "iiDecision_DIR-NOTFOUND" CACHE PATH "" FORCE)
include("{helper.as_posix()}")
society_use_local_macos_runtime_packages("{root.as_posix()}")
if(NOT iiLocalDiffusion_DIR STREQUAL "{(root / "iiLocalDiffusion-apple/lib/cmake/iiLocalDiffusion").as_posix()}")
  message(FATAL_ERROR "local diffusion package was not selected")
endif()
if(NOT iiDecision_DIR STREQUAL "{(root / "iiDecision-onnx-free/lib/cmake/iiDecision").as_posix()}")
  message(FATAL_ERROR "missing decision package was not replaced")
endif()
if(NOT iiVoiceOver_DIR STREQUAL "/caller/preferred")
  message(FATAL_ERROR "explicit package was changed")
endif()
if(NOT MLX_DIR STREQUAL "{(mlx / "share/cmake/MLX").as_posix()}" OR
   NOT MLX_LIBRARY STREQUAL "{(mlx / "lib/libmlx.dylib").as_posix()}")
  message(FATAL_ERROR "local MLX package was not selected")
endif()
if(NOT SOCIETY_MLX_RUNTIME_DIR STREQUAL "{local_runtime.as_posix()}")
  message(FATAL_ERROR "matching local MLX runtime was not selected")
endif()
set(SOCIETY_MLX_RUNTIME_DIR "/caller/explicit" CACHE PATH "" FORCE)
society_use_local_macos_runtime_packages("{root.as_posix()}")
if(NOT SOCIETY_MLX_RUNTIME_DIR STREQUAL "/caller/explicit")
  message(FATAL_ERROR "explicit MLX runtime was changed")
endif()
unset(iiLocalLLM_DIR CACHE)
unset(iiLocalLLM_DIR)
set(CMAKE_SYSTEM_NAME iOS)
society_use_local_macos_runtime_packages("{root.as_posix()}")
if(DEFINED iiLocalLLM_DIR)
  message(FATAL_ERROR "macOS runtime leaked into an iOS build")
endif()
set(CMAKE_SYSTEM_NAME Darwin)
unset(iiDecision_DIR CACHE)
unset(iiDecision_DIR)
society_use_local_macos_runtime_packages("{(fixture / "missing").as_posix()}")
if(DEFINED iiDecision_DIR)
  message(FATAL_ERROR "missing local package blocked normal discovery")
endif()
''')
            result = subprocess.run(['cmake', '-P', str(script)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
