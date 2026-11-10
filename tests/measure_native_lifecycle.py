"""Launch an isolated native Society window; the operator quits it through the UI."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("fixture", type=Path, help="Existing isolated directory under resources/.state")
    args = parser.parse_args()
    project = Path(__file__).resolve().parents[1]
    state = (project / "resources/.state").resolve()
    fixture = args.fixture.resolve(strict=True)
    if fixture == state or state not in fixture.parents:
        raise SystemExit("Fixture must be an isolated directory below resources/.state")
    container = fixture / "Society.societycontainer"
    if not container.is_dir() or container.is_symlink():
        raise SystemExit("Create a dedicated Society.societycontainer in the fixture first")
    executable = project / "build/bin/Society.app/Contents/MacOS/Society"
    env = {key: value for key, value in os.environ.items()
           if not key.startswith(("DYLD_", "QT_", "QML", "QSG_", "SOCIETY_", "IISOCIETYSYNC_"))}
    env.update({
        "SOCIETY_STORAGE_SETTINGS_PATH": str(fixture / "storage.json"),
        "SOCIETY_CONTAINER_PATH": str(container),
        "SOCIETY_HELPER_DIRECTORY": str(container / ".society-runtime/helpers/v1"),
        "SOCIETY_LOCAL_BROKER_DIR": str(fixture / "broker"),
        "SOCIETY_DISABLE_SESSION_RESTORE": "1",
        "SOCIETY_DISABLE_NATIVE_DRIVE": "1",
        "SOCIETY_LIFECYCLE_PROBE": "1",
        "SOCIETY_LIFECYCLE_PROBE_SCOPE": str(fixture),
        "IISACC_SHARED_STORAGE_ROOT": str(fixture),
        "IISOCIETYSYNC_DISABLE_BLE": "1",
        "IILOCALLLM_DISABLE_APP_MCP": "1",
        "QML_DISK_CACHE_PATH": str(fixture / "qml-cache"),
        "LVRS_PSO_CACHE_DIR": str(fixture / "pso-cache"),
        "XDG_CONFIG_HOME": str(fixture / "config"),
        "XDG_DATA_HOME": str(fixture / "data"),
        "XDG_CACHE_HOME": str(fixture / "cache"),
    })
    log_path = fixture / "lifecycle.log"
    started_ms = time.time_ns() // 1_000_000
    with log_path.open("w") as log:
        process = subprocess.Popen([str(executable), "--container", str(container)], cwd=fixture,
                                   env=env, stdout=log, stderr=subprocess.STDOUT)
        print(json.dumps({"event": "started", "pid": process.pid, "started_ms": started_ms}), flush=True)
        position = 0
        deadline = time.monotonic() + 180
        while process.poll() is None and time.monotonic() < deadline:
            with log_path.open() as reader:
                reader.seek(position)
                lines = reader.read().splitlines()
                position = reader.tell()
                for line in lines:
                    if "SOCIETY_LIFECYCLE" in line or "bootstrap.entry.root-loaded" in line:
                        print(line.strip(), flush=True)
                    if "bootstrap.entry.root-loaded" in line:
                        print(json.dumps({"event": "root_observed", "observed_ms": time.time_ns() // 1_000_000}), flush=True)
            time.sleep(0.05)
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
            print(json.dumps({"event": "timeout"}), flush=True)
        else:
            with log_path.open() as reader:
                reader.seek(position)
                for line in reader:
                    if "SOCIETY_LIFECYCLE" in line:
                        print(line.strip(), flush=True)
        print(json.dumps({"event": "exited", "returncode": process.returncode,
                          "exited_ms": time.time_ns() // 1_000_000}), flush=True)


if __name__ == "__main__":
    main()
