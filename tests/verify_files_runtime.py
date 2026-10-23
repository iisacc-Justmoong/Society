"""Measure a real, isolated Society window with its bundled runtime and Metal renderer."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import time


def verify(bundle, fixture, output, runs=3, keep_last=False):
    bundle, fixture, output = bundle.resolve(), fixture.resolve(), output.resolve()
    assert fixture.is_dir() and fixture.is_relative_to(output)
    assert len(list((fixture / "Files").glob("file-*.txt"))) == 2000
    reports = []
    for run in range(1, runs + 1):
        report_path = output / f"installed-run-{run}.json"
        report_path.unlink(missing_ok=True)
        session = output / f"probe-session-{run}"
        session.mkdir(exist_ok=True)
        env = {key: value for key, value in os.environ.items()
               if not key.startswith(("DYLD_", "QT_", "QML", "QSG_", "SOCIETY_", "IISOCIETYSYNC_"))}
        env.update({"SOCIETY_FILES_PROBE_OUTPUT": str(report_path),
                    "SOCIETY_STORAGE_SETTINGS_PATH": str(session / "storage.json"),
                    "SOCIETY_HELPER_DIRECTORY": str(fixture / ".society-runtime/helpers/v1"),
                    "SOCIETY_CONTAINER_PATH": str(fixture),
                    "SOCIETY_DISABLE_NATIVE_DRIVE": "1",
                    "SOCIETY_DISABLE_SESSION_RESTORE": "1", "SOCIETY_DISABLE_PHOTOS": "1",
                    "IISOCIETYSYNC_DISABLE_BLE": "1"})
        with (output / f"installed-run-{run}.log").open("w") as log:
            process = subprocess.Popen([str(bundle / "Contents/MacOS/Society")],
                                       env=env, stdout=log, stderr=subprocess.STDOUT,
                                       start_new_session=keep_last and run == runs)
            preserve = False
            try:
                deadline = time.monotonic() + 100
                while not report_path.exists() and process.poll() is None and time.monotonic() < deadline:
                    time.sleep(0.1)
                assert process.poll() is None and report_path.exists(), f"Probe failed; see {log.name}"
                report = json.loads(report_path.read_text())
                assert report["complete"] and report["windowVisible"] and report["screenshotSaved"], report
                assert report["source"] == str(fixture) and report["rowCount"] == 2000, report
                assert 0 < report["visibleCells"] < 200 and report["sceneItems"] < 3000, report
                assert report["graphicsApiName"] == "Metal", report
                assert 0 <= report["guiReadyMs"] <= report["firstPopulatedFrameMs"], report
                assert (report_path.with_suffix(".json.png")).stat().st_size > 10000
                loaded = subprocess.run(["lsof", "-Fn", "-p", str(process.pid)],
                                        capture_output=True, text=True, check=True).stdout
                libraries = [line[1:] for line in loaded.splitlines()
                             if line.startswith("n") and ("libLVRS.dylib" in line or "libiiSocietyContainerGui." in line or "libiiSocietyContainer.0" in line)]
                assert len(libraries) >= 3 and all(path.startswith(str(bundle) + "/") for path in libraries), libraries
                report.update({"pid": process.pid, "bundledLibraries": libraries})
                report_path.write_text(json.dumps(report, indent=2) + "\n")
                reports.append(report)
                preserve = keep_last and run == runs
            finally:
                if not preserve and process.poll() is None:
                    process.terminate()
                    process.wait(timeout=15)
    (output / "installed-measurements.json").write_text(json.dumps(reports, indent=2) + "\n")
    return reports


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bundle", type=Path)
    parser.add_argument("fixture", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--keep-last", action="store_true")
    args = parser.parse_args()
    print(json.dumps(verify(args.bundle, args.fixture, args.output, args.runs, args.keep_last), indent=2))
