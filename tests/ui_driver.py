"""What the interaction tests share (D-102): the viewer started on Xvfb with its own HOME, settings
and trash, driven like a user with xdotool, the screen grabbed and waited on, the settings file
read and edited. Imported by tests/ui_test.py and tests/ui_views_test.py, whose command line it
reads: <ImageViewer> [vulkan|opengl]; env UI_TEST_DIR, where to keep the files, the log and the
last screenshot (default: a temp dir). Split from ui_test.py (D-08); a module, not a test.

The application never touches the user's settings or trash: HOME, XDG_CONFIG_HOME and
XDG_DATA_HOME are directories of the test's own.
"""
import ctypes
import ctypes.util
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time

import numpy as np

import xvfb

DEADLINE_S = 60
POLL_S = 0.3

exe = os.path.abspath(sys.argv[1])
svg_renderer = re.search(r"^ *svg \| .* \| yes \| ", subprocess.run(
    [exe, "--formats"], env=dict(os.environ, QT_QPA_PLATFORM="offscreen"), capture_output=True, text=True,
    encoding="utf-8", errors="replace", timeout=120).stdout, re.MULTILINE) is not None
rhi = sys.argv[2] if len(sys.argv) > 2 else "vulkan"
work = os.environ.get("UI_TEST_DIR") or tempfile.mkdtemp(prefix="imageviewer-ui-")
home = os.path.join(work, "home")
pictures = os.path.join(home, "pictures")
config = os.path.join(work, "config")
data = os.path.join(home, ".local", "share")
# A reused UI_TEST_DIR must not carry an earlier run's settings, trash or test files into this one.
for d in (home, config, *(os.path.join(work, name) for name in ("zoom", "wide", "gallery", "churn", "line", "svgs"))):
    shutil.rmtree(d, ignore_errors=True)
for d in (pictures, config, data):
    os.makedirs(d, exist_ok=True)
log_path = os.path.join(work, f"viewer-{rhi}.log")
shot_path = os.path.join(work, f"screen-{rhi}.png")
settings_file = os.path.join(config, "Cristallumnis", "ImageViewer.conf")

server, display = xvfb.start("1600x1000x24")
env = dict(os.environ, DISPLAY=display, QT_QPA_PLATFORM="xcb", IMAGEVIEWER_RHI=rhi, IMAGEVIEWER_OUTPUT="sdr",
           LC_ALL="C.UTF-8", LANGUAGE="en", HOME=home, XDG_CONFIG_HOME=config, XDG_DATA_HOME=data,
           QT_LOGGING_RULES="imageviewer.*=true")


class State:
    app = None  # the running viewer (start()): every wait fails at once when it has exited


state = State()


def launch(args, log):
    """The viewer started on `args` (files, folders), its output into `log`."""
    state.app = subprocess.Popen([exe, *args], env=env, stdout=log, stderr=log)
    return state.app


def grab():
    return xvfb.grab(display, shot_path)


def find(screen, ref):
    """Top-left of an exact copy of `ref` on the screen, or None."""
    h, w = ref.shape[:2]
    ys, xs = np.nonzero(np.all(screen[: screen.shape[0] - h + 1, : screen.shape[1] - w + 1] == ref[0, 0], axis=2))
    for y, x in zip(ys, xs):
        if np.array_equal(screen[y:y + h, x:x + w], ref):
            return int(x), int(y)
    return None


def wait_for(what, ref):
    start = time.monotonic()
    while time.monotonic() - start < DEADLINE_S:
        if state.app.poll() is not None:
            fail(f"{what}: ImageViewer exited with code {state.app.returncode}")
        screen = grab()
        if screen is not None and find(screen, ref) is not None:
            print(f"ok   {what} ({time.monotonic() - start:.1f} s)")
            return
        time.sleep(POLL_S)
    fail(f"{what}: not on screen after {DEADLINE_S} s (see {shot_path})")


