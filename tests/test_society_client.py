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


if __name__ == "__main__":
    unittest.main()
