import importlib.util
import sys

spec = importlib.util.spec_from_file_location("society", sys.argv[1])
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
client = module.Client(executable=sys.argv[2], broker_directory=sys.argv[3], timeout=120)
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
comparison = client.resource_compare_bytes("Files/example.txt", "Files/copy.txt", container=sys.argv[4])["byteComparison"]
assert comparison["comparison"] == "equal" and comparison["snapshot"] == "non_atomic"
assert not comparison["deduplicationAuthorized"]
for request in (lambda: client.resource_content_index("Files/example.txt", "model://fixture", container=sys.argv[4]),
                lambda: client.resource_content_search("concept", "model://fixture", limit=3, container=sys.argv[4])):
    try:
        request()
    except module.BackendError as error:
        assert "disabled" in str(error).lower()
    else:
        raise AssertionError("file-content embedding was not disabled")
policy = client.resource_content_policy(container=sys.argv[4])["policy"]
assert not policy["enabled"]
try:
    client.set_resource_content_policy("model://fixture", document_prefix="passage: ",
                                       query_prefix="query: ", container=sys.argv[4])
except module.BackendError as error:
    assert "disabled" in str(error).lower()
else:
    raise AssertionError("file-content embedding policy could be enabled")
assert not client.set_resource_content_policy(None, container=sys.argv[4])["policy"]["enabled"]
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
search = client.resource_search({"section": "files", "text": "copy"}, limit=5, container=sys.argv[4])["search"]
assert search["evidence"] == "indexed_metadata" and search["completeness"] == "unknown"
assert [row["path"] for row in search["items"]] == ["Files/copy.txt"]
lineage = client.resource_lineage("Files/example.txt", direction="incoming", depth=2, limit=5,
                                  container=sys.argv[4])["lineage"]
assert lineage["evidence"] == "user_asserted_relations" and lineage["completeness"] == "unknown"
assert lineage["cleanupDecision"] == "review_required"
assert [edge["source"] for edge in lineage["edges"]] == ["files/copy.txt"]
plan = client.resource_cleanup_plan("Files/example.txt", limit=5, container=sys.argv[4])["cleanupPlan"]
assert plan["policy"]["decision"] == "blocked" and not plan["policy"]["executable"]
assert "dependency_coverage_unknown" in plan["policy"]["reasons"]
assert plan["relations"]["edges"][0]["source"] == "files/copy.txt"
try:
    client.submit("trash", "Files/example.txt", container=sys.argv[4])
except module.BackendError as error:
    assert not error.uncertain and "cleanup" in str(error).lower()
else:
    raise AssertionError("the headless CLI submitted an unverified cleanup")
try:
    client.submit("move", "Files/example.txt", "Deleted", container=sys.argv[4])
except module.BackendError as error:
    assert not error.uncertain and "cleanup" in str(error).lower()
else:
    raise AssertionError("the headless CLI bypassed cleanup through move")
assert client.resource_inspect("Files/example.txt", container=sys.argv[4])["resource"]["resource"]["id"]
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
