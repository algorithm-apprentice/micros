"""Bounded-memory live output for fixed CI-owned subprocess commands."""

import os
from pathlib import Path
import subprocess
import sys
import time


def stream_command(argv, root, log_path, *, output=None):
    output = sys.stdout.buffer if output is None else output
    log_path = Path(log_path)
    log_path.parent.mkdir(parents=True, exist_ok=True)
    started = time.monotonic_ns()
    with log_path.open("xb") as log:
        process = subprocess.Popen(
            tuple(argv), cwd=root, stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        )
        try:
            while chunk := process.stdout.read1(65536):
                log.write(chunk)
                log.flush()
                output.write(chunk)
                output.flush()
            code = process.wait()
        except BaseException:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
            raise
        finally:
            process.stdout.close()
        os.fsync(log.fileno())
    return {"argv": list(argv), "exit_code": code, "duration_ns": time.monotonic_ns() - started}
