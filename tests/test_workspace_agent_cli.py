import json
import os
import pathlib
import socket
import struct
import subprocess
import tempfile
import threading
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
CLI = ROOT / "build/bin/society-cli"


def send_frame(peer, value):
    body = json.dumps(value).encode()
    peer.sendall(struct.pack(">I", len(body)) + body)


def read_exactly(peer, size):
    data = bytearray()
    while len(data) < size:
        part = peer.recv(size - len(data))
        if not part:
            raise RuntimeError("incomplete fixture frame")
        data.extend(part)
    return bytes(data)


@unittest.skipUnless(hasattr(socket, "AF_UNIX") and CLI.exists(), "requires the built Unix CLI")
class WorkspaceAgentCliTest(unittest.TestCase):
    def test_attachment_paths_reach_the_selected_backend(self):
        with tempfile.TemporaryDirectory(prefix="agent-cli-", dir=ROOT / "resources/.state") as directory:
            server = socket.socket(socket.AF_UNIX)
            self.addCleanup(server.close)
            server.bind(os.path.join(directory, "backend.sock"))
            server.listen(1)
            server.settimeout(15)
            observed = []

            def serve():
                with server.accept()[0] as peer:
                    peer.settimeout(15)
                    send_frame(peer, {"type": "state", "state": {"selectedContainer": "/drive"}})
                    length = struct.unpack(">I", read_exactly(peer, 4))[0]
                    request = json.loads(read_exactly(peer, length))
                    observed.append(request)
                    send_frame(peer, {"type": "result", "id": request["id"], "accepted": True})

            worker = threading.Thread(target=serve, daemon=True)
            worker.start()
            result = subprocess.run([str(CLI), "--broker", directory, "agent", "send", "--stdin",
                                     "--attach", "Files/a b.txt", "--attach", "/drive/Files/b.png",
                                     "--container", "/drive"], input="Review", text=True,
                                    capture_output=True, timeout=20, check=False)
            worker.join(15)
            self.assertFalse(worker.is_alive())
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual(observed[0]["prompt"], "Review")
            self.assertEqual(observed[0]["attachments"], ["Files/a b.txt", "/drive/Files/b.png"])
            self.assertEqual(observed[0]["container"], "/drive")


if __name__ == "__main__":
    unittest.main()
