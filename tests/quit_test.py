#!/usr/bin/env python3
"""Quit test on macOS: the application menu's Quit must end the process cleanly.

The viewer opens an image in a folder of test images (so the neighbours are being preloaded)
and is quit the way users quit a Mac application: ImageViewer > Quit ImageViewer in the menu
bar, then Cmd+Q, then a "quit" Apple event, whichever this machine lets a script send. The
test fails when the process ends with a signal or a non-zero code, or when macOS wrote a
crash report for it. Two rounds: right after the image appears (a preload is usually still
decoding) and after the application has gone idle. A failing round is run again under lldb,
which prints every thread's backtrace at the crash. A viewer still running after a quit
that was sent fails too; only a machine that refuses every route skips the test.

Meant for CI machines: macOS keeps preferences in cfprefsd, which no environment variable
redirects, so the viewer runs with the user's own preferences and saves its session there.

usage: python3 tests/quit_test.py <ImageViewer.app/Contents/MacOS/ImageViewer>
env:   QUIT_TEST_DIR  where to keep the images and the logs (default: a temp dir)
"""
import glob
import os
import shutil
import subprocess
import sys
import tempfile
import time

DEADLINE_S = 60
EXIT_S = 20

exe = os.path.abspath(sys.argv[1])
here = os.path.dirname(os.path.abspath(__file__))
work = os.environ.get("QUIT_TEST_DIR") or tempfile.mkdtemp(prefix="imageviewer-quit-")
folder = os.path.join(work, "images")
os.makedirs(folder, exist_ok=True)
for name in os.listdir(os.path.join(here, "data")):
    shutil.copy(os.path.join(here, "data", name), folder)
image = os.path.join(folder, "p3red16.png")
reports = os.path.expanduser("~/Library/Logs/DiagnosticReports")


def crash_reports():
    return set(glob.glob(os.path.join(reports, "ImageViewer*"))) | set(glob.glob("/Library/Logs/DiagnosticReports/ImageViewer*"))


def osascript(script):
    result = subprocess.run(["osascript", "-e", script], capture_output=True, text=True, timeout=30)
    return result.returncode == 0, (result.stdout + result.stderr).strip()


def quit_routes(pid):
    process = f"(first process whose unix id is {pid})"
    yield "application menu", (
        f'tell application "System Events" to tell {process}\n'
        "set frontmost to true\n"
        'click (first menu item of menu 1 of menu bar item 2 of menu bar 1 whose name starts with "Quit")\n'
        "end tell")
    yield "Cmd+Q", (
        f'tell application "System Events"\nset frontmost of {process} to true\n'
        'keystroke "q" using command down\nend tell')
    yield "quit Apple event", 'tell application id "com.cristallumnis.imageviewer" to quit'


def run_round(label, settle_s, debugger=False):
    log_path = os.path.join(work, f"viewer-{label}.log")
    env = dict(os.environ, LANG="en_US.UTF-8",
               QT_LOGGING_RULES="imageviewer.*=true;qt.qpa.application=true",
               QT_MESSAGE_PATTERN="%{time process} %{category}: %{message}")
    before = crash_reports()
    command = [exe, image]
    if debugger:
        # On a crash, lldb's batch mode runs the -k commands: all threads' backtraces.
        command = ["lldb", "--batch", "-o", "run", "-k", "thread backtrace all", "-k", "quit 1", "--", exe, image]
    with open(log_path, "w") as log:
        app = subprocess.Popen(command, env=env, stdout=log, stderr=log)
        start = time.monotonic()
        shown = False
        while time.monotonic() - start < DEADLINE_S and app.poll() is None:
            time.sleep(0.3)
            text = open(log_path, encoding="utf-8", errors="replace").read()
            if "decoded for display" in text or "shown from the cache" in text:
                shown = True
                break
        pid = app.pid
        if debugger and shown:
            found = subprocess.run(["pgrep", "-n", "-x", "ImageViewer"], capture_output=True, text=True).stdout.split()
            shown = bool(found)
            pid = int(found[0]) if found else 0
        if not shown:
            app.kill()
            app.wait()
            print(open(log_path, encoding="utf-8", errors="replace").read())
            return f"{label}: the image was not shown within {DEADLINE_S} s (exit code {app.returncode})"
        time.sleep(settle_s)
        used = None
        sent = []
        for route, script in quit_routes(pid):
            ok, output = osascript(script)
            print(f"{label}: {route}: {'sent' if ok else 'refused: ' + output}")
            if not ok:
                continue
            sent.append(route)
            try:
                app.wait(EXIT_S * (3 if debugger else 1))
                used = route
                break
            except subprocess.TimeoutExpired:
                print(f"{label}: still running {EXIT_S} s after {route}")
        if used is None:
            app.kill()
            app.wait()
            if sent:
                print(open(log_path, encoding="utf-8", errors="replace").read())
                return f"{label}: still running after the {', '.join(sent)}"
            return None  # nothing could quit it here: reported by the caller as skipped
    # ReportCrash writes the report a moment after the process ends.
    new_reports = []
    for _ in range(20):
        new_reports = sorted(crash_reports() - before)
        if new_reports or app.returncode == 0:
            break
        time.sleep(1)
    text = open(log_path, encoding="utf-8", errors="replace").read()
    print(text)
    for path in new_reports:
        print(f"--- {path}")
        with open(path, encoding="utf-8", errors="replace") as report:
            print("".join(report.readlines()[:300]))
    crashed = app.returncode != 0 or new_reports or (debugger and "stop reason = signal" in text)
    if crashed:
        return (f"{label}: quit through the {used} ended with code {app.returncode}"
                f"{' and a crash report' if new_reports else ''}")
    print(f"{label}: quit through the {used}: clean exit")
    return ""


failures = []
skipped = False
for label, settle_s in (("while-preloading", 0.0), ("idle", 3.0)):
    outcome = run_round(label, settle_s)
    if outcome is None:
        skipped = True
    elif outcome:
        failures.append(outcome)
        run_round(label + "-lldb", settle_s, debugger=True)  # for the backtraces only
for failure in failures:
    print(f"FAIL: {failure}")
if skipped and not failures:
    print("::warning title=Quit test skipped::this machine refused every scripted way to quit an application")
sys.exit(1 if failures else 0)
