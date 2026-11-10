"""Capture a tool report without shell redirection or shell quoting."""
import pathlib
import subprocess
import sys

if __name__ == "__main__":
    result = subprocess.run(sys.argv[2:], check=True, stdout=subprocess.PIPE)
    pathlib.Path(sys.argv[1]).write_bytes(result.stdout)
