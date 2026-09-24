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
assert not list(bundle.rglob("*.app")), "Nested application bundles are forbidden"
expected_container = Path(sys.argv[3]).resolve() if len(sys.argv) > 3 else None
expected_file_actions = Path(sys.argv[4]).resolve() if len(sys.argv) > 4 else None
packaged_container = bundle / 'Contents/Frameworks/libiiSocietyContainer.0.dylib'
def binary_uuids(path):
    output = subprocess.check_output(['dwarfdump', '--uuid', str(path)], text=True)
    return re.findall(r'UUID: ([0-9A-F-]+) \(([^)]+)\)', output)
if expected_container:
    assert binary_uuids(expected_container) and binary_uuids(packaged_container) == binary_uuids(expected_container), \
        'The bundled model classifier differs from the iiSocietyContainer selected at build time.'
if expected_file_actions:
    packaged_actions = bundle / 'Contents/Frameworks/libiiSocietyContainerGui.0.dylib'
    assert binary_uuids(expected_file_actions) and binary_uuids(packaged_actions) == binary_uuids(expected_file_actions), \
        'The bundled file actions differ from the iiSocietyContainer Gui selected at build time.'
with (bundle / 'Contents/Info.plist').open('rb') as file:
    group = plistlib.load(file)['SocietyAppGroup']
entitlements = plistlib.loads(subprocess.check_output(['codesign', '-d', '--entitlements', '-', '--xml', str(bundle)], stderr=subprocess.DEVNULL))
assert group in entitlements.get('com.apple.security.application-groups', []), 'Packaging dropped the Society App Group entitlement.'
def links(executable):
    lines = subprocess.check_output(["otool", "-L", str(executable)], text=True).splitlines()
    return [line.strip() for line in lines if line[:1].isspace()]
assert links(bundle / "Contents/MacOS/Society") == links(source / "Contents/MacOS/Society"), \
    "Daemon packaging partially rewrote the GUI's Qt/SDK library graph."
with (bundle / "Contents/Library/LaunchAgents/com.iisacc.society.daemon.plist").open("rb") as file:
    agent = plistlib.load(file)
assert agent["Label"] == "com.iisacc.society.daemon"
assert agent["RunAtLoad"] and agent["KeepAlive"]
assert "--sync" in agent["ProgramArguments"]
executable = bundle / agent["BundleProgram"]
helper_entitlements = plistlib.loads(subprocess.check_output(['codesign', '-d', '--entitlements', '-', '--xml', str(executable)], stderr=subprocess.DEVNULL))
assert group in helper_entitlements.get('com.apple.security.application-groups', []), 'Daemon cannot restore the shared account state.'
environment = {key: value for key, value in os.environ.items()
               if not key.startswith(("DYLD_", "QT_", "SOCIETY_HELPER_"))}
environment["DYLD_PRINT_LIBRARIES"] = "1"
# Arbitrary external-volume folders can require macOS app consent. Validate the
# actual bundle loader without user data. Receipt/recovery has separate build/ tests.
result = subprocess.run([str(executable), "--help"], env=environment,
                        capture_output=True, text=True, timeout=60)
assert result.returncode == 0, result.stderr[-4000:]
assert '--sync' in result.stdout, 'The packaged service has no synchronization runtime.'
capabilities = subprocess.run([str(executable), '--check-runtime'], env=environment, capture_output=True, text=True, timeout=60)
assert capabilities.returncode == 0, capabilities.stderr[-4000:]
assert json.loads(capabilities.stdout)['tls'] and json.loads(capabilities.stdout)['sqlite']
loaded = re.findall(r"^dyld\[\d+\]: <[^>]+> (/.+)$", result.stderr, re.MULTILINE)
assert loaded, "No dynamic loader evidence was captured."
external = [path for path in loaded
            if not path.startswith((str(bundle) + "/", "/System/", "/usr/lib/"))]
assert not external, "Daemon loaded dependencies outside its bundle: " + ", ".join(external)
helper_contents = executable.parent.parent
native_libraries = list((helper_contents / "Frameworks").glob("*.dylib"))
for library in native_libraries:
    assert library.resolve().is_relative_to(bundle), f'Bundled library redirects outside the app: {library}'
    for dependency in links(library):
        path = dependency.split(" (compatibility", 1)[0]
        if path.startswith("@rpath/") and path.endswith(".dylib"):
            destination = helper_contents / "Frameworks" / path.removeprefix("@rpath/")
            assert destination.is_file(), f'Missing transitive native library: {library.name} -> {path}'
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
                  "sqliteDriverDependencies": "passed", "modelClassifierUuids": binary_uuids(packaged_container),
                  "bundledNativeLibraries": sorted(path.name for path in native_libraries)}))
