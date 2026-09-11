"""Reject untrusted source archives and verify the Qt Android TLS naming contract."""
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

SOURCE = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('android_openssl', SOURCE / 'tools/build_android_openssl.py')
openssl = importlib.util.module_from_spec(spec)
spec.loader.exec_module(openssl)


class AndroidTlsBuildTest(unittest.TestCase):
    def test_runtime_cache_requires_matching_public_headers(self):
        with tempfile.TemporaryDirectory(dir=SOURCE / 'build') as temporary:
            product = Path(temporary)
            ndk = product / 'ndk'; ndk.mkdir(); (ndk / 'source.properties').write_text('fixture-ndk')
            directory = product / 'build/android' / f'openssl-{openssl.VERSION}' / 'arm64-v8a'
            output = directory / 'lib'; output.mkdir(parents=True)
            for name in ('libcrypto_3.so', 'libssl_3.so'):
                (output / name).touch()
            (directory / 'build.json').write_text(json.dumps({
                'version': openssl.VERSION, 'sha256': openssl.SHA256, 'abi': 'arm64-v8a', 'api': 28,
                'ndk': str(ndk.resolve()), 'ndkRevision': 'fixture-ndk',
                'builder': hashlib.sha256(Path(openssl.__file__).read_bytes()).hexdigest()}))
            (product / 'build/android' / f'openssl-{openssl.VERSION}.tar.gz').touch()
            with patch.object(openssl, 'PRODUCT', product), patch.object(openssl, 'verify_archive', side_effect=RuntimeError('rebuild-required')):
                with self.assertRaisesRegex(RuntimeError, 'rebuild-required'):
                    openssl.build_openssl(ndk, 'arm64-v8a')
                headers = directory / 'include/openssl'; headers.mkdir(parents=True)
                for name in ('evp.h', 'configuration.h', 'opensslconf.h'):
                    (headers / name).touch()
                self.assertEqual(openssl.build_openssl(ndk, 'arm64-v8a'), output)

    def test_download_checksum_is_checked_before_source_execution(self):
        with tempfile.TemporaryDirectory(dir=SOURCE / 'build') as directory:
            archive = Path(directory) / 'source.tar.gz'
            archive.write_bytes(b'local fixture')
            with self.assertRaisesRegex(ValueError, 'checksum mismatch'):
                openssl.verify_archive(archive)
            with patch.object(openssl, 'SHA256', hashlib.sha256(b'local fixture').hexdigest()):
                openssl.verify_archive(archive)

    def test_qt_sonames_preserve_upstream_symbol_versions(self):
        with tempfile.TemporaryDirectory(dir=SOURCE / 'build') as directory:
            source = Path(directory)
            configuration = source / 'Configurations/15-android.conf'
            definitions = source / 'util/mkdef.pl'
            configuration.parent.mkdir()
            definitions.parent.mkdir()
            configuration.write_text('        shared_extension => ".so",\n')
            definitions.write_text(' OPENSSL${SO_VARIANT}_$previous OPENSSL${SO_VARIANT}_$current')
            openssl.patch_qt_sonames(source)
            self.assertIn('shlib_variant => "_3"', configuration.read_text())
            self.assertEqual(definitions.read_text(), ' OPENSSL_$previous OPENSSL_$current')
            with self.assertRaisesRegex(ValueError, 'symbol version'):
                openssl.patch_qt_sonames(source)


if __name__ == '__main__':
    unittest.main()