def wait_until(what, condition):
    start = time.monotonic()
    while time.monotonic() - start < DEADLINE_S:
        if state.app.poll() is not None:
            fail(f"{what}: ImageViewer exited with code {state.app.returncode}")
        if condition():
            print(f"ok   {what} ({time.monotonic() - start:.1f} s)")
            return
        time.sleep(POLL_S)
    fail(f"{what}: not after {DEADLINE_S} s (see {shot_path} and {log_path})")


def log_text():
    with open(log_path, encoding="utf-8", errors="replace") as f:
        return f.read()


def stored(pattern):
    """The settings file holds a line matching `pattern`."""
    if not os.path.exists(settings_file):
        return False
    with open(settings_file, encoding="utf-8") as f:
        return re.search(pattern, f.read()) is not None


def set_option(text, group, key, value):
    """`text`, a settings file, with `key` of `group` set to `value`."""
    if re.search(rf"(?m)^\[{group}\]$", text) is None:
        text = text.rstrip("\n") + f"\n\n[{group}]\n"
    section = re.search(rf"(?ms)^\[{group}\]\n.*?(?=^\[|\Z)", text)
    body = re.sub(rf"(?m)^{key}=.*\n?", "", section.group(0))
    return text[:section.start()] + body.replace(f"[{group}]\n", f"[{group}]\n{key}={value}\n", 1) + text[section.end():]


def edit_settings(*options):
    """(group, key, value) options written into the settings file, as a user would edit it."""
    text = ""
    if os.path.exists(settings_file):
        with open(settings_file, encoding="utf-8") as f:
            text = f.read()
    for group, key, value in options:
        text = set_option(text, group, key, value)
    os.makedirs(os.path.dirname(settings_file), exist_ok=True)
    with open(settings_file, "w", encoding="utf-8") as f:
        f.write(text)


def find_dialog(title):
    """Window id of a visible dialog with this exact title, waiting for it."""
    found = []
    start = time.monotonic()
    while not found and time.monotonic() - start < DEADLINE_S:
        found = xdotool("search", "--onlyvisible", "--name", f"^{title}$").stdout.split()
        time.sleep(POLL_S)
    return found[0] if found else None


def xdotool(*args):
    return subprocess.run(["xdotool", *args], env=env, capture_output=True, text=True, timeout=30)


def viewer():
    """(title, width, height) of the viewer's window; empty when there is none."""
    for wid in xdotool("search", "--onlyvisible", "--name", "ImageViewer$").stdout.split():
        name = xdotool("getwindowname", wid).stdout.strip()
        info = dict(line.split("=", 1) for line in xdotool("getwindowgeometry", "--shell", wid).stdout.split())
        return name, int(info["WIDTH"]), int(info["HEIGHT"])
    return "", 0, 0


def window_geometry():
    found = xdotool("search", "--sync", "--onlyvisible", "--name", "ImageViewer$")
    for wid in found.stdout.split():
        info = dict(line.split("=", 1) for line in xdotool("getwindowgeometry", "--shell", wid).stdout.split())
        if int(info["WIDTH"]) >= 640:  # the viewer, not a dialog
            return int(info["X"]), int(info["Y"]), int(info["WIDTH"]), int(info["HEIGHT"])
    fail("viewer window not found")


def focus_viewer():
    """Keyboard focus on the viewer again: without a window manager, a dialog that closes leaves
    it on the root window with some Qt builds (seen with Qt 6.12 built from source). Asked again
    until the deadline: a dialog's windows are destroyed for seconds after it is hidden (the file
    dialog over /usr/bin, 12 to 16 s on OpenGL in CI), and an xdotool search that meets one being
    destroyed ends with nothing found (CI run 217)."""
    start = time.monotonic()
    while time.monotonic() - start < DEADLINE_S:
        for wid in xdotool("search", "--onlyvisible", "--name", "ImageViewer$").stdout.split():
            fields = xdotool("getwindowgeometry", "--shell", wid).stdout.split()
            info = dict(line.split("=", 1) for line in fields if "=" in line)
            if int(info.get("WIDTH", 0)) >= 640:  # the viewer, not a dialog
                xdotool("windowfocus", "--sync", wid)
                return
        time.sleep(POLL_S)
    fail("viewer window not found")


