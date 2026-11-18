import importlib.util
import pathlib
import unittest


module_path = pathlib.Path(__file__).resolve().parents[1] / "App/Society/Bindings/society.py"
spec = importlib.util.spec_from_file_location("society_workspace_binding", module_path)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class Probe(module.Client):
    def __init__(self):
        self.recorded = None

    def _request(self, arguments, *, mutation=False, input_text=None):
        self.recorded = (arguments, mutation, input_text)
        return {"accepted": True}


class WorkspaceAgentBindingTest(unittest.TestCase):
    def test_attachment_paths_are_passed_as_distinct_arguments(self):
        client = Probe()
        client.agent_send("review", ["/one file.txt", "/two.png"])
        self.assertEqual(client.recorded, (["agent", "send", "--stdin", "--attach", "/one file.txt",
                                            "--attach", "/two.png"], True, "review"))

    def test_existing_text_call_remains_compatible(self):
        client = Probe()
        client.agent_send("hello")
        self.assertEqual(client.recorded, (["agent", "send", "--stdin"], True, "hello"))


if __name__ == "__main__":
    unittest.main()
