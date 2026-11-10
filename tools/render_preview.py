"""Export a native framebuffer and PPM using the actual C demo renderer."""
import argparse
import json
import subprocess
import sys
from host_renderer import render_reference


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output_directory")
    args = parser.parse_args()
    try:
        _, _, metadata = render_reference(args.output_directory)
        print(json.dumps(metadata, indent=2))
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        print(f"Host render failed: {error}", file=sys.stderr)
        sys.exit(1)
