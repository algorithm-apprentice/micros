#!/usr/bin/env -S python3.15 -E -S -B

"""Generate only the checked current observed user-image catalog."""

import argparse
import os
from pathlib import Path
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from tools.python_host_runtime import require_current_runtime
from tools.stack_artifact_files import read_regular
from tools.stack_bound_types import StackEvidenceError
from tools.stack_bound_user_images import (
    USER_IMAGE_SCOPES, render_user_catalog, user_observer_descriptor,
)
from tools.user_elf_parse import load_elf


ROOT = Path(__file__).resolve().parent.parent


def main(argv=None):
    require_current_runtime()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    arguments = parser.parse_args(argv)
    directory = arguments.build.resolve()
    configure = directory.parent.name
    if configure not in USER_IMAGE_SCOPES or directory != ROOT / "build/stack-evidence" / configure / "observed":
        raise StackEvidenceError("user-observer catalog has an unregistered build directory")
    cache = read_regular(directory / "CMakeCache.txt")[0].decode().splitlines()
    if [line.partition("=")[2] for line in cache if line.startswith("MICROS_STACK_BUILD_MODE:")] != ["observed"]:
        raise StackEvidenceError("user-observer catalog is forbidden outside observed mode")
    rows = tuple(user_observer_descriptor(
        load_elf(directory / relative), relative, image_id,
    ) for image_id, relative in USER_IMAGE_SCOPES[configure])
    content = render_user_catalog(configure, rows)
    output = directory / "generated/stack_observer_images.h"
    output.parent.mkdir(parents=True, exist_ok=True)
    if output.is_symlink():
        raise StackEvidenceError("user-observer catalog output is linked")
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=output.parent, delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(content.encode())
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, output)
        temporary = None
    finally:
        if temporary is not None:
            temporary.unlink()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
