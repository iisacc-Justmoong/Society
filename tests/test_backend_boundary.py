"""Keep presentation libraries out of the background process."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location("runtime", Path(__file__).with_name("verify_macos_runtime.py"))
runtime = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runtime)


def loader(*names):
    return "\n".join("dyld[123]: <1234> /Applications/Society.app/Contents/Frameworks/" + name
                     for name in names)


class BackendBoundaryTests(unittest.TestCase):
    def test_existing_codec_and_network_adapters_are_allowed(self):
        self.assertEqual(runtime.check_daemon_presentation(loader(
            "QtCore.framework/Versions/A/QtCore", "QtGui.framework/Versions/A/QtGui",
            "QtNetwork.framework/Versions/A/QtNetwork", "libiiSharedCanvas.0.31.dylib")), [])

    def test_quick_library_is_rejected(self):
        with self.assertRaisesRegex(AssertionError, "QtQuick"):
            runtime.check_daemon_presentation(loader("QtQuick.framework/Versions/A/QtQuick"))

    def test_transitive_qml_modules_are_rejected(self):
        for name in ("QtQml", "QtQmlModels", "QtQmlMeta", "QtQuickControls2"):
            with self.subTest(name=name), self.assertRaises(AssertionError):
                runtime.check_daemon_presentation(loader(name + ".framework/Versions/A/" + name))

    def test_canvas_quick_adapter_is_rejected(self):
        with self.assertRaises(AssertionError):
            runtime.check_daemon_presentation(loader("libiiSharedCanvasQuick.0.31.dylib"))


if __name__ == "__main__":
    unittest.main()
