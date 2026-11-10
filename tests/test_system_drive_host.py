"""Only the backend may own the configured filesystem-host path."""
import sys
from pathlib import Path

daemon, expected, gui = Path(sys.argv[1]), sys.argv[2].encode("utf-8"), Path(sys.argv[3])
if not daemon.is_file() or expected not in daemon.read_bytes():
    raise SystemExit("The daemon does not contain its configured filesystem-host path")
if not gui.is_file() or expected in gui.read_bytes():
    raise SystemExit("The GUI must not own the filesystem-host path")