def fail(message):
    print(f"FAIL: {rhi}: {message}")
    raise SystemExit(1)


class CursorImage(ctypes.Structure):  # XFixesCursorImage
    _fields_ = [("x", ctypes.c_short), ("y", ctypes.c_short), ("width", ctypes.c_ushort),
                ("height", ctypes.c_ushort), ("xhot", ctypes.c_ushort), ("yhot", ctypes.c_ushort),
                ("cursor_serial", ctypes.c_ulong), ("pixels", ctypes.POINTER(ctypes.c_ulong)),
                ("atom", ctypes.c_ulong), ("name", ctypes.c_char_p)]


def open_xfixes():
    x11 = ctypes.CDLL(ctypes.util.find_library("X11") or "libX11.so.6")
    xfixes = ctypes.CDLL(ctypes.util.find_library("Xfixes") or "libXfixes.so.3")
    x11.XOpenDisplay.restype = ctypes.c_void_p
    x11.XOpenDisplay.argtypes = [ctypes.c_char_p]
    x11.XFree.argtypes = [ctypes.c_void_p]
    xfixes.XFixesQueryVersion.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_int)]
    xfixes.XFixesGetCursorImage.restype = ctypes.POINTER(CursorImage)
    xfixes.XFixesGetCursorImage.argtypes = [ctypes.c_void_p]
    connection = x11.XOpenDisplay(display.encode())
    major, minor = ctypes.c_int(6), ctypes.c_int(0)
    if not connection or not xfixes.XFixesQueryVersion(connection, ctypes.byref(major), ctypes.byref(minor)):
        fail("XFixes is not available on the X server")

    def pointer_hidden():
        """The pointer's image is blank (Qt's BlankCursor): every pixel fully transparent."""
        image = xfixes.XFixesGetCursorImage(connection)
        if not image:
            fail("XFixesGetCursorImage failed")
        c = image.contents
        hidden = not any(c.pixels[i] >> 24 for i in range(c.width * c.height))
        x11.XFree(image)
        return hidden
    return pointer_hidden


def watch_iconify():
    """A test of whether the viewer asked the window manager to minimize it since this call: the
    ICCCM's WM_CHANGE_STATE message with IconicState, which Qt sends to the root window (there is
    no window manager here to act on it)."""
    x11 = ctypes.CDLL(ctypes.util.find_library("X11") or "libX11.so.6")
    x11.XOpenDisplay.restype = ctypes.c_void_p
    x11.XOpenDisplay.argtypes = [ctypes.c_char_p]
    x11.XDefaultRootWindow.restype = ctypes.c_ulong
    x11.XDefaultRootWindow.argtypes = [ctypes.c_void_p]
    x11.XInternAtom.restype = ctypes.c_ulong
    x11.XInternAtom.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
    x11.XSelectInput.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_long]
    x11.XPending.argtypes = [ctypes.c_void_p]
    x11.XNextEvent.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
    connection = x11.XOpenDisplay(display.encode())
    if not connection:
        fail("cannot open the X display")
    change_state = x11.XInternAtom(connection, b"WM_CHANGE_STATE", 0)
    x11.XSelectInput(connection, x11.XDefaultRootWindow(connection), 1 << 17)  # StructureNotifyMask
    x11.XPending(connection)  # sends the request
    event = (ctypes.c_long * 24)()  # an XEvent; as XClientMessageEvent on LP64: type, ..., message_type
                                    # at word 5, format at 6, data.l[0] at 7

    def iconify_requested():
        while x11.XPending(connection):
            x11.XNextEvent(connection, event)
            if event[0] & 0xFFFFFFFF == 33 and event[5] == change_state and event[7] == 3:  # ClientMessage, IconicState
                return True
        return False
    return iconify_requested
