"""Verify saved-host cold launches on a provisioned iPhone without resetting data.

Requires SOCIETY_GROUP_STATE_RUNTIME_PROBE and an available registered host.
Only metadata is copied; account credentials and pairing seeds are never read.
"""
import argparse
import json
from pathlib import Path
import subprocess
import time
import uuid


def command(*arguments):
    result = subprocess.run(["xcrun", "devicectl", *arguments], capture_output=True,
                            text=True, timeout=45)
    if result.returncode:
        raise RuntimeError(f"Device command failed: {result.stdout}\n{result.stderr}")
    return result


def verify(device, output, runs=3, workspace_budget_ms=5000, connection_budget_ms=10000, offline=False):
    output.mkdir(parents=True, exist_ok=True)
    results = []
    slow_launches = []
    try:
        for index in range(runs):
            run_id = str(uuid.uuid4())
            command("device", "process", "launch", "--device", device,
                    "--terminate-existing", "--environment-variables",
                    json.dumps({"SOCIETY_GROUP_STATE_PROBE_STAGE": "offline-inspect" if offline else "mirror-inspect",
                                "SOCIETY_RECONNECT_PROBE_ID": run_id}),
                    "com.iisacc.society", "--timeout", "30")
            deadline = time.monotonic() + 90
            path = output / f"launch-{index + 1}.json"
            state = {}
            tabs = set()
            while time.monotonic() < deadline:
                try:
                    command("device", "copy", "from", "--device", device,
                            "--domain-type", "appDataContainer", "--domain-identifier",
                            "com.iisacc.society", "--source", "Documents/mirror-probe.json",
                            "--destination", str(path), "--timeout", "15")
                    state = json.loads(path.read_text())
                except (subprocess.SubprocessError, RuntimeError, OSError, ValueError):
                    time.sleep(1)
                    continue
                if offline and state.get("runId") == run_id:
                    if state.get("connected") or state.get("runtimeEnabled") or state.get("onboardingRequired"):
                        raise RuntimeError(f"Offline launch violated runtime independence; inspect {path}")
                    if state.get("shellVisible"):
                        tabs.add(state.get("selectedTab"))
                    if (state.get("elapsedSinceRootMs", 0) >= 15000
                            and tabs == {"Dashboard", "Storage", "Tools"}):
                        break
                if (not offline and state.get("runId") == run_id and state.get("workspaceReady")
                        and state.get("connected") and not state.get("onboardingRequired")
                        and state.get("savedHostRouteCount", 0) > 0):
                    break
                time.sleep(1)
            else:
                raise RuntimeError(f"Launch {index + 1} did not restore its host; inspect {path}")
            result = {key: state[key] for key in ("firstWorkspaceMs", "firstConnectionMs", "firstHostReadyMs",
                      "workspaceReady", "connected", "hostConnectionReady", "onboardingRequired", "savedHostRouteCount",
                      "shellVisible", "runtimeEnabled", "elapsedSinceRootMs")}
            if offline:
                result["visitedTabs"] = sorted(tabs)
            result["launch"] = index + 1
            result["processId"] = state["processId"]
            result["runId"] = run_id
            results.append(result)
            print(json.dumps(result), flush=True)
            # The first upgraded launch learns its route; later launches must use
            # the persisted state promptly. Timings begin at QML root readiness.
            if not offline and index and (state["firstWorkspaceMs"] > workspace_budget_ms
                          or state["firstConnectionMs"] > connection_budget_ms):
                slow_launches.append(index + 1)
            time.sleep(2)
    finally:
        (output / "summary.json").write_text(json.dumps(results, indent=2) + "\n")
        # Leave the real application running with no diagnostic environment.
        command("device", "process", "launch", "--device", device,
                "--terminate-existing", "com.iisacc.society", "--timeout", "30")
    if slow_launches:
        raise RuntimeError(f"Cold launches exceeded the timing budget: {slow_launches}; all measurements were retained")
    return results


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--device", required=True)
    parser.add_argument("--output", type=Path, default=Path("build/reconnect-device"))
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--workspace-budget-ms", type=int, default=5000)
    parser.add_argument("--connection-budget-ms", type=int, default=10000)
    parser.add_argument("--offline", action="store_true", help="Disable this process's transport and verify mobile navigation")
    args = parser.parse_args()
    if args.runs < 2:
        parser.error("Use at least two launches to verify persisted reconnection.")
    verify(args.device, args.output.resolve(), args.runs, args.workspace_budget_ms, args.connection_budget_ms, args.offline)
