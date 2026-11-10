"""Check native Society LVUI sources with a selected host compiler."""

import argparse
import os
from pathlib import Path
import subprocess
import sys
import unittest


ROOT = Path(__file__).resolve().parents[1]
SDK_COPY = ROOT / "resources/dependencies/lvui-native-model-chooser"
SOURCES = (
    ROOT / "App/Society/Native/NativeFiles.lvui",
    ROOT / "App/Society/Tools/NativeModelChooser.lvui",
    ROOT / "App/Society/Tools/NativeModelCalibrator.lvui",
)
EXAMPLE_DIR = SDK_COPY / "share/LVUI/docs/examples"


class NativeLVUISyntaxTests(unittest.TestCase):
    compiler = None

    def check_source(self, source):
        result = subprocess.run(
            [str(self.compiler), "--framework", "iiGUI", "--name", "SyntaxCheck", str(source)],
            stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True, check=False,
        )
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_native_app_views(self):
        for source in SOURCES:
            with self.subTest(source=source):
                self.check_source(source)

    def test_bundled_sdk_examples_when_present(self):
        if not EXAMPLE_DIR.is_dir():
            self.skipTest("The optional staged SDK documentation is not present")
        for name in ("Counter.lvui", "ReactiveCounter.lvui"):
            with self.subTest(name=name):
                self.check_source(EXAMPLE_DIR / name)

    def test_bundled_keyword_catalog_example_when_present(self):
        if not EXAMPLE_DIR.is_dir():
            self.skipTest("The optional staged SDK documentation is not present")
        checker = self.compiler.with_name("lvui-check")
        if not checker.is_file():
            self.skipTest("The selected SDK has no adjacent syntax checker")
        result = subprocess.run([str(checker), str(EXAMPLE_DIR / "ReservedComponents.lvui")],
                                stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
                                text=True, check=False)
        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", type=Path,
                        default=Path(os.environ.get("LVUI_HOST_COMPILER") or
                                     SDK_COPY / "bin/lvui-compile"))
    options, remaining = parser.parse_known_args()
    if not options.compiler.is_file():
        parser.error(f"LVUI host compiler does not exist: {options.compiler}")
    NativeLVUISyntaxTests.compiler = options.compiler
    unittest.main(argv=[sys.argv[0], *remaining])
