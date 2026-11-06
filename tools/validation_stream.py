"""Live canonical-workflow output with byte-exact owned logs."""

import codecs
import os
import signal
import subprocess
import sys


def _darwin_only_exited_leader(process):
    if sys.platform != "darwin":
        return False
    observed = os.waitid(os.P_PID, process.pid, os.WEXITED | os.WNOHANG | os.WNOWAIT)
    if observed is None or observed.si_pid != process.pid:
        return False
    import ctypes

    query = ctypes.CDLL("/usr/lib/libproc.dylib", use_errno=True).proc_listpgrppids
    query.argtypes = (ctypes.c_int, ctypes.c_void_p, ctypes.c_int)
    query.restype = ctypes.c_int
    members = (ctypes.c_int * 2)(-1, -1)
    ctypes.set_errno(0)
    count = query(process.pid, members, ctypes.sizeof(members))
    return count == 1 and tuple(members) == (process.pid, -1) and ctypes.get_errno() == 0


def _stop_owned_group(process):
    try:
        if process.returncode is None:
            # Keep the unreaped leader's PID reserved until its group is terminated.
            for value in (signal.SIGTERM, signal.SIGKILL):
                try:
                    os.killpg(process.pid, value)
                except ProcessLookupError:
                    pass
                except PermissionError:
                    # Darwin denies signaling a group containing only its zombie leader.
                    if not _darwin_only_exited_leader(process):
                        raise
                    break
    finally:
        process.wait(timeout=2)


def _display(text, output):
    if text:
        if output.write(text) != len(text):
            raise OSError("workflow progress display was truncated")
        output.flush()


def stream_workflow(command, *, cwd, log, display=None):
    display = sys.stdout if display is None else display
    if signal.getsignal(signal.SIGCHLD) != signal.SIG_DFL:
        raise ValueError("workflow ownership requires the default SIGCHLD disposition")
    process, cancellation = None, None
    finishing, ownership_lost = False, False
    handlers = {}

    def cancel(signum, frame):
        nonlocal cancellation

        cancellation = signum
        if process is not None and not finishing:
            raise KeyboardInterrupt("workflow cancellation")

    try:
        for signum in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP, signal.SIGQUIT):
            handlers[signum] = signal.signal(signum, cancel)
        if cancellation is not None:
            raise KeyboardInterrupt("workflow cancellation")
        process = subprocess.Popen(
            command, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            bufsize=0, start_new_session=True,
        )
        if cancellation is not None:
            raise KeyboardInterrupt("workflow cancellation")
        decoder = codecs.getincrementaldecoder("utf-8")(errors="replace")
        if process.stdout is None:
            raise ValueError("workflow output pipe is unavailable")
        with process.stdout:
            while True:
                data = process.stdout.read(65536)
                if not data:
                    break
                if log.write(data) != len(data):
                    raise OSError("workflow evidence log was truncated")
                log.flush()
                _display(decoder.decode(data), display)
            _display(decoder.decode(b"", final=True), display)
        try:
            os.waitid(os.P_PID, process.pid, os.WEXITED | os.WNOWAIT)
        except ChildProcessError:
            ownership_lost = True
            raise
    finally:
        finishing = True
        try:
            if process is not None and not ownership_lost:
                _stop_owned_group(process)
        finally:
            try:
                if process is not None and process.stdout is not None:
                    process.stdout.close()
            finally:
                for signum, previous in handlers.items():
                    signal.signal(signum, previous)
    if cancellation is not None:
        raise KeyboardInterrupt("workflow cancellation")
    return subprocess.CompletedProcess(command, process.returncode)
