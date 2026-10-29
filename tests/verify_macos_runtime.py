"""Verify the GUI and daemon with no development library or QML search paths."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import time


def check_loaded(log, bundle, lazy_runtime_names=()):
    loaded = re.findall(r"^dyld\[\d+\]: <[^>]+> (/.+)$", log, re.MULTILINE)
    assert loaded, "No dynamic loader evidence was captured."
    external = [path for path in loaded if not path.startswith((str(bundle) + "/", "/System/", "/usr/lib/"))]
    assert not external, "Dependencies outside the app bundle: " + ", ".join(external)
    retired = [path for path in loaded if "onnx" in Path(path).name.lower()
               or "opencv_dnn" in Path(path).name.lower()
               or Path(path).name.lower().startswith("libpiper.")]
    assert not retired, "Retired model runtime loaded: " + ", ".join(retired)
    eager = [path for path in loaded if Path(path).name in lazy_runtime_names]
    assert not eager, "Optional inference runtime initialized without a request: " + ", ".join(eager)
    return len(loaded)


def check_daemon_presentation(log):
    loaded = re.findall(r"^dyld\[\d+\]: <[^>]+> (/.+)$", log, re.MULTILINE)
    presentation = [path for path in loaded if re.search(
        r"/(?:QtQuick|QtQml)[^/]*\.framework/|/libiiSharedCanvasQuick[^/]*\.dylib$", path)]
    assert not presentation, "Presentation dependencies in SocietyDaemon: " + ", ".join(presentation)
    return presentation


def verify(bundle, gui=False, lazy_runtime_names=()):
    bundle = bundle.resolve()
    env = {key: value for key, value in os.environ.items()
           if not key.startswith(("DYLD_", "QT_", "QML", "QSG_", "SOCIETY_", "IISOCIETYSYNC_"))}
    env["DYLD_PRINT_LIBRARIES"] = "1"
    executable = bundle / "Contents/MacOS/Society"
    result = subprocess.run([str(executable), "--daemon-service", "status"], env=env,
                            capture_output=True, text=True, timeout=90)
    assert result.returncode == 0, result.stderr[-5000:]
    assert json.loads(result.stdout)["ok"], result.stdout
    report = {"guiLoaderLibraries": check_loaded(result.stderr, bundle, lazy_runtime_names),
              "lazyRuntimeNames": list(lazy_runtime_names)}
    helper = bundle / "Contents/MacOS/SocietyDaemon"
    result = subprocess.run([str(helper), "--check-runtime"], env=env,
                            capture_output=True, text=True, timeout=90)
    assert result.returncode == 0, result.stderr[-5000:]
    capabilities = json.loads(result.stdout)
    assert capabilities["sqlite"] and capabilities["tls"], capabilities
    report["daemonLoaderLibraries"] = check_loaded(result.stderr, bundle, lazy_runtime_names)
    report["daemonPresentationLibraries"] = check_daemon_presentation(result.stderr)
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
                    deadline = time.monotonic() + 90
                    while time.monotonic() < deadline and process.poll() is None:
                        if "bootstrap.entry.root-loaded" in log_path.read_text():
                            break
                        time.sleep(0.1)
                    time.sleep(0.5)
                    output = log_path.read_text()
                    assert process.poll() is None and "bootstrap.entry.root-loaded" in output, output[-7000:]
                    assert "failed to load component" not in output.lower(), output[-7000:]
                    report["qmlLoaderLibraries"] = check_loaded(output, bundle, lazy_runtime_names)
                    report["noContainerStartup"] = not (fixture / "storage.json").exists()
                    assert report["noContainerStartup"]
                finally:
                    if process.poll() is None:
                        process.terminate()
                        process.wait(timeout=10)
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('bundle', type=Path)
    parser.add_argument('--gui', action='store_true')
    parser.add_argument('--lazy-runtime-name', action='append', default=[])
    args = parser.parse_args()
    print(json.dumps(verify(args.bundle, args.gui, args.lazy_runtime_name), indent=2))
