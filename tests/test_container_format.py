"""Verify the generated macOS plist declares the Society container package."""
import pathlib
import plistlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class ContainerFormatTests(unittest.TestCase):
    def test_generated_app_preserves_links_and_declares_package(self):
        with tempfile.TemporaryDirectory(prefix="container-format-", dir=ROOT / "build") as folder:
            source = pathlib.Path(folder)
            (source / "CMakeLists.txt").write_text(
                'cmake_minimum_required(VERSION 3.24)\n'
                'project(ContainerFormat NONE)\n'
                'set(APPLE TRUE)\n'
                'add_library(Society INTERFACE)\n'
                f'include("{ROOT / "cmake/ApplicationLinks.cmake"}")\n'
                'society_add_application_links(Society)\n'
            )
            subprocess.run(["cmake", "-S", str(source), "-B", str(source / "build")],
                           check=True, capture_output=True, text=True)
            plist = plistlib.loads((source / "build/Society-ApplicationLinks.plist.in").read_bytes())
            self.assertEqual(plist["CFBundleURLTypes"][0]["CFBundleURLSchemes"], ["society"])
            exported = plist["UTExportedTypeDeclarations"][0]
            self.assertEqual(exported["UTTypeIdentifier"], "com.iisacc.society.container")
            self.assertEqual(exported["UTTypeConformsTo"], ["com.apple.package"])
            self.assertEqual(exported["UTTypeTagSpecification"]["public.filename-extension"], ["societycontainer"])
            document = plist["CFBundleDocumentTypes"][0]
            self.assertTrue(document["LSTypeIsPackage"])
            self.assertEqual(document["LSItemContentTypes"], [exported["UTTypeIdentifier"]])
            self.assertEqual(document["CFBundleTypeName"], "Society Container")


if __name__ == "__main__":
    unittest.main()
