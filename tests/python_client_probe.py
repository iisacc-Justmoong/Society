import importlib.util
import sys
import time

spec = importlib.util.spec_from_file_location("society", sys.argv[1])
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
client = module.Client(executable=sys.argv[2], broker_directory=sys.argv[3])
container = sys.argv[4]
assert client.status()["state"]["selectedContainer"] == container
accepted = client.submit("rename", "Files/python.txt", "python-renamed.txt", container=container)
assert accepted["state"] == "queued"
deadline = time.monotonic() + 25
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
    raise RuntimeError("file job did not finish")
