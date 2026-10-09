#!/usr/bin/env python3
"""Fuzz smoke test (decision D-38, point 5): every test file, corrupted in many ways, must
decode or fail cleanly within a time limit. `imageViewer --info` exits 0 (decoded) or 1 (not
decoded); a crash, an abort, a hang or any other exit code fails the test. Mutations are
deterministic (seeded per file), so a failure reproduces from its name.

Not real fuzzing (libFuzzer with coverage, Phase 5): it catches the crashes a damaged file
reaches easily, in our code and in the libraries. A crash or a hang of the decode worker
(GraphicsMagick) does not reach the viewer, which reports the file as damaged: the viewer's
log line for it counts as a failure here too.

usage: python3 tests/fuzz_smoke.py <imageViewer> [mutations per file, default 12]
env:   FUZZ_SMOKE_DIR  where the mutated files are kept (default: a temporary directory);
       a failing case stays there, named <file>.<mutation>.<ext>
"""
import os
import random
import shutil
import subprocess
import sys
import tempfile
import time

exe = os.path.abspath(sys.argv[1])
per_file = int(sys.argv[2]) if len(sys.argv) > 2 else 12
data = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data")
work = os.environ.get("FUZZ_SMOKE_DIR") or tempfile.mkdtemp(prefix="imageviewer-fuzz-")
os.makedirs(work, exist_ok=True)
TIMEOUT_S = 60  # the decode worker stops itself at 30 s
# QT_FORCE_STDERR_LOGGING: on Windows a GUI program without a console logs to the debugger, and
# the worker failures below would never be seen.
env = dict(os.environ, LC_ALL="C.UTF-8", QT_QPA_PLATFORM="offscreen", QT_FORCE_STDERR_LOGGING="1")


def mutations(content, rng):
    """(name, bytes) pairs: the corruptions a damaged or hostile file shows first."""
    n = len(content)
    head = min(n, 64)
    yield "truncated-half", content[: n // 2]
    yield "truncated-header", content[: max(1, head // 2)]
    yield "zero-tail", content[: n // 2] + bytes(n - n // 2)
    while True:
        kind = rng.choice(["flip", "bytes", "header", "max", "repeat", "cut"])
        b = bytearray(content)
        if kind == "flip":  # a few random bit flips anywhere
            for _ in range(rng.randint(1, 8)):
                i = rng.randrange(n)
                b[i] ^= 1 << rng.randrange(8)
        elif kind == "bytes":  # random bytes over a random span
            i = rng.randrange(n)
            for j in range(i, min(n, i + rng.randint(1, 16))):
                b[j] = rng.randrange(256)
        elif kind == "header":  # sizes, offsets and counts live in the first bytes
            for _ in range(rng.randint(1, 4)):
                b[rng.randrange(head)] = rng.randrange(256)
        elif kind == "max":  # an all-ones field: the classic overflow
            i = rng.randrange(max(1, head - 4))
            b[i:i + 4] = b"\xff\xff\xff\xff"
        elif kind == "repeat":  # a block repeated, which shifts everything after it
            i = rng.randrange(n)
            block = bytes(b[i:i + rng.randint(1, 64)])
            b[i:i] = block * rng.randint(1, 8)
        else:  # cut at a random place
            b = b[: rng.randrange(1, n)] if n > 1 else b
        yield f"{kind}{rng.randrange(1 << 16):04x}", bytes(b)


# What the viewer logs (worker.cpp) when the decode worker crashed, hung or answered nonsense.
WORKER_FAILURES = (b"QProcess::CrashExit", b"decode worker stopped after", b"broken answer true")


def run(path):
    start = time.monotonic()
    try:
        result = subprocess.run([exe, "--info", path], env=env, capture_output=True, timeout=TIMEOUT_S)
    except subprocess.TimeoutExpired:
        return "hang", TIMEOUT_S
    if any(marker in result.stderr for marker in WORKER_FAILURES):
        return "worker", time.monotonic() - start
    return result.returncode, time.monotonic() - start


files = sorted(f for f in os.listdir(data) if os.path.isfile(os.path.join(data, f)) and not f.startswith("."))
failures = []
cases = 0
slowest = (0.0, "")
for name in files:
    content = open(os.path.join(data, name), "rb").read()
    if not content:
        continue
    stem, ext = os.path.splitext(name)
    rng = random.Random(name)
    for i, (kind, mutated) in enumerate(mutations(content, rng)):
        if i >= per_file:
            break
        path = os.path.join(work, f"{stem}.{kind}{ext}")
        with open(path, "wb") as f:
            f.write(mutated)
        code, seconds = run(path)
        cases += 1
        slowest = max(slowest, (seconds, os.path.basename(path)))
        if code in (0, 1):
            os.remove(path)
        else:
            why = {"hang": f"no answer within {TIMEOUT_S} s",
                   "worker": "the decode worker crashed, hung or answered nonsense"}.get(code, f"exit code {code}")
            failures.append(f"{os.path.basename(path)}: {why}")
            print(f"FAIL {failures[-1]}", flush=True)

print(f"{cases} corrupted files from {len(files)} test files; slowest {slowest[0]:.1f} s ({slowest[1]})")
if cases == 0:
    print(f"FAIL no test files in {data}")
    sys.exit(1)
if failures:
    print(f"{len(failures)} failure(s); the files are in {work}")
    sys.exit(1)
if not os.environ.get("FUZZ_SMOKE_DIR"):
    shutil.rmtree(work, ignore_errors=True)
print("fuzz smoke: every corrupted file decoded or failed cleanly")
