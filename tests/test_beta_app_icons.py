"""Check independent icon variants and their actual macOS CMake selection."""
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
ICONS = ROOT / "resources/Appicon"
BETA = ICONS / "Beta/generated"
SCRIPT = r"""
set(APPLE TRUE)
set(CMAKE_CURRENT_SOURCE_DIR "${TEST_ROOT}")
function(set_source_files_properties)
endfunction()
function(qt_add_resources)
    set_property(GLOBAL PROPERTY runtime_icon "${ARGN}")
endfunction()
function(target_sources)
    set_property(GLOBAL PROPERTY bundle_icon "${ARGN}")
endfunction()
function(set_target_properties)
endfunction()
include("${TEST_ROOT}/cmake/AppIcons.cmake")
society_add_app_icons(probe)
get_property(runtime GLOBAL PROPERTY runtime_icon)
get_property(bundle GLOBAL PROPERTY bundle_icon)
set(expected_runtime "${TEST_EXPECTED}/macos/Society.png")
set(expected_bundle "${TEST_EXPECTED}/macos/Society.icns")
string(FIND "${runtime}" "${expected_runtime}" runtime_pos)
string(FIND "${bundle}" "${expected_bundle}" bundle_pos)
if(runtime_pos EQUAL -1 OR bundle_pos EQUAL -1)
    message(FATAL_ERROR "Incorrect selected icons: runtime=${runtime}; bundle=${bundle}")
endif()
"""


class IconVariantTests(unittest.TestCase):
    """Verify that variant selection does not alter the regular brand assets."""

    def assert_selection(self, expected, override):
        """Configure the existing icon function with its default or an override."""
        state = ROOT / "resources/.state"
        state.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="beta-icon-selection-", dir=state) as temp:
            script = Path(temp) / "selection.cmake"
            script.write_text(SCRIPT)
            command = ["cmake", f"-DTEST_ROOT={ROOT}", f"-DTEST_EXPECTED={expected}"]
            if override:
                command.append(f"-DSOCIETY_APP_ICON_DIRECTORY={expected}")
            command += ["-P", str(script)]
            result = subprocess.run(command, cwd=ROOT, text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_default_and_beta_resolve_independently(self):
        """The default stays regular while a beta override selects beta assets."""
        self.assert_selection(ICONS / "generated", False)
        self.assert_selection(BETA, True)

    def test_beta_derivatives_record_their_actual_master(self):
        """The beta manifest identifies and hashes the actual supplied master."""
        source = ICONS / "Beta/Society-beta-concept.png"
        manifest = json.loads((BETA / "manifest.json").read_text())
        self.assertEqual(manifest["source"], source.name)
        self.assertEqual(manifest["source_sha256"], hashlib.sha256(source.read_bytes()).hexdigest())
        for name in ("common/Society.png", "macos/Society.png",
                     "macos/Society.icns", "windows/Society.ico"):
            self.assertEqual(hashlib.sha256((BETA / name).read_bytes()).hexdigest(),
                             manifest["files"][name]["sha256"])


if __name__ == "__main__":
    unittest.main()

