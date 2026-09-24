"""Society entry point for the shared single-bundle runtime deployer."""
from pathlib import Path
import runpy

globals().update(runpy.run_path(str(Path(__file__).resolve().parents[1] / "cmake/single-app/deploy_macos.py"),
                              run_name="__main__" if __name__ == "__main__" else "deployment"))
