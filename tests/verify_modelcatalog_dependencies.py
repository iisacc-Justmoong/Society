"""Verify the catalog regression needs no presentation or inference runtime."""
from pathlib import Path
import subprocess
import sys


def verify(executable: Path) -> None:
    assert executable.is_file(), executable
    libraries = subprocess.check_output(["/usr/bin/otool", "-L", str(executable)], text=True)
    for name in ("QtQuick", "QtQml", "libLVRS", "libiiLocalDiffusion", "libiiMultimodalAgent"):
        assert name not in libraries, libraries
    print("Catalog executable links neither Qt Quick/QML nor inference libraries")


if __name__ == "__main__":
    verify(Path(sys.argv[1]))
