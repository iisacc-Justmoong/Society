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
duplicates = client.resource_duplicates("Files/example.txt", limit=2, container=sys.argv[4])["duplicates"]
assert duplicates["evidence"] == "matching_last_observed_sha256"
assert duplicates["completeness"] == "unknown"
assert [row["path"] for row in duplicates["candidates"]] == ["Files/copy.txt"]
assert duplicates["candidates"][0]["resourceId"] != duplicates["reference"]["resourceId"]
cursor = None
seen = []
for _ in range(5):
    page = client.resource_duplicates("Files/example.txt", limit=1, cursor=cursor, container=sys.argv[4])["duplicates"]
    seen.extend(row["path"] for row in page["candidates"])
    cursor = page["next"] or None
    if cursor is None:
        break
    try:
        client.resource_duplicates("Files/copy.txt", limit=1, cursor=cursor, container=sys.argv[4])
    except module.BackendError:
        pass
    else:
        raise AssertionError("a cursor was accepted for another reference")
else:
    raise AssertionError("pagination did not terminate")
assert seen == ["Files/copy.txt"]
try:
    client.resource_inspect("Files/../outside", container=sys.argv[4])
except module.BackendError:
    pass
else:
    raise AssertionError("resource inspection accepted traversal")
try:
    client.resource_duplicates("Files/../outside", container=sys.argv[4])
except module.BackendError:
    pass
else:
    raise AssertionError("duplicate discovery accepted traversal")
