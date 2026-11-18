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
