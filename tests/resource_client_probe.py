import importlib.util
import sys

spec = importlib.util.spec_from_file_location("society", sys.argv[1])
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
client = module.Client(executable=sys.argv[2], broker_directory=sys.argv[3], timeout=45)
value = client.resource_inspect("Files/example.txt", container=sys.argv[4])["resource"]
assert value["resource"]["id"]
assert value["contentRevision"]["resourceId"] == value["resource"]["id"]
assert value["locations"][0]["id"] is None
assert value["locations"][0]["availability"] == "unknown"
assert value["provenance"] == "last_observed"
try:
    client.resource_inspect("Files/../outside", container=sys.argv[4])
except module.BackendError:
    pass
else:
    raise AssertionError("resource inspection accepted traversal")
