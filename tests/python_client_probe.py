import importlib.util
import os
import sys
import time

spec = importlib.util.spec_from_file_location("society", sys.argv[1])
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
client = module.Client(executable=sys.argv[2], broker_directory=sys.argv[3])
container = sys.argv[4]
assert client.status()["state"]["selectedContainer"] == container
assert "messages" in client.agent_status()["agent"]
assert "items" in client.suggested_status()["suggested"]
assert "items" in client.suggested_refresh()["suggested"]
assert "items" in client.recent_status()["recent"]
assert "items" in client.recent_refresh()["recent"]
try:
    client.agent_send("fixture")
except module.BackendError as error:
    assert not error.uncertain
else:
    raise AssertionError("an agent with no model must reject a prompt")
if len(sys.argv) > 5 and sys.argv[5] == "--agent-only":
    sys.exit(0)
accepted = client.submit("rename", "Files/python.txt", "python-renamed.txt", container=container)
assert accepted["state"] == "queued"
configured_timeout = int(os.environ.get("SOCIETY_TEST_FILE_JOB_TIMEOUT_MS", "0"))
diagnostic_timeout = configured_timeout / 1000 if 30000 < configured_timeout <= 180000 else 25
deadline = time.monotonic() + diagnostic_timeout
state = None
while time.monotonic() < deadline:
    try:
        state = client.inspect(accepted["jobId"], container=container)["job"]["state"]
        if state == "succeeded":
            break
        if state in ("failed", "partial", "cancelled", "attention"):
            raise RuntimeError(f"file job ended as {state}")
    except module.BackendError as error:
        if "not found" not in str(error).lower():
            raise
    time.sleep(0.1)
else:
    raise RuntimeError(f"file job did not finish; last state: {state!r}")
