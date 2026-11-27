"""The macOS release must reject a bundled library missing a required import.

These fixtures compile Mach-O images but never execute them.  In particular,
replacing a provider by another library with the same install name must not
hide a missing symbol, while an explicitly weak import may remain absent.
"""

import importlib.util
import shutil
import subprocess
import sys
import unittest
from pathlib import Path
from unittest.mock import patch

import resource_workspace


ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "deployment_symbol_test", ROOT / "cmake/single-app/deploy_macos.py"
)
deployment = importlib.util.module_from_spec(spec)
spec.loader.exec_module(deployment)


@unittest.skipUnless(sys.platform == "darwin", "Mach-O deployment is macOS-only")
class SymbolBindingTests(unittest.TestCase):
    """Build disposable images to exercise the release-time loader check."""

    def setUp(self):
        """Create isolated resources, build output, and an unexecuted host image."""
        inputs = resource_workspace.temporary(ROOT, "symbol-binding-input-")
        self.inputs = inputs.__enter__()
        self.addCleanup(inputs.__exit__, None, None, None)
        output = resource_workspace.build(ROOT, "symbol-binding-build-")
        self.root = output.__enter__()
        self.addCleanup(output.__exit__, None, None, None)
        self.app = self.root / "Fixture.app"
        self.frameworks = self.app / "Contents/Frameworks"
        self.frameworks.mkdir(parents=True)
        executable = self.app / "Contents/MacOS/Fixture"
        executable.parent.mkdir(parents=True)
        self.compile("main.c", "int main(void) { return 0; }\n", "-o", executable)

    def compile(self, name, source, *arguments):
        """Compile a fixture from resources into this test's build directory."""
        path = self.inputs / name
        path.write_text(source)
        subprocess.run(["cc", str(path), *map(str, arguments)], check=True,
                       capture_output=True, text=True)

    def library(self, source_name, source, output):
        """Compile a provider with a stable install name."""
        self.compile(source_name, source, "-dynamiclib", "-o", output,
                     "-install_name", "@loader_path/libprovider.dylib")

    def consumer(self, source):
        """Link a client to the original provider without running it."""
        self.compile("consumer.c", source, "-dynamiclib",
                     self.frameworks / "libprovider.dylib", "-o",
                     self.frameworks / "libconsumer.dylib",
                     "-install_name", "@loader_path/libconsumer.dylib")

    def replace_provider(self):
        """Replace the provider with an ABI-incompatible image of the same name."""
        replacement = self.root / "replacement.dylib"
        self.library("replacement.c", "int unrelated(void) { return 1; }\n", replacement)
        shutil.copy2(replacement, self.frameworks / "libprovider.dylib")

    def test_library_labels_match_dyld_version_abbreviations(self):
        """Recognize versioned dylib labels in dyld_info output."""
        self.assertEqual(deployment.macho_library_label("@loader_path/libx.0.4.dylib"), "libx.0")
        self.assertEqual(deployment.macho_library_label("@loader_path/libx.0.56.dylib"), "libx.0.56")
        self.assertEqual(deployment.macho_library_label("/usr/lib/libSystem.B.dylib"), "libSystem")

    def test_rejects_incomplete_tool_output(self):
        """A changed or truncated inspection format must fail closed."""
        with patch.object(deployment, "run", return_value="unexpected tool output"):
            with self.assertRaisesRegex(RuntimeError, "Incomplete Mach-O inspection"):
                deployment.macho_sections(self.app, "arm64")

    def test_rejects_missing_required_symbol_after_provider_replacement(self):
        """A same-name, ABI-incompatible replacement must stop deployment."""
        self.library("provider.c", "int required(void) { return 1; }\n",
                     self.frameworks / "libprovider.dylib")
        self.consumer("extern int required(void); int use(void) { return required(); }\n")
        deployment.verify_symbol_bindings(self.app)
        self.replace_provider()
        with self.assertRaisesRegex(RuntimeError, r"libconsumer.*libprovider.*_required"):
            deployment.verify_symbol_bindings(self.app)

    def test_accepts_absent_weak_import(self):
        """A weak import may be absent without preventing image loading."""
        self.library("provider.c", "int optional(void) { return 1; }\n",
                     self.frameworks / "libprovider.dylib")
        self.consumer("extern int optional(void) __attribute__((weak_import)); "
                      "int use(void) { return optional ? optional() : 0; }\n")
        self.replace_provider()
        deployment.verify_symbol_bindings(self.app)

    def test_follows_reexported_provider(self):
        """Resolve re-exported symbols and detect their removal from the provider."""
        self.library("provider.c", "int required(void) { return 1; }\n",
                     self.frameworks / "libprovider.dylib")
        self.compile("wrapper.c", "extern int required(void); int wrapper_use(void) { return required(); }\n", "-dynamiclib",
                     "-Wl,-reexport_library," + str(self.frameworks / "libprovider.dylib"),
                     "-o", self.frameworks / "libwrapper.dylib", "-install_name",
                     "@loader_path/libwrapper.dylib")
        self.compile("consumer.c", "extern int required(void); int use(void) { return required(); }\n",
                     "-dynamiclib", self.frameworks / "libwrapper.dylib", "-o",
                     self.frameworks / "libconsumer.dylib", "-install_name",
                     "@loader_path/libconsumer.dylib")
        deployment.verify_symbol_bindings(self.app)
        self.replace_provider()
        with self.assertRaisesRegex(RuntimeError, r"_required"):
            deployment.verify_symbol_bindings(self.app)


if __name__ == "__main__":
    unittest.main()
