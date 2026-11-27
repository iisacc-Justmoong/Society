import importlib.util
import json
from pathlib import Path
import subprocess
import unittest
from unittest.mock import patch


module_path = Path(__file__).resolve().parents[1] / "App/Society/Bindings/society.py"
spec = importlib.util.spec_from_file_location("society", module_path)
society = importlib.util.module_from_spec(spec)
spec.loader.exec_module(society)


class SocietyClientTests(unittest.TestCase):
    def test_submit_and_inspect_preserve_job_identity(self):
        client = society.Client(executable="/fixture/society-cli", broker_directory="/fixture/broker")
        responses = [
            subprocess.CompletedProcess([], 0, json.dumps({"accepted": True, "jobId": "job-1", "state": "queued"}), ""),
            subprocess.CompletedProcess([], 0, json.dumps({"accepted": True, "job": {"state": "succeeded"}}), ""),
        ]
        with patch.object(society.subprocess, "run", side_effect=responses) as run:
            accepted = client.submit("rename", "Files/a.txt", "b.txt", container="/fixture/drive")
            result = client.inspect(accepted["jobId"], container="/fixture/drive")
        self.assertEqual(accepted["state"], "queued")
        self.assertEqual(result["job"]["state"], "succeeded")
        self.assertEqual(run.call_args_list[0].args[0], ["/fixture/society-cli", "--broker", "/fixture/broker",
                         "file-job", "submit", "rename", "Files/a.txt", "--argument", "b.txt", "--container", "/fixture/drive"])
        self.assertFalse(run.call_args_list[0].kwargs.get("shell", False))

    def test_failed_or_interrupted_submission_is_not_retried(self):
        client = society.Client(executable="society-cli")
        with patch.object(society.subprocess, "run", side_effect=subprocess.TimeoutExpired("society-cli", 1)) as run:
            with self.assertRaises(society.BackendError) as caught:
                client.submit("trash", "Files/a.txt")
        self.assertTrue(caught.exception.uncertain)
        self.assertEqual(run.call_count, 1)
        result = subprocess.CompletedProcess([], 1, json.dumps({"accepted": False, "error": "drive changed"}), "")
        with patch.object(society.subprocess, "run", return_value=result):
            with self.assertRaises(society.BackendError) as caught:
                client.inspect("job-1")
        self.assertFalse(caught.exception.uncertain)
        self.assertIn("drive changed", str(caught.exception))

    def test_duplicate_candidate_cursor_and_validation(self):
        client = society.Client(executable="society-cli", broker_directory="/fixture/broker")
        cursor = {"containerId": "drive", "resourceId": "asset", "revisionId": "v1", "index": {"revision": "2"}}
        result = subprocess.CompletedProcess([], 0, json.dumps({"accepted": True, "duplicates": {"next": {}}}), "")
        with patch.object(society.subprocess, "run", return_value=result) as run:
            response = client.resource_duplicates("Files/a b.txt", limit=1, cursor=cursor, container="/fixture/drive")
        self.assertEqual(response["duplicates"]["next"], {})
        args = run.call_args.args[0]
        self.assertEqual(args[:7], ["society-cli", "--broker", "/fixture/broker", "resource", "duplicates", "Files/a b.txt", "--limit"])
        self.assertEqual(args[7], "1")
        self.assertEqual(json.loads(args[args.index("--cursor") + 1]), cursor)
        self.assertEqual(args[-2:], ["--container", "/fixture/drive"])
        for limit in (0, 21, True, 1.5):
            with self.assertRaises(ValueError):
                client.resource_duplicates("Files/a.txt", limit=limit)
        with self.assertRaises(ValueError):
            client.resource_duplicates("Files/a.txt", cursor={})

    def test_indexed_search_passes_a_structured_query(self):
        client = society.Client(executable="society-cli")
        query = {"section": "files", "text": "campaign", "keywords": ["approved"]}
        result = subprocess.CompletedProcess([], 0, json.dumps({"accepted": True, "search": {"items": []}}), "")
        with patch.object(society.subprocess, "run", return_value=result) as run:
            response = client.resource_search(query, limit=5, container="/fixture/drive")
        self.assertEqual(response["search"]["items"], [])
        args = run.call_args.args[0]
        self.assertEqual(args[:4], ["society-cli", "resource", "search", "--query"])
        self.assertEqual(json.loads(args[4]), query)
        self.assertEqual(args[-2:], ["--container", "/fixture/drive"])
        with self.assertRaises(ValueError):
            client.resource_search(query, limit=21)

    def test_lineage_preserves_the_review_scope_and_limits(self):
        client = society.Client(executable="society-cli")
        result = subprocess.CompletedProcess([], 0, json.dumps({"accepted": True, "lineage": {
            "edges": [], "completeness": "unknown", "cleanupDecision": "review_required"}}), "")
        with patch.object(society.subprocess, "run", return_value=result) as run:
            response = client.resource_lineage("Files/a b.txt", direction="incoming", depth=3, limit=7,
                                               container="/fixture/drive")
        self.assertEqual(response["lineage"]["cleanupDecision"], "review_required")
        self.assertEqual(run.call_args.args[0], ["society-cli", "resource", "lineage", "Files/a b.txt",
            "--direction", "incoming", "--depth", "3", "--limit", "7", "--container", "/fixture/drive"])
        for depth in (0, 4, True, 1.5):
            with self.assertRaises(ValueError):
                client.resource_lineage("Files/a.txt", depth=depth)
        for limit in (0, 21, True, 1.5):
            with self.assertRaises(ValueError):
                client.resource_lineage("Files/a.txt", limit=limit)
        with self.assertRaises(ValueError):
            client.resource_lineage("Files/a.txt", direction="sideways")

    def test_cleanup_plan_is_a_bounded_read_only_request(self):
        client = society.Client(executable="society-cli")
        result = subprocess.CompletedProcess([], 0, json.dumps({"accepted": True, "cleanupPlan": {
            "policy": {"decision": "blocked", "executable": False}}}), "")
        with patch.object(society.subprocess, "run", return_value=result) as run:
            response = client.resource_cleanup_plan("Files/a b.txt", limit=3, container="/fixture/drive")
        self.assertEqual(response["cleanupPlan"]["policy"]["decision"], "blocked")
        self.assertEqual(run.call_args.args[0], ["society-cli", "resource", "cleanup-plan", "Files/a b.txt",
            "--limit", "3", "--container", "/fixture/drive"])
        for limit in (0, 21, True, 1.5):
            with self.assertRaises(ValueError):
                client.resource_cleanup_plan("Files/a.txt", limit=limit)

    def test_byte_comparison_preserves_both_paths(self):
        client = society.Client(executable="society-cli")
        result = subprocess.CompletedProcess([], 0, json.dumps({"accepted": True, "byteComparison": {
            "comparison": "equal", "snapshot": "non_atomic", "deduplicationAuthorized": False}}), "")
        with patch.object(society.subprocess, "run", return_value=result) as run:
            response = client.resource_compare_bytes("Files/a b.txt", "Models/Other/copy.bin", container="/fixture/drive")
        self.assertFalse(response["byteComparison"]["deduplicationAuthorized"])
        self.assertEqual(run.call_args.args[0], ["society-cli", "resource", "compare-bytes", "Files/a b.txt",
            "Models/Other/copy.bin", "--container", "/fixture/drive"])

    def test_content_embedding_requests_preserve_model_and_prefixes(self):
        client = society.Client(executable="society-cli")
        result = subprocess.CompletedProcess([], 0, json.dumps({"accepted": True, "content": {
            "evidence": "local_text_embeddings", "completeness": "unknown", "items": []}}), "")
        with patch.object(society.subprocess, "run", return_value=result) as run:
            client.resource_content_index("Files/a b.md", "model://fixture", document_prefix="passage: ",
                                          query_prefix="query: ", container="/fixture/drive")
            response = client.resource_content_search("waves", "model://fixture", limit=3,
                document_prefix="passage: ", query_prefix="query: ", container="/fixture/drive")
        self.assertEqual(response["content"]["completeness"], "unknown")
        self.assertEqual(run.call_args_list[0].args[0], ["society-cli", "resource", "content-index", "Files/a b.md",
            "--model", "model://fixture", "--document-prefix", "passage: ", "--query-prefix", "query: ",
            "--container", "/fixture/drive"])
        self.assertEqual(run.call_args_list[1].args[0], ["society-cli", "resource", "content-search", "--query", "waves",
            "--model", "model://fixture", "--limit", "3", "--document-prefix", "passage: ",
            "--query-prefix", "query: ", "--container", "/fixture/drive"])
        for limit in (0, 21, True, 1.5):
            with self.assertRaises(ValueError):
                client.resource_content_search("waves", "model://fixture", limit=limit)

    def test_content_policy_requires_an_explicit_model_and_preserves_prefixes(self):
        client = society.Client(executable="society-cli")
        result = subprocess.CompletedProcess([], 0, json.dumps({"accepted": True, "policy": {"enabled": True}}), "")
        with patch.object(society.subprocess, "run", return_value=result) as run:
            client.resource_content_policy(container="/fixture/drive")
            client.set_resource_content_policy("model://fixture", document_prefix="passage: ",
                                               query_prefix="query: ", container="/fixture/drive")
            client.set_resource_content_policy(None, container="/fixture/drive")
        self.assertEqual(run.call_args_list[0].args[0], ["society-cli", "resource", "content-policy", "status", "--container", "/fixture/drive"])
        self.assertEqual(run.call_args_list[1].args[0], ["society-cli", "resource", "content-policy", "enable", "--model", "model://fixture",
            "--document-prefix", "passage: ", "--query-prefix", "query: ", "--container", "/fixture/drive"])
        self.assertEqual(run.call_args_list[2].args[0], ["society-cli", "resource", "content-policy", "disable", "--container", "/fixture/drive"])
        for invalid in ("", "   ", 12):
            with self.assertRaises(ValueError):
                client.set_resource_content_policy(invalid)
        with self.assertRaises(ValueError):
            client.set_resource_content_policy(None, document_prefix="not allowed")
        with patch.object(society.subprocess, "run", side_effect=subprocess.TimeoutExpired("society-cli", 1)):
            with self.assertRaises(society.BackendError) as caught:
                client.set_resource_content_policy("model://fixture")
        self.assertTrue(caught.exception.uncertain)


if __name__ == "__main__":
    unittest.main()
