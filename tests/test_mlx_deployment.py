"""Check MLX's macOS load commands and its matching packaged Metal library."""
from pathlib import Path
import hashlib
import re
import subprocess
import sys
import unittest


ROOT = Path(__file__).resolve().parents[1]
BUNDLE = ROOT / "build/bin/Society.app"


def minimum_macos(binary, architecture):
    output = subprocess.check_output(["vtool", "-arch", architecture, "-show-build", str(binary)], text=True)
    match = re.search(r"\bminos\s+(\d+(?:\.\d+){1,2})\b", output)
    if not match:
        raise AssertionError(f"Missing macOS build version: {binary}")
    parts = tuple(int(part) for part in match.group(1).split("."))
    return parts + (0,) * (3 - len(parts))


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def mach_o_uuids(path):
    output = subprocess.check_output(["dwarfdump", "--uuid", str(path)], text=True)
    return set(re.findall(r"UUID: ([0-9A-Fa-f-]+) \(([^)]+)\)", output))


@unittest.skipUnless(sys.platform == "darwin", "Mach-O checks require macOS")
class MlxDeploymentTests(unittest.TestCase):
    def test_mlx_minimum_does_not_exceed_the_application(self):
        app = BUNDLE / "Contents/MacOS/Society"
        mlx = BUNDLE / "Contents/Frameworks/libmlx.dylib"
        self.assertTrue(app.is_file(), app)
        self.assertTrue(mlx.is_file(), mlx)
        app_architectures = {arch for _, arch in mach_o_uuids(app)}
        mlx_architectures = {arch for _, arch in mach_o_uuids(mlx)}
        self.assertTrue(app_architectures, "Society has no Mach-O architectures")
        self.assertTrue(app_architectures <= mlx_architectures,
                        "MLX does not support every Society architecture")
        for architecture in app_architectures:
            with self.subTest(architecture=architecture):
                self.assertLessEqual(minimum_macos(mlx, architecture), minimum_macos(app, architecture),
                                     "MLX requires a newer macOS than Society")

    def test_metal_library_matches_the_selected_mlx_build(self):
        cache = (ROOT / "build/CMakeCache.txt").read_text(encoding="utf-8")
        match = re.search(r"^SOCIETY_MLX_RUNTIME_DIR:PATH=(.+)$", cache, re.MULTILINE)
        if not match or not match.group(1):
            self.skipTest("No explicit MLX runtime directory was selected")
        runtime = Path(match.group(1))
        source = runtime / "mlx.metallib"
        resource = BUNDLE / "Contents/Resources/mlx.metallib"
        colocated = BUNDLE / "Contents/Frameworks/mlx.metallib"
        self.assertTrue(source.is_file(), source)
        self.assertTrue(resource.is_file(), resource)
        self.assertTrue(colocated.is_symlink(), colocated)
        self.assertEqual(colocated.resolve(), resource.resolve())
        self.assertEqual(sha256(source), sha256(resource))
        selected = mach_o_uuids(runtime / "libmlx.dylib")
        self.assertTrue(selected, "Selected MLX has no Mach-O UUID")
        bundled = mach_o_uuids(BUNDLE / "Contents/Frameworks/libmlx.dylib")
        self.assertTrue(bundled, "Bundled MLX has no Mach-O UUID")
        self.assertTrue(bundled <= selected, "Bundled MLX came from another build")


if __name__ == "__main__":
    unittest.main()
