"""Disposable project resources for build-host contract tests."""
from contextlib import contextmanager
import os
from pathlib import Path
import sys

provider_dir = os.environ.get("IIFILEPROVIDER_PYTHON_DIRECTORY") or str(
    Path.home() / ".local/SDK/iiFileProvider/share/iiFileProvider/python")
sys.path.insert(0, provider_dir)
import ii_file_provider as provider


@contextmanager
def temporary(project, prefix="test-"):
    project = Path(project).resolve(strict=True)
    if not prefix or "/" in prefix or "\\" in prefix or prefix in (".", ".."):
        raise ValueError("Test workspace prefix must be a single name")
    resources = project / "resources"
    state = resources / ".state"
    if resources.is_symlink() or state.is_symlink():
        raise ValueError("Test resource directories cannot be symlinks")
    provider.mkdir(state, parents=True, exist_ok=True)
    with provider.TemporaryDirectory(prefix=prefix, dir=state) as owner:
        yield Path(owner)


@contextmanager
def build(project, prefix="test-build-"):
    """Keep compiled test artifacts in an owned directory under build/."""
    project = Path(project).resolve(strict=True)
    if not prefix or "/" in prefix or "\\" in prefix or prefix in (".", ".."):
        raise ValueError("Test build prefix must be a single name")
    root = project / "build"
    if root.is_symlink() or not root.is_dir():
        raise ValueError("The project build directory must be a real directory")
    with provider.TemporaryDirectory(prefix=prefix, dir=root) as owner:
        yield Path(owner)
