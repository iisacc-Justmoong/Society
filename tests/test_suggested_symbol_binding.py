#!/usr/bin/env python3
"""Check that macOS binds the catalog lifetime to one provider ABI."""
import re
import subprocess
import sys


def check(path: str) -> None:
    symbols = subprocess.check_output(["/usr/bin/nm", "-m", path], text=True)
    catalog = [line for line in symbols.splitlines()
               if re.search(r"FileCatalog(?:C[12]|D[12])E", line)
               and "(undefined)" in line]
    if not catalog or not any(re.search(r"FileCatalogD[12]E", line) for line in catalog):
        raise AssertionError(f"{path}: FileCatalog lifetime symbols are missing")
    wrong = [line for line in catalog if "(from libiiFileProviderCatalog)" not in line]
    if wrong:
        raise AssertionError(f"{path}: mixed FileCatalog ABI bindings: {wrong}")


if __name__ == "__main__":
    for binary in sys.argv[1:]:
        check(binary)
        print(f"{binary}: catalog ABI binding verified")
