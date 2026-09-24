"""Verify the GUI and daemon with no development library or QML search paths."""
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time


def check_loaded(log, bundle):
    loaded = re.findall(r"^dyld\[\d+\]: <[^>]+> (/.+)$", log, re.MULTILINE)
    assert loaded, "No dynamic loader evidence was captured."
    external = [path for path in loaded if not path.startswith((str(bundle) + "/", "/System/", "/usr/lib/"))]
    assert not external, "Dependencies outside the app bundle: " + ", ".join(external)
    return len(loaded)


def verify(bundle, gui=False):
    bundle = bundle.resolve()
    env = {key: value for key, value in os.environ.items()
           if not key.startswith(("DYLD_", "QT_", "QML", "QSG_", "SOCIETY_", "IISOCIETYSYNC_"))}
    env["DYLD_PRINT_LIBRARIES"] = "1"
    executable = bundle / "Contents/MacOS/Society"
    result = subprocess.run([str(executable), "--daemon-service", "status"], env=env,
                            capture_output=True, text=True, timeout=30)
    assert result.returncode == 0, result.stderr[-5000:]
    assert json.loads(result.stdout)["ok"], result.stdout
    report = {"guiLoaderLibraries": check_loaded(result.stderr, bundle)}
    helper = bundle / "Contents/MacOS/SocietyDaemon"
    result = subprocess.run([str(helper), "--check-runtime"], env=env,
                            capture_output=True, text=True, timeout=30)
    assert result.returncode == 0, result.stderr[-5000:]
    capabilities = json.loads(result.stdout)
    assert capabilities["sqlite"] and capabilities["tls"], capabilities
    report["daemonLoaderLibraries"] = check_loaded(result.stderr, bundle)
    if gui:
        with tempfile.TemporaryDirectory(prefix="gui-runtime-", dir=bundle.parent) as directory:
            fixture = Path(directory)
            env.update({"SOCIETY_STORAGE_SETTINGS_PATH": str(fixture / "storage.json"),
                        "SOCIETY_HELPER_DIRECTORY": str(fixture / "helper"),
                        "SOCIETY_DISABLE_SESSION_RESTORE": "1", "SOCIETY_DISABLE_PHOTOS": "1",
                        "IISOCIETYSYNC_DISABLE_BLE": "1", "QT_QPA_PLATFORM": "offscreen"})
            log_path = fixture / "launch.log"
            with log_path.open("w") as log:
                process = subprocess.Popen([str(executable)], env=env, stdout=log, stderr=subprocess.STDOUT)
                try:
                    deadline = time.monotonic() + 30
                    while time.monotonic() < deadline and process.poll() is None:
                        if "bootstrap.entry.root-loaded" in log_path.read_text():
                            break
                        time.sleep(0.1)
                    time.sleep(0.5)
                    output = log_path.read_text()
                    assert process.poll() is None and "bootstrap.entry.root-loaded" in output, output[-7000:]
                    assert "failed to load component" not in output.lower(), output[-7000:]
                    report["qmlLoaderLibraries"] = check_loaded(output, bundle)
                    report["noContainerStartup"] = not (fixture / "storage.json").exists()
                    assert report["noContainerStartup"]
                finally:
                    if process.poll() is None:
                        process.terminate()
                        process.wait(timeout=10)
    return report


if __name__ == "__main__":
    print(json.dumps(verify(Path(sys.argv[1]), "--gui" in sys.argv[2:]), indent=2))
