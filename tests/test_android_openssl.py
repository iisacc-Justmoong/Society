"""Reject untrusted source archives and verify the Qt Android TLS naming contract."""
import hashlib
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

SOURCE = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('android_openssl', SOURCE / 'tools/build_android_openssl.py')
openssl = importlib.util.module_from_spec(spec)
spec.loader.exec_module(openssl)


class AndroidTlsBuildTest(unittest.TestCase):
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
