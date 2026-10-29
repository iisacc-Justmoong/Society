"""Exercise the Core component's actual Mach-O lookup without loader overrides."""
from pathlib import Path
import os
import re
import subprocess
import sys


def verify(executable: Path, core_library: Path) -> None:
    commands = subprocess.check_output(["/usr/bin/otool", "-l", str(executable)], text=True)
    rpaths = re.findall(r"cmd LC_RPATH\s+cmdsize \d+\s+path (.*?) \(offset", commands)
    assert str(core_library.parent) in rpaths, (core_library, rpaths)
    libraries = subprocess.check_output(["/usr/bin/otool", "-L", str(executable)], text=True)
    for name in ("iiLocalDiffusion", "libtorch", "libc10"):
        assert name not in libraries, libraries
    environment = os.environ.copy()
    for name in ("DYLD_LIBRARY_PATH", "DYLD_FRAMEWORK_PATH", "DYLD_FALLBACK_LIBRARY_PATH", "DYLD_FALLBACK_FRAMEWORK_PATH"):
        environment.pop(name, None)
    result = subprocess.run([str(executable.resolve()), "-functions"], env=environment,
                            capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stdout + result.stderr
    assert result.stdout.strip(), "No QtTest functions were listed"
    print("Core runtime lookup and process startup passed without inference links or DYLD overrides")


if __name__ == "__main__":
    verify(Path(sys.argv[1]), Path(sys.argv[2]))
