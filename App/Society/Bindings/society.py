"""Python access to Society's local, daemon-owned file jobs.

Submission returns a job ID and a queued acknowledgement, not completion.
Inspect the job separately. Interrupted submissions are never retried here.
"""
from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess
from typing import Any


class BackendError(RuntimeError):
    def __init__(self, message: str, *, uncertain: bool = False, response: dict[str, Any] | None = None):
        super().__init__(message)
        self.uncertain = uncertain
        self.response = response or {}


def _default_executable() -> str:
    if configured := os.environ.get("SOCIETY_CLI"):
        return configured
    source = Path(__file__).resolve()
    if source.parent.name == "python" and source.parent.parent.name == "Resources":
        return str(source.parents[2] / "MacOS" / "society-cli")
    return "society-cli"


class Client:
    """A small process client; each call connects to the selected Society daemon."""

    def __init__(self, executable: str | os.PathLike[str] | None = None,
                 broker_directory: str | os.PathLike[str] | None = None, timeout: float = 15.0):
        if timeout <= 0:
            raise ValueError("timeout must be positive")
        self.executable = os.fspath(executable) if executable is not None else _default_executable()
        self.broker_directory = os.fspath(broker_directory) if broker_directory is not None else None
        self.timeout = timeout

    def _request(self, arguments: list[str], *, mutation: bool = False, input_text: str | None = None) -> dict[str, Any]:
        command = [self.executable]
        if self.broker_directory is not None:
            command.extend(["--broker", self.broker_directory])
        command.extend(arguments)
        try:
            result = subprocess.run(command, input=input_text, capture_output=True, text=True, timeout=self.timeout, check=False)
        except subprocess.TimeoutExpired as error:
            raise BackendError("Society did not respond; check the operation before retrying", uncertain=mutation) from error
        except OSError as error:
            raise BackendError(f"Could not start the Society command: {error}") from error
        try:
            response = json.loads(result.stdout)
            if not isinstance(response, dict):
                raise ValueError("expected a JSON object")
        except (ValueError, TypeError) as error:
            raise BackendError("Society returned an invalid response", uncertain=mutation) from error
        if result.returncode or not response.get("accepted", False):
            raise BackendError(str(response.get("error") or "Society rejected the request"),
                               uncertain=bool(response.get("uncertain")), response=response)
        return response

    def status(self) -> dict[str, Any]:
        return self._request(["status"])

    def agent_status(self) -> dict[str, Any]:
        return self._request(["agent", "status"])

    def agent_send(self, prompt: str, attachments: list[str] | None = None) -> dict[str, Any]:
        arguments = ["agent", "send", "--stdin"]
        for path in attachments or []:
            arguments.extend(["--attach", path])
        return self._request(arguments, mutation=True, input_text=prompt)

    def agent_cancel(self) -> dict[str, Any]:
        return self._request(["agent", "cancel"], mutation=True)

    def agent_new(self) -> dict[str, Any]:
        return self._request(["agent", "new"], mutation=True)

    def agent_select(self, model_id: str) -> dict[str, Any]:
        return self._request(["agent", "select", model_id], mutation=True)

    def space_status(self, session_id: str, *, container: str | None = None) -> dict[str, Any]:
        """Inspect one Space conversation; loading can continue after the acknowledgement."""
        arguments = ["space", "status", "--session", session_id]
        if container is not None:
            arguments.extend(["--container", container])
        return self._request(arguments)

    def space_send(self, session_id: str, prompt: str, attachments: list[str] | None = None,
                   *, effort: str = "standard", container: str | None = None) -> dict[str, Any]:
        """Submit one Space turn; an accepted reply is not the completed response."""
        arguments = ["space", "send", "--stdin", "--session", session_id, "--effort", effort]
        for path in attachments or []:
            arguments.extend(["--attach", path])
        if container is not None:
            arguments.extend(["--container", container])
        return self._request(arguments, mutation=True, input_text=prompt)

    def space_cancel(self, session_id: str, *, container: str | None = None) -> dict[str, Any]:
        """Cancel a response only when it belongs to this Space session."""
        arguments = ["space", "cancel", "--session", session_id]
        if container is not None:
            arguments.extend(["--container", container])
        return self._request(arguments, mutation=True)

    def suggested_status(self) -> dict[str, Any]:
        return self._request(["suggested", "status"])

    def suggested_refresh(self) -> dict[str, Any]:
        return self._request(["suggested", "refresh"])

    def recent_status(self) -> dict[str, Any]:
        return self._request(["recent", "status"])

    def recent_refresh(self) -> dict[str, Any]:
        return self._request(["recent", "refresh"])

    def resource_inspect(self, path: str, *, container: str | None = None) -> dict[str, Any]:
        arguments = ["resource", "inspect", path]
        if container is not None:
            arguments.extend(["--container", container])
        return self._request(arguments)

    def resource_duplicates(self, path: str, *, limit: int = 5, cursor: dict[str, Any] | None = None,
                            container: str | None = None) -> dict[str, Any]:
        """Find indexed candidates by last-observed SHA-256; current bytes are not verified."""
        if isinstance(limit, bool) or not isinstance(limit, int) or not 1 <= limit <= 20:
            raise ValueError("limit must be an integer from 1 to 20")
        arguments = ["resource", "duplicates", path, "--limit", str(limit)]
        if cursor is not None:
            if not isinstance(cursor, dict) or not cursor:
                raise ValueError("cursor must be a nonempty object")
            arguments.extend(["--cursor", json.dumps(cursor, ensure_ascii=False, separators=(",", ":"))])
        if container is not None:
            arguments.extend(["--container", container])
        return self._request(arguments)

    def resource_compare_bytes(self, first_path: str, second_path: str, *,
                               container: str | None = None) -> dict[str, Any]:
        """Compare two bounded snapshots; equality does not authorize deduplication."""
        arguments = ["resource", "compare-bytes", first_path, second_path]
        if container is not None:
            arguments.extend(["--container", container])
        return self._request(arguments)

    def resource_content_index(self, path: str, model: str, *, document_prefix: str = "",
                               query_prefix: str = "", container: str | None = None) -> dict[str, Any]:
        """Legacy endpoint; file-content embedding is disabled by storage policy."""
        arguments = ["resource", "content-index", path, "--model", model,
                     "--document-prefix", document_prefix, "--query-prefix", query_prefix]
        if container is not None:
            arguments.extend(["--container", container])
        return self._request(arguments, mutation=True)

    def resource_content_search(self, query: str, model: str, *, limit: int = 10,
                                document_prefix: str = "", query_prefix: str = "",
                                container: str | None = None) -> dict[str, Any]:
        """Legacy endpoint; file-content embedding search is disabled by storage policy."""
        if isinstance(limit, bool) or not isinstance(limit, int) or not 1 <= limit <= 20:
            raise ValueError("limit must be an integer from 1 to 20")
        arguments = ["resource", "content-search", "--query", query, "--model", model, "--limit", str(limit),
                     "--document-prefix", document_prefix, "--query-prefix", query_prefix]
        if container is not None:
            arguments.extend(["--container", container])
        return self._request(arguments)

    def resource_content_policy(self, *, container: str | None = None) -> dict[str, Any]:
        """Read the disabled content-embedding policy and any saved legacy profile."""
        arguments = ["resource", "content-policy", "status"]
        if container is not None:
            arguments.extend(["--container", container])
        return self._request(arguments)

    def set_resource_content_policy(self, model: str | None, *, document_prefix: str = "",
                                    query_prefix: str = "", container: str | None = None) -> dict[str, Any]:
        """Report disabled content indexing; enabling a model is rejected by the daemon."""
        if model is not None and (not isinstance(model, str) or not model.strip()):
            raise ValueError("model must be a nonempty string or None")
        if not isinstance(document_prefix, str) or not isinstance(query_prefix, str):
            raise ValueError("embedding prefixes must be strings")
        if model is None and (document_prefix or query_prefix):
            raise ValueError("prefixes cannot be supplied while disabling indexing")
        arguments = ["resource", "content-policy", "enable" if model is not None else "disable"]
        if model is not None:
            arguments.extend(["--model", model, "--document-prefix", document_prefix, "--query-prefix", query_prefix])
        if container is not None:
            arguments.extend(["--container", container])
        return self._request(arguments, mutation=True)

    def resource_search(self, query: dict[str, Any], *, limit: int = 5, cursor: dict[str, Any] | None = None,
                        container: str | None = None) -> dict[str, Any]:
        """Search the existing metadata index; results do not establish complete coverage."""
        if not isinstance(query, dict):
            raise ValueError("query must be an object")
        if isinstance(limit, bool) or not isinstance(limit, int) or not 1 <= limit <= 20:
            raise ValueError("limit must be an integer from 1 to 20")
        arguments = ["resource", "search", "--query", json.dumps(query, ensure_ascii=False, separators=(",", ":")),
                     "--limit", str(limit)]
        if cursor is not None:
            if not isinstance(cursor, dict) or not cursor:
                raise ValueError("cursor must be a nonempty object")
            arguments.extend(["--cursor", json.dumps(cursor, ensure_ascii=False, separators=(",", ":"))])
        if container is not None:
            arguments.extend(["--container", container])
        return self._request(arguments)

    def resource_lineage(self, path: str, *, direction: str = "both", depth: int = 2, limit: int = 10,
                         container: str | None = None) -> dict[str, Any]:
        """Trace recorded relations without reconciling paths or authorizing cleanup."""
        if direction not in ("incoming", "outgoing", "both"):
            raise ValueError("direction must be incoming, outgoing, or both")
        if isinstance(depth, bool) or not isinstance(depth, int) or not 1 <= depth <= 3:
            raise ValueError("depth must be an integer from 1 to 3")
        if isinstance(limit, bool) or not isinstance(limit, int) or not 1 <= limit <= 20:
            raise ValueError("limit must be an integer from 1 to 20")
        arguments = ["resource", "lineage", path, "--direction", direction, "--depth", str(depth),
                     "--limit", str(limit)]
        if container is not None:
            arguments.extend(["--container", container])
        return self._request(arguments)

    def resource_cleanup_plan(self, path: str, *, limit: int = 10,
                              container: str | None = None) -> dict[str, Any]:
        """Read a non-executable cleanup assessment and its unresolved evidence."""
        if isinstance(limit, bool) or not isinstance(limit, int) or not 1 <= limit <= 20:
            raise ValueError("limit must be an integer from 1 to 20")
        arguments = ["resource", "cleanup-plan", path, "--limit", str(limit)]
        if container is not None:
            arguments.extend(["--container", container])
        return self._request(arguments)

    def submit(self, action: str, path: str, argument: str | None = None,
               *, container: str | None = None) -> dict[str, Any]:
        arguments = ["file-job", "submit", action, path]
        if argument is not None:
            arguments.extend(["--argument", argument])
        if container is not None:
            arguments.extend(["--container", container])
        return self._request(arguments, mutation=True)

    def inspect(self, job_id: str, *, container: str | None = None) -> dict[str, Any]:
        arguments = ["file-job", "inspect", job_id]
        if container is not None:
            arguments.extend(["--container", container])
        return self._request(arguments)
