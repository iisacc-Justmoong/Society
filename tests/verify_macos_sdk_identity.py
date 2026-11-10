"""Verify that a bundle contains the SDK binaries selected at configure time."""
import argparse
from pathlib import Path
import re
import subprocess


def architecture_uuids(binary):
    output = subprocess.check_output(["dwarfdump", "--uuid", str(binary)], text=True)
    return {architecture: uuid.upper() for uuid, architecture in re.findall(
        r"^UUID:\s+([0-9A-Fa-f-]+)\s+\(([^)]+)\)", output, re.MULTILINE)}


def verify_pair(source, bundled):
    expected = architecture_uuids(source)
    actual = architecture_uuids(bundled)
    # Release deployment may remove unused architectures, but it must not
    # replace the remaining architecture with another SDK build.
    if not actual or any(expected.get(arch) != uuid for arch, uuid in actual.items()):
        raise RuntimeError(f"Bundled SDK does not match selected SDK: {bundled} != {source}")


def verify_library(app, source):
    output = subprocess.check_output(["otool", "-D", str(source)], text=True)
    identities = [line.strip() for line in output.splitlines()[1:] if line.strip()]
    if len(identities) != 1:
        raise RuntimeError(f"Expected one SDK install name: {source}")
    verify_pair(source, app / "Contents/Frameworks" / Path(identities[0]).name)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", type=Path, required=True)
    parser.add_argument("--library", type=Path, action="append", required=True)
    args = parser.parse_args()
    for library in args.library:
        verify_library(args.app, library)


if __name__ == "__main__":
    main()
