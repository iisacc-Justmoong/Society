"""Reject untrusted source archives and verify the Qt Android TLS naming contract."""
import contextlib
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import resource_workspace
import sys
import tarfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

SOURCE = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('android_openssl', SOURCE / 'tools/build_android_openssl.py')
openssl = importlib.util.module_from_spec(spec)
spec.loader.exec_module(openssl)
files = resource_workspace.provider
sys.path.insert(0, str(SOURCE / 'tools'))
android_spec = importlib.util.spec_from_file_location('society_android_build', SOURCE / 'tools/build_android.py')
android = importlib.util.module_from_spec(android_spec)
android_spec.loader.exec_module(android)


class AndroidTlsBuildTest(unittest.TestCase):
    def test_android_build_uses_owned_resources_and_streams_output(self):
        with resource_workspace.temporary(SOURCE, 'android-build-test-') as temporary:
            project = Path(temporary) / 'product'
            files.mkdir(project)
            paths = {}
            for name in ('qt', 'host', 'sdk', 'ndk', 'lvrs'):
                paths[name] = project / name
                files.mkdir(paths[name])
            state_root = project / 'resources/.state'
            files.mkdir(state_root / 'other-user', parents=True)
            command = ['--qt', str(paths['qt']), '--qt-host', str(paths['host']),
                       '--sdk', str(paths['sdk']), '--ndk', str(paths['ndk']),
                       '--lvrs', str(paths['lvrs'])]
            calls = []
            def run(args, *, env):
                calls.append((args, env))
                return SimpleNamespace(returncode=0)
            with patch.object(android.subprocess, 'run', side_effect=run), \
                 patch.object(android, 'build_openssl', return_value=project / 'build/android/openssl/lib'), \
                 contextlib.redirect_stdout(io.StringIO()):
                android.main(command, project)
            account = next(args for args, _ in calls if '-S' in args and 'iiAccountManager' in args[args.index('-S') + 1])
            prefix = android.dependency_prefix(project, 'arm64-v8a', 'iiAccountManager')
            self.assertIn(f'-DCMAKE_INSTALL_PREFIX={prefix}', account)
            self.assertTrue(prefix.is_relative_to(project / 'resources/dependencies'))
            env = calls[0][1]
            self.assertTrue(Path(env['GRADLE_USER_HOME']).is_relative_to(state_root))
            self.assertTrue(Path(env['ANDROID_USER_HOME']).is_relative_to(state_root))
            self.assertTrue(Path(env['TMPDIR']).is_relative_to(state_root))
            self.assertEqual(env['ORG_GRADLE_PROJECT_android.builder.sdkDownload'], 'false')
            self.assertIn('-Dorg.gradle.problems.report=false', env['GRADLE_OPTS'])
            self.assertIn('-Djava.io.tmpdir=', env['JAVA_TOOL_OPTIONS'])
            self.assertFalse(list((project / 'build/android').glob('*.log')))
            self.assertEqual([p.name for p in state_root.iterdir()], ['other-user'])

            with patch.object(android.subprocess, 'run', return_value=SimpleNamespace(returncode=7)), \
                 contextlib.redirect_stdout(io.StringIO()):
                with self.assertRaises(SystemExit) as failure:
                    android.main(command, project)
            self.assertEqual(failure.exception.code, 7)
            self.assertEqual([p.name for p in state_root.iterdir()], ['other-user'])

    def test_runtime_cache_requires_matching_public_headers(self):
        with resource_workspace.temporary(SOURCE, 'android-tls-test-') as temporary:
            product = Path(temporary)
            ndk = product / 'ndk'; files.mkdir(ndk); files.write_text(ndk / 'source.properties', 'fixture-ndk')
            directory = product / 'build/android' / f'openssl-{openssl.VERSION}' / 'arm64-v8a'
            output = directory / 'lib'; files.mkdir(output, parents=True)
            for name in ('libcrypto_3.so', 'libssl_3.so'):
                files.write_bytes(output / name, b'')
            stamp = product / 'resources/.state/android-openssl/arm64-v8a/build.json'
            files.mkdir(stamp.parent, parents=True)
            metadata = {
                'version': openssl.VERSION, 'sha256': openssl.SHA256, 'abi': 'arm64-v8a', 'api': 28,
                'ndk': str(ndk.resolve()), 'ndkRevision': 'fixture-ndk',
                'builder': hashlib.sha256(Path(openssl.__file__).read_bytes()).hexdigest()}
            files.write_text(stamp, json.dumps(metadata))
            archive = product / 'resources/dependencies/android-openssl' / f'openssl-{openssl.VERSION}.tar.gz'
            files.mkdir(archive.parent, parents=True)
            files.write_bytes(archive, b'')
            with patch.object(openssl, 'PRODUCT', product), patch.object(openssl, 'verify_archive', side_effect=RuntimeError('rebuild-required')):
                with self.assertRaisesRegex(RuntimeError, 'rebuild-required'):
                    openssl.build_openssl(ndk, 'arm64-v8a')
                self.assertFalse(stamp.exists())
                headers = directory / 'include/openssl'; files.mkdir(headers, parents=True)
                for name in ('evp.h', 'configuration.h', 'opensslconf.h'):
                    files.write_bytes(headers / name, b'')
                files.write_text(stamp, json.dumps(metadata))
                self.assertEqual(openssl.build_openssl(ndk, 'arm64-v8a'), output)

    def test_download_checksum_is_checked_before_source_execution(self):
        with resource_workspace.temporary(SOURCE, 'android-tls-test-') as directory:
            archive = Path(directory) / 'source.tar.gz'
            files.write_bytes(archive, b'local fixture')
            with self.assertRaisesRegex(ValueError, 'checksum mismatch'):
                openssl.verify_archive(archive)
            with patch.object(openssl, 'SHA256', hashlib.sha256(b'local fixture').hexdigest()):
                openssl.verify_archive(archive)

    def test_qt_sonames_preserve_upstream_symbol_versions(self):
        with resource_workspace.temporary(SOURCE, 'android-tls-test-') as directory:
            source = Path(directory)
            configuration = source / 'Configurations/15-android.conf'
            definitions = source / 'util/mkdef.pl'
            files.mkdir(configuration.parent)
            files.mkdir(definitions.parent)
            files.write_text(configuration, '        shared_extension => ".so",\n')
            files.write_text(definitions, ' OPENSSL${SO_VARIANT}_$previous OPENSSL${SO_VARIANT}_$current')
            openssl.patch_qt_sonames(source)
            self.assertIn('shlib_variant => "_3"', configuration.read_text())
            self.assertEqual(definitions.read_text(), ' OPENSSL_$previous OPENSSL_$current')
            with self.assertRaisesRegex(ValueError, 'symbol version'):
                openssl.patch_qt_sonames(source)

    def test_build_keeps_sources_archives_and_metadata_outside_build(self):
        contents = {
            'Configure': b'fixture',
            'Configurations/15-android.conf': b'        shared_extension => ".so",\n',
            'util/mkdef.pl': b' OPENSSL${SO_VARIANT}_$previous OPENSSL${SO_VARIANT}_$current',
            'include/openssl/evp.h': b'public header',
            'LICENSE.txt': b'license',
        }
        buffer = io.BytesIO()
        with tarfile.open(fileobj=buffer, mode='w:gz') as package:
            for name, data in contents.items():
                info = tarfile.TarInfo(f'openssl-{openssl.VERSION}/{name}')
                info.size = len(data)
                package.addfile(info, io.BytesIO(data))
        archive_bytes = buffer.getvalue()
        with resource_workspace.temporary(SOURCE, 'android-tls-test-') as temporary:
            product = Path(temporary) / 'product'
            ndk = product / 'ndk'
            host = 'darwin-x86_64' if openssl.platform.system() == 'Darwin' else 'linux-x86_64'
            tools = ndk / 'toolchains/llvm/prebuilt' / host / 'bin'
            files.mkdir(tools, parents=True)
            files.write_text(ndk / 'source.properties', 'fixture-ndk')
            files.write_bytes(tools / 'clang', b'')
            calls = []

            def run(command, *, cwd=None, env=None, check=False):
                calls.append((command, cwd))
                if command[0] == 'perl':
                    self.assertTrue(Path(command[1]).is_relative_to(product / 'resources/.state'))
                    self.assertTrue(Path(cwd).is_relative_to(product / 'build/android'))
                    self.assertTrue(Path(env['TMPDIR']).is_relative_to(product / 'resources/.state'))
                if command[0] == 'make':
                    for name in ('libcrypto_3.so', 'libssl_3.so'):
                        files.write_bytes(Path(cwd) / name, b'compiled fixture')
                    headers = Path(cwd) / 'include/openssl'
                    files.mkdir(headers, parents=True)
                    for name in ('configuration.h', 'opensslconf.h'):
                        files.write_text(headers / name, 'generated header')

            with patch.object(openssl, 'PRODUCT', product), \
                 patch.object(openssl, 'SHA256', hashlib.sha256(archive_bytes).hexdigest()), \
                 patch.object(openssl.urllib.request, 'urlopen', return_value=io.BytesIO(archive_bytes)) as download, \
                 patch.object(openssl.subprocess, 'run', side_effect=run):
                output = openssl.build_openssl(ndk, 'arm64-v8a')
                self.assertTrue((output / 'libcrypto_3.so').is_file())
                self.assertTrue((output.parent / 'include/openssl/configuration.h').is_file())
                self.assertTrue((product / 'resources/dependencies/android-openssl' /
                                 f'openssl-{openssl.VERSION}.tar.gz').is_file())
                self.assertTrue((product / 'resources/.state/android-openssl/arm64-v8a/build.json').is_file())
                self.assertFalse((output.parent / 'build.log').exists())
                self.assertFalse(list(output.parent.glob('work-*')))
                self.assertFalse(list((product / 'resources/.state/android-openssl').glob('source-*')))
                self.assertEqual(openssl.build_openssl(ndk, 'arm64-v8a'), output)
                self.assertEqual(download.call_count, 1)
                self.assertEqual(len(calls), 4)
                (output.parent / 'include/openssl/configuration.h').unlink()
                def fail_build(command, **kwargs):
                    if command[0] == 'make':
                        raise RuntimeError('fixture build failed')
                with patch.object(openssl.subprocess, 'run', side_effect=fail_build):
                    with self.assertRaisesRegex(RuntimeError, 'fixture build failed'):
                        openssl.build_openssl(ndk, 'arm64-v8a')
                self.assertFalse((product / 'resources/.state/android-openssl/arm64-v8a/build.json').exists())
                self.assertFalse(list(output.parent.glob('work-*')))
                self.assertFalse(list((product / 'resources/.state/android-openssl').glob('source-*')))


if __name__ == '__main__':
    unittest.main()
