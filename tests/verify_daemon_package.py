"""Run the bundled daemon and reject runtime dependencies outside its app bundle."""
import json
import os
from pathlib import Path
import plistlib
import re
import subprocess
import sys


bundle = Path(sys.argv[1]).resolve()
source = Path(sys.argv[2]).resolve()
def links(executable):
    lines = subprocess.check_output(["otool", "-L", str(executable)], text=True).splitlines()
    return [line.strip() for line in lines if line[:1].isspace()]
assert links(bundle / "Contents/MacOS/Society") == links(source / "Contents/MacOS/Society"), \
    "Daemon packaging partially rewrote the GUI's Qt/SDK library graph."
with (bundle / "Contents/Library/LaunchAgents/com.iisacc.society.daemon.plist").open("rb") as file:
    agent = plistlib.load(file)
assert agent["Label"] == "com.iisacc.society.daemon"
assert agent["RunAtLoad"] and agent["KeepAlive"]
executable = bundle / agent["BundleProgram"]
environment = {key: value for key, value in os.environ.items()
               if not key.startswith(("DYLD_", "QT_", "SOCIETY_HELPER_"))}
environment["DYLD_PRINT_LIBRARIES"] = "1"
# Arbitrary external-volume folders can require macOS app consent. Validate the
# actual bundle loader without user data. Receipt/recovery has separate build/ tests.
result = subprocess.run([str(executable), "--help"], env=environment,
                        capture_output=True, text=True, timeout=15)
assert result.returncode == 0, result.stderr[-4000:]
loaded = re.findall(r"^dyld\[\d+\]: <[^>]+> (/.+)$", result.stderr, re.MULTILINE)
assert loaded, "No dynamic loader evidence was captured."
external = [path for path in loaded
            if not path.startswith((str(bundle) + "/", "/System/", "/usr/lib/"))]
assert not external, "Daemon loaded dependencies outside its bundle: " + ", ".join(external)
helper_contents = executable.parent.parent
plugin = helper_contents / "PlugIns/sqldrivers/libqsqlite.dylib"
dependencies = subprocess.check_output(["otool", "-L", str(plugin)], text=True).splitlines()[1:]
for line in dependencies:
    if not line[:1].isspace():
        continue
    path = line.strip().split(" (compatibility", 1)[0]
    if path.startswith(("/System/", "/usr/lib/")):
        continue
    for prefix, base in (("@rpath/", helper_contents / "Frameworks"),
                         ("@loader_path/", plugin.parent),
                         ("@executable_path/", executable.parent)):
        if path.startswith(prefix):
            path = str((base / path[len(prefix):]).resolve())
            break
    assert path.startswith(str(bundle) + "/") and Path(path).is_file(), path
print(json.dumps({"bundledDaemon": str(executable), "externalDependencies": external,
                  "sqliteDriverDependencies": "passed"}))
