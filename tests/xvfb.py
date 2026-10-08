"""Xvfb on a free display for the Linux GUI tests (screen, render, UI and animation tests).

Xvfb chooses the display number itself and writes it to -displayfd once it accepts
connections, so there is no fixed display number to collide with and no sleep.
"""
import os
import select
import subprocess
import time


def start(screen="640x480x24", timeout_s=30):
    """Start Xvfb; return (process, display) with display like ':1'."""
    read_fd, write_fd = os.pipe()
    process = subprocess.Popen(["Xvfb", "-displayfd", str(write_fd), "-screen", "0", screen, "-nolisten", "tcp"],
                               pass_fds=(write_fd,), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    os.close(write_fd)
    reply = b""
    deadline = time.monotonic() + timeout_s
    try:
        while not reply.endswith(b"\n"):
            remaining = deadline - time.monotonic()
            if remaining <= 0 or not select.select([read_fd], [], [], remaining)[0]:
                raise RuntimeError(f"Xvfb did not report a display within {timeout_s} s")
            chunk = os.read(read_fd, 64)
            if not chunk:
                raise RuntimeError(f"Xvfb exited with code {process.wait(10)} before reporting a display")
            reply += chunk
    except BaseException:
        stop(process)
        raise
    finally:
        os.close(read_fd)
    return process, ":" + reply.decode().strip()


def stop(process):
    process.terminate()
    try:
        process.wait(10)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()
