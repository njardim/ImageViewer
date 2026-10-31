#!/usr/bin/env python3
"""Interaction test on Xvfb, driven like a user with xdotool (decisions D-29, D-31).

Three small noise images (asymmetric, so every flip and rotation is distinguishable) are
opened from a folder; every step waits until the expected pixels are on screen, exactly:
  1. at startup the top overlay and the gallery show and the information panel does not
     (D-81), and G hides the gallery (its thumbnails would be exact copies of the test images);
     hovering the right edge shows the "next" button; clicking it shows the next image, and the
     left edge goes back;
  2. H, V, R and Shift+R flip and rotate the view (compared with numpy's flips/rotations);
  3. Delete asks for confirmation, Return moves the file to the trash, the next image follows;
  4. Ctrl+Z restores it (no stale trash record); stepping to a neighbour uses the preload
     cache (log); F2 renames, and refuses the name of another file that differs in case only;
     Shift+Delete always asks (Return cancels) and then deletes;
     a file created, deleted or rewritten by another program shows up, goes away or is
     shown again (folder watching); I hides and shows the top overlay without moving the image
     by a pixel (E14); with the information panel on (P), a notice (Z) is a panel of its own
     16 px above it (D-81); B shows the checkerboard behind a transparent image; Apply in the
     Settings saves a change while the dialog stays open, and Cancel keeps it; Font size Large
     enlarges the top overlay's text (D-85); Open… chosen in the context menu, which runs once
     the menu is gone, opens the file dialog (D-85);
  5. Q quits and the session (last file, window geometry) is in the settings file;
  6. zoom modes, zoom lock and the title bar (D-51), read from the window title with every
     detail on: fit to the window, U (a user shortcut for Fit to Width, D-52) fits the width,
     1 is 100 %, Z keeps it for the next image; the mode chosen last stays for the next
     images and is saved, a zoom by hand lasts for its image (D-65); H centres a wide image,
     its two halves meeting in the middle of the window (D-85); the window takes an image's
     size within 50 % of the screen;
  7. shortcuts (D-52): an unknown key name in the settings leaves the defaults, Shift+1 runs
     the command bound to "!" before Actual Size ("1"), and a key recorded in the Shortcuts
     tab replaces the first shortcut while the others stay; O opens a file, Shift+O another
     application (D-79), M asks the window manager to minimize the viewer (D-78);
  8. a still pointer hides after 2 s and a move shows it again; T and C, toggled from the
     keyboard, are saved as settings (D-56); I and the overlay's checkbox in Settings are one
     switch; the Settings dialog opens again on the tab it last closed on, after a restart too
     (D-84);
  9. the title in the overlay's style with its fields (D-58); the gallery (D-60, D-65): G shows
     it, the current thumbnail on the highlight colour and the hovered one fainter, a click
     shows a thumbnail's image, the ring past the first image, the wheel scrolls, a file saved
     again by an editor gets a new thumbnail (D-82), G hides it;
     large thumbnails and another highlight from the settings;
 10. an unreadable file shows the warning sign in the middle (D-63).
The application runs with its own HOME, XDG_CONFIG_HOME and XDG_DATA_HOME (trash), so the
user's settings and trash are never touched.

usage: python3 tests/ui_test.py <ImageViewer> [vulkan|opengl]
needs: Xvfb, xdotool, xwd (x11-apps), ImageMagick `convert`, libXfixes, numpy, Pillow
env:   UI_TEST_DIR  where to keep the files, the log and the last screenshot (default: a temp dir)
"""
import configparser
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
from PIL import Image

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
# A reused UI_TEST_DIR must not carry an earlier run's settings or trash into this one.
for d in (home, config, os.path.join(work, "zoom"), os.path.join(work, "gallery")):
    shutil.rmtree(d, ignore_errors=True)
for d in (pictures, config, data):
    os.makedirs(d, exist_ok=True)
log_path = os.path.join(work, f"viewer-{rhi}.log")
shot_path = os.path.join(work, f"screen-{rhi}.png")
settings_file = os.path.join(config, "Cristallumnis", "ImageViewer.conf")

refs = {}
for i, name in enumerate(("a", "b", "c", "e")):
    pixels = np.random.default_rng(i + 1).integers(0, 256, (64, 96, 3), dtype=np.uint8)
    if name != "e":  # e.png is created later, by "another program"
        Image.fromarray(pixels).save(os.path.join(pictures, f"{name}.png"))
    refs[name] = pixels.astype(int)


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
        if app.poll() is not None:
            fail(f"{what}: ImageViewer exited with code {app.returncode}")
        screen = grab()
        if screen is not None and find(screen, ref) is not None:
            print(f"ok   {what} ({time.monotonic() - start:.1f} s)")
            return
        time.sleep(POLL_S)
    fail(f"{what}: not on screen after {DEADLINE_S} s (see {shot_path})")


def wait_until(what, condition):
    start = time.monotonic()
    while time.monotonic() - start < DEADLINE_S:
        if app.poll() is not None:
            fail(f"{what}: ImageViewer exited with code {app.returncode}")
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


def overlay_visible():
    """The top overlay's text in the band below the window's top edge."""
    shot = grab()
    if shot is None:
        return False
    band = shot[y + 8:y + 48, x + w // 2 - 150:x + w // 2 + 150]
    return (band.min(axis=2) > 200).sum() > 30


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


server, display = xvfb.start("1600x1000x24")
env = dict(os.environ, DISPLAY=display, QT_QPA_PLATFORM="xcb", IMAGEVIEWER_RHI=rhi, IMAGEVIEWER_OUTPUT="sdr",
           LC_ALL="C.UTF-8", LANGUAGE="en", HOME=home, XDG_CONFIG_HOME=config, XDG_DATA_HOME=data,
           QT_LOGGING_RULES="imageviewer.*=true")
app = None
try:
    with open(log_path, "w") as log:
        app = subprocess.Popen([exe, os.path.join(pictures, "a.png")], env=env, stdout=log, stderr=log)
        try:
            wait_for("first image shown", refs["a"])
            x, y, w, h = window_geometry()

            # 1. The defaults at startup (D-81): the top overlay shows; the gallery shows, a.png's
            # thumbnail (the first of three, on the left) on the emerald tile of the current image,
            # which shows above the 96 × 64 image within its 96 px square; no information panel,
            # whose text would be the only light pixels left of the gallery.
            wait_until("at startup the top overlay shows", overlay_visible)

            def gallery_shown():
                shot = grab()
                if shot is None:
                    return False
                tile = shot[y + h - 16 - 8 - 96 + 5, x + w // 2 - 104]
                return tile[1] > tile[0] + 40 and tile[1] > tile[2] + 20
            wait_until("at startup the gallery shows, the current thumbnail on its emerald tile", gallery_shown)
            shot = grab()
            if shot is None or (shot[y + h // 2 + 40:y + h - 16, x + 16:x + 16 + 200].min(axis=2) > 200).any():
                fail(f"the information panel shows at startup (see {shot_path})")
            print("ok   at startup the information panel does not show")
            xdotool("key", "g")
            wait_until("G hides the gallery and saves it", lambda: stored(r"(?m)^show=false$"))

            # Side zones: the button appears on hover, a click navigates.
            xdotool("mousemove", "--sync", str(x + w - 60), str(y + h // 2))
            start = time.monotonic()
            while True:  # the chevron is white on the dark button, in the middle of the 100 px zone
                screen = grab()
                cx, cy = x + w - 50, y + h // 2
                if screen is not None and (screen[cy - 20:cy + 20, cx - 20:cx + 20].min(axis=2) > 220).any():
                    print(f"ok   next button shown on hover ({time.monotonic() - start:.1f} s)")
                    break
                if time.monotonic() - start > DEADLINE_S:
                    fail(f"next button not shown on hover (see {shot_path})")
                time.sleep(POLL_S)
            xdotool("click", "1")
            wait_for("right side click shows the next image", refs["b"])
            xdotool("mousemove", "--sync", str(x + 60), str(y + h // 2))
            xdotool("click", "1")
            wait_for("left side click shows the previous image", refs["a"])
            xdotool("mousemove", "--sync", str(x + w // 2), str(y + h // 4))  # away from the zones
            xdotool("key", "Right")
            wait_for("Right key shows the next image", refs["b"])

            # 2. View transforms (screen space: flips and quarter turns of what is shown).
            b = refs["b"]
            for key, expected, what in (("shift+w", b[:, ::-1], "Shift+W flips horizontally"), ("shift+w", b, "Shift+W again restores"),
                                        ("shift+h", b[::-1], "Shift+H flips vertically"), ("shift+h", b, "Shift+H again restores"),
                                        ("r", np.rot90(b, -1), "R rotates clockwise"),
                                        ("shift+r", b, "Shift+R rotates back")):
                xdotool("key", key)
                wait_for(what, expected)

            # 3. Move to trash, with the confirmation dialog (on by default).
            xdotool("key", "Delete")
            dialogs = []
            start = time.monotonic()
            while not dialogs and time.monotonic() - start < DEADLINE_S:
                dialogs = xdotool("search", "--onlyvisible", "--name", "^ImageViewer$").stdout.split()
                time.sleep(POLL_S)
            if not dialogs:
                fail("Delete did not ask for confirmation")
            if not os.path.exists(os.path.join(pictures, "b.png")):
                fail("b.png was moved before the confirmation")
            print("ok   Delete asks for confirmation first")
            xdotool("windowfocus", "--sync", dialogs[0])
            xdotool("key", "Return")
            wait_for("after the trash, the next image is shown", refs["c"])
            trashed = os.path.join(data, "Trash", "files", "b.png")
            if os.path.exists(os.path.join(pictures, "b.png")) or not os.path.exists(trashed):
                fail(f"b.png was not moved to {os.path.dirname(trashed)}")
            print("ok   b.png is in the trash, not in the folder")

            # 4. Undo, preloading, rename, permanent delete, folder watching, top overlay.
            xdotool("key", "ctrl+z")
            wait_for("Ctrl+Z restores the trashed image", refs["b"])
            info = os.path.join(data, "Trash", "info", "b.png.trashinfo")
            if not os.path.exists(os.path.join(pictures, "b.png")) or os.path.exists(trashed) or os.path.exists(info):
                fail("b.png not restored cleanly (file back in the folder, no trash record left)")
            print("ok   b.png back in the folder, trash record removed")

            # c.png was the image on screen before the undo, and is b's next neighbour.
            cache_hits = log_text().count("shown from the cache: c.png")
            xdotool("key", "Right")
            wait_for("Right shows the next image again", refs["c"])
            wait_until("it came from the preload cache",
                       lambda: log_text().count("shown from the cache: c.png") > cache_hits)

            xdotool("key", "F2")
            rename = find_dialog("Rename")
            if not rename:
                fail("F2 did not open the Rename dialog")
            xdotool("windowfocus", "--sync", rename)
            xdotool("type", "--delay", "50", "d")  # replaces the selected "c", keeps ".png"
            xdotool("key", "Return")
            wait_until("F2 renames c.png to d.png",
                       lambda: os.path.exists(os.path.join(pictures, "d.png"))
                       and not os.path.exists(os.path.join(pictures, "c.png")))
            wait_for("the renamed image stays on screen", refs["c"])
            # Beside another file "D.png" (a case-sensitive file system), the name "D.png" is refused
            # in the dialog: Return does nothing (D-83). It was accepted, and the rename failed after.
            Image.fromarray(refs["e"].astype(np.uint8)).save(os.path.join(pictures, "D.png"))
            xdotool("key", "F2")
            rename = find_dialog("Rename")
            if not rename:
                fail("F2 did not open the Rename dialog")
            xdotool("windowfocus", "--sync", rename)
            xdotool("type", "--delay", "50", "D")  # replaces the selected "d", keeps ".png"
            xdotool("key", "Return")
            time.sleep(1.0)
            if not xdotool("search", "--onlyvisible", "--name", "^Rename$").stdout.split():
                fail("the Rename dialog took the name of another file, D.png beside d.png")
            print("ok   a name that is another file's but for its case is refused in the dialog")
            xdotool("key", "Escape")
            wait_until("Rename closes", lambda: not xdotool("search", "--onlyvisible", "--name", "^Rename$").stdout.split())
            os.remove(os.path.join(pictures, "D.png"))
            time.sleep(1.0)  # the folder listed again without it (changes settle in 0.3 s)
            focus_viewer()

            xdotool("key", "shift+Delete")
            box = find_dialog("ImageViewer")
            if not box:
                fail("Shift+Delete did not ask for confirmation")
            xdotool("windowfocus", "--sync", box)
            xdotool("key", "Return")  # the default button is Cancel
            wait_until("Return closes the confirmation",
                       lambda: not xdotool("search", "--onlyvisible", "--name", "^ImageViewer$").stdout.split())
            if not os.path.exists(os.path.join(pictures, "d.png")):
                fail("Return deleted the file: Cancel must be the default")
            print("ok   Shift+Delete asks first; Return cancels")
            xdotool("key", "shift+Delete")
            box = find_dialog("ImageViewer")
            if not box:
                fail("Shift+Delete did not ask for confirmation the second time")
            xdotool("windowfocus", "--sync", box)
            xdotool("key", "Tab")  # from Cancel to Delete
            xdotool("key", "space")
            wait_until("Shift+Delete then Delete removes d.png for good",
                       lambda: not os.path.exists(os.path.join(pictures, "d.png")))
            if os.path.exists(os.path.join(data, "Trash", "files", "d.png")):
                fail("d.png went to the trash instead of being deleted")
            wait_for("the previous image takes its place", refs["b"])

            # Another program adds a file, then deletes the one on screen.
            Image.fromarray(refs["e"].astype(np.uint8)).save(os.path.join(pictures, "e.png"))

            def end_shows_e():
                xdotool("key", "End")
                time.sleep(0.5)
                screen = grab()
                return screen is not None and find(screen, refs["e"]) is not None
            wait_until("a file created by another program appears in the folder", end_shows_e)
            os.remove(os.path.join(pictures, "e.png"))
            wait_for("the image deleted by another program is replaced", refs["b"])
            # An editor saves new content into the file on screen: it is decoded again.
            Image.fromarray(refs["e"].astype(np.uint8)).save(os.path.join(pictures, "b.png"))
            wait_for("an image rewritten by another program is shown again", refs["e"])

            # E14: the top overlay, shown by default, hides and shows again without moving or
            # changing the image.
            screen = grab()
            before = find(screen, refs["e"]) if screen is not None else None
            if before is None:
                fail("image not found before hiding the top overlay")

            def image_in_place():
                shot = grab()
                return shot is not None and find(shot, refs["e"]) == before
            xdotool("mousemove", "--sync", str(x + w // 2), str(y + h // 2))
            xdotool("key", "i")
            wait_until("I hides the top overlay, the image stays exactly in place",
                       lambda: not overlay_visible() and image_in_place())
            xdotool("key", "i")
            wait_until("I shows it again, the image still exactly in place",
                       lambda: overlay_visible() and image_in_place())

            # A notice is a panel of its own, 16 px above the information panel (D-60, D-81): down
            # the window's left margin, just inside both panels' padding, the rows of their dark
            # background (black at 70 % over #212121) come in two runs, 16 px apart.
            def panel_runs():
                shot = grab()
                if shot is None:
                    return []
                dark = list(shot[y:y + h, x + 16 + 9].max(axis=1) < 20) + [False]
                runs, start = [], None
                for row, d in enumerate(dark):
                    if d and start is None:
                        start = row
                    elif not d and start is not None:
                        runs.append((start, row))
                        start = None
                return runs
            xdotool("key", "p")
            wait_until("P shows the information panel", lambda: len(panel_runs()) == 1)
            xdotool("key", "z")
            wait_until("Z's notice is a panel of its own, 16 px above the information panel",
                       lambda: (lambda r: len(r) == 2 and abs(r[1][0] - r[0][1] - 16) <= 1)(panel_runs()))
            xdotool("key", "z", "p")
            wait_until("Z unlocks the zoom, P hides the panel, both saved",
                       lambda: stored(r"(?m)^lockZoom=false$") and stored(r"(?m)^showInfo=false$"))

            # Checkerboard: a fully transparent image shows 8-pixel cells of the background
            # (#212121) and of a lighter grey, anchored to the image's top-left corner.
            Image.new("RGBA", (96, 64), (255, 0, 0, 0)).save(os.path.join(pictures, "f.png"))
            cells = (np.add.outer(np.arange(64) // 8, np.arange(96) // 8) % 2).astype(bool)

            def image_area(shot, dx, dy):
                cx, cy = x + (w - 96) // 2 + dx, y + (h - 64) // 2 + dy
                area = shot[cy:cy + 64, cx:cx + 96]
                return area if area.shape[:2] == (64, 96) else None

            def matches(expect_dark, expect_light):
                xdotool("key", "shift+Right")  # the last image (D-48; End is its second shortcut)
                shot = grab()
                for dy in (-1, 0, 1):  # the centred image may sit half a pixel either way
                    for dx in (-1, 0, 1):
                        area = image_area(shot, dx, dy) if shot is not None else None
                        if area is not None and np.abs(area[~cells] - expect_dark).max() <= 2 \
                                and np.abs(area[cells] - expect_light).max() <= 2:
                            return True
                return False
            # Before B: nothing but the background where the transparent image lies.
            wait_until("Shift+Right shows the transparent image over the plain background", lambda: matches(0x21, 0x21))
            xdotool("key", "b")
            wait_until("B shows the checkerboard behind the transparent image", lambda: matches(0x21, 0x40))

            # Settings: Apply takes effect while the dialog stays open, and Cancel keeps it.
            def trash_confirmation_off():
                return stored(r"(?m)^confirmTrash=false$")

            def settings_open():
                return bool(xdotool("search", "--onlyvisible", "--name", "^Settings$").stdout.split())
            xdotool("key", "ctrl+comma")
            dialog = find_dialog("Settings")
            if not dialog:
                fail("Ctrl+, did not open the Settings dialog")
            xdotool("windowfocus", "--sync", dialog)
            xdotool("key", "Tab", "Tab", "Tab", "space")  # past the language and font size lists: "Confirm before…" off
            info = dict(line.split("=", 1) for line in xdotool("getwindowgeometry", "--shell", dialog).stdout.split())
            xdotool("mousemove", "--sync", str(int(info["X"]) + int(info["WIDTH"]) - 50),
                    str(int(info["Y"]) + int(info["HEIGHT"]) - 24))
            xdotool("click", "1")  # Apply, the rightmost button
            wait_until("Apply saves the change while the dialog stays open", trash_confirmation_off)
            if not settings_open():
                fail("Apply closed the Settings dialog")
            xdotool("key", "Escape")
            wait_until("Cancel closes the Settings dialog", lambda: not settings_open())
            if not trash_confirmation_off():
                fail("Cancel undid what Apply had applied")

            # Font size (General, after Language, D-85) enlarges the text over the image as well.
            def overlay_text_height():
                shot = grab()
                band = shot[y:y + 80, x + w // 2 - 300:x + w // 2 + 300]
                rows = np.nonzero((band.min(axis=2) > 200).any(axis=1))[0]
                return int(rows.max() - rows.min() + 1) if rows.size else 0
            small_text = overlay_text_height()
            xdotool("key", "ctrl+comma")
            dialog = find_dialog("Settings")
            if not dialog:
                fail("Ctrl+, did not open the Settings dialog again")
            xdotool("windowfocus", "--sync", dialog)
            xdotool("key", "Tab", "Tab", "End")  # Font size: Large
            info = dict(line.split("=", 1) for line in xdotool("getwindowgeometry", "--shell", dialog).stdout.split())
            xdotool("mousemove", "--sync", str(int(info["X"]) + int(info["WIDTH"]) - 50),
                    str(int(info["Y"]) + int(info["HEIGHT"]) - 24))
            xdotool("click", "1")  # Apply
            wait_until("Font size Large is saved", lambda: stored(r"(?m)^fontSize=large$"))
            xdotool("key", "Escape")
            wait_until("the Settings dialog closes", lambda: not settings_open())
            wait_until("... and the top overlay's text has grown with it", lambda: overlay_text_height() > small_text > 0)

            # A context menu's choice runs once the menu is gone, from the event loop, as a key's
            # command does (D-85): its first item, Open…, opens the file dialog.
            focus_viewer()
            xdotool("mousemove", "--sync", str(x + w // 2), str(y + h // 2))
            xdotool("click", "3")
            time.sleep(1)
            xdotool("key", "Down", "Return")
            dialog = find_dialog("Open Image")
            if not dialog:
                fail("Open… in the context menu did not open the file dialog")
            print("ok   Open… in the context menu opens the file dialog")
            xdotool("windowfocus", "--sync", dialog)
            xdotool("key", "Escape")
            wait_until("the file dialog closes", lambda: not xdotool("search", "--onlyvisible", "--name", "^Open Image$").stdout.split())
            focus_viewer()

            # 5. Quit (Q, D-55); the session is saved.
            xdotool("key", "q")
            app.wait(15)
            if app.returncode != 0:
                print(log_text())
                fail(f"ImageViewer exited with code {app.returncode}")
            store = configparser.ConfigParser(interpolation=None)
            store.read(settings_file)
            last = store.get("session", "lastFile", fallback="")
            if os.path.basename(last) != "f.png" or not store.get("session", "geometry", fallback="").startswith("@Rect("):
                fail(f"session not saved as expected in {settings_file}: lastFile={last!r}")
            print("ok   session saved (last file, window geometry)")

            # 6. Zoom modes, zoom lock and the title bar (D-51).
            zoom_dir = os.path.join(work, "zoom")
            os.makedirs(zoom_dir)
            for name, size, colour in (("big1.png", (2400, 1800), (200, 60, 30)), ("big2.png", (2400, 1800), (30, 60, 200)),
                                       ("small.png", (400, 300), (60, 200, 30))):
                Image.new("RGB", size, colour).save(os.path.join(zoom_dir, name))

            def set_option(text, group, key, value):
                if re.search(rf"(?m)^\[{group}\]$", text) is None:
                    text = text.rstrip("\n") + f"\n\n[{group}]\n"
                section = re.search(rf"(?ms)^\[{group}\]\n.*?(?=^\[|\Z)", text)
                body = re.sub(rf"(?m)^{key}=.*\n?", "", section.group(0))
                return text[:section.start()] + body.replace(f"[{group}]\n", f"[{group}]\n{key}={value}\n", 1) + text[section.end():]

            def edit_settings(*options):
                with open(settings_file, encoding="utf-8") as f:
                    text = f.read()
                for group, key, value in options:
                    text = set_option(text, group, key, value)
                with open(settings_file, "w", encoding="utf-8") as f:
                    f.write(text)

            def viewer():
                for wid in xdotool("search", "--onlyvisible", "--name", "ImageViewer$").stdout.split():
                    name = xdotool("getwindowname", wid).stdout.strip()
                    info = dict(line.split("=", 1) for line in xdotool("getwindowgeometry", "--shell", wid).stdout.split())
                    return name, int(info["WIDTH"]), int(info["HEIGHT"])
                return "", 0, 0

            def title_zoom(name, expected):
                def check():
                    d = "   ·   "  # the top overlay's separator, which the title shares (D-58)
                    title, width, height = viewer()
                    m = re.fullmatch(rf"{re.escape(name)}{d}[\d,]+ / [\d,]+{d}[\d,]+ × [\d,]+{d}.+{d}([\d.]+) % — ImageViewer",
                                 title)  # numbers as QLocale writes them: 1,200
                    want = expected(width, height)
                    return m is not None and abs(float(m.group(1)) - want) <= 0.06
                return check

            edit_settings(("window", "title", "everything"), ("shortcuts", "FitWidth", "U"),  # D-52
                          ("app", "fontSize", "small"))
            app = subprocess.Popen([exe, os.path.join(zoom_dir, "big1.png")], env=env, stdout=log, stderr=log)
            fit = lambda w, h: round(min(w / 2400, h / 1800, 1.0) * 100, 1)
            wait_until("the title shows the name, position, size, file size and fitted zoom", title_zoom("big1.png", fit))
            xdotool("key", "u")
            wait_until("U, the user's shortcut for Fit to Width (D-52), fits the width", title_zoom("big1.png", lambda w, h: round(min(w / 2400, 1.0) * 100, 1)))
            xdotool("key", "1")
            wait_until("1 shows 100 %", title_zoom("big1.png", lambda w, h: 100.0))
            xdotool("key", "z")
            xdotool("key", "Right")
            wait_until("with the zoom locked (Z), the next image keeps 100 %", title_zoom("big2.png", lambda w, h: 100.0))
            xdotool("key", "z")
            xdotool("key", "Left")
            # D-65: the mode chosen last (Actual Size, 1) stays for the next images, locked or not.
            wait_until("unlocked, the next image keeps the mode chosen last (Actual Size)",
                       title_zoom("big1.png", lambda w, h: 100.0))
            xdotool("key", "u")
            xdotool("key", "Right")
            wait_until("Fit to Width chosen, the next image fits its width too",
                       title_zoom("big2.png", lambda w, h: round(min(w / 2400, 1.0) * 100, 1)))
            wait_until("the mode chosen is saved as the zoom of a new image",
                       lambda: stored(r"(?m)^fitMode=width$"))
            xdotool("key", "minus")  # a zoom by hand lasts for its image only, unless locked
            xdotool("key", "Left")
            wait_until("after a zoom by hand, the next image opens in the mode again",
                       title_zoom("big1.png", lambda w, h: round(min(w / 2400, 1.0) * 100, 1)))
            xdotool("key", "0")  # back to Fit to Window for the sections below
            wait_until("Fit to Window is saved", lambda: stored(r"(?m)^fitMode=window$"))
            xdotool("key", "q")
            app.wait(15)

            # Fit to Height centres the image (D-85): equal margins on both sides, even outside the
            # window. Alone in its folder, the image has no navigation buttons over its edges.
            wide_dir = os.path.join(work, "wide")
            os.makedirs(wide_dir)
            wide = Image.new("RGB", (4000, 1000), (200, 60, 30))
            wide.paste((30, 60, 200), (2000, 0, 4000, 1000))
            wide.save(os.path.join(wide_dir, "wide.png"))
            app = subprocess.Popen([exe, os.path.join(wide_dir, "wide.png")], env=env, stdout=log, stderr=log)
            wait_until("wide.png fits the window", title_zoom("wide.png", lambda w, h: round(min(w / 4000, h / 1000, 1.0) * 100, 1)))
            xdotool("key", "h")
            wait_until("H fits the height", title_zoom("wide.png", lambda w, h: round(min(h / 1000, 1.0) * 100, 1)))

            def halves_meet_in_the_middle():
                wx, wy, ww, wh = window_geometry()
                row = grab()[wy + wh // 2, wx:wx + ww].astype(int)
                red = (row[:, 0] > 150) & (row[:, 2] < 80)
                blue = (row[:, 2] > 150) & (row[:, 0] < 80)
                if not red[0] or not blue[-1]:
                    return False
                red_end, blue_start = int(np.argmin(red)), int(np.argmax(blue))  # one blended column at most between
                return (red[:red_end].all() and blue[blue_start:].all() and blue_start - red_end <= 1
                        and abs(red_end - ww // 2) <= 1)
            wait_until("... centred: its halves meet in the middle of the window", halves_meet_in_the_middle)
            xdotool("key", "0")
            wait_until("Fit to Window is saved again", lambda: stored(r"(?m)^fitMode=window$"))
            xdotool("key", "q")
            app.wait(15)

            edit_settings(("window", "matchImage", "every"), ("window", "matchImagePercent", "50"),
                          ("window", "title", "name"))
            app = subprocess.Popen([exe, os.path.join(zoom_dir, "small.png")], env=env, stdout=log, stderr=log)
            wait_until("the window takes the size of a small image (400 × 300)",
                       lambda: viewer() == ("small.png — ImageViewer", 400, 300))
            xdotool("key", "Home")
            # 2400 × 1800 within 50 % of 1600 × 1000: 800 × 500 at most, so 667 × 500.
            wait_until("a large image's window stays within 50 % of the screen, with its aspect ratio",
                       lambda: (lambda t, w, h: t == "big1.png — ImageViewer" and abs(w - 667) <= 1 and h == 500)(*viewer()))
            xdotool("key", "q")
            app.wait(15)

            # 7. Shortcuts (D-52): a hand-written unknown key leaves the defaults; a shifted symbol
            # ("!", Shift+1 on a US keyboard) is matched before the digit (Actual Size); the
            # Shortcuts tab records a key in place of the first shortcut and keeps the others.
            edit_settings(("window", "matchImage", "never"), ("shortcuts", "Next", "Foo"), ("shortcuts", "AboutQt", "!"))
            app = subprocess.Popen([exe, os.path.join(zoom_dir, "small.png")], env=env, stdout=log, stderr=log)
            wait_until("small.png shown", lambda: viewer()[0] == "small.png — ImageViewer")
            xdotool("key", "Right")
            wait_until("Right still goes to the next image when the settings name an unknown key (Next=Foo)",
                       lambda: viewer()[0] == "big1.png — ImageViewer")
            xdotool("key", "shift+1")
            about = find_dialog("About Qt")
            if not about:
                fail("Shift+1 did not run the command bound to \"!\" (About Qt)")
            print("ok   Shift+1 runs the command bound to \"!\", not Actual Size")
            xdotool("windowfocus", "--sync", about)
            xdotool("key", "Escape")
            wait_until("About Qt closes", lambda: not xdotool("search", "--onlyvisible", "--name", "^About Qt$").stdout.split())
            xdotool("key", "v")
            about = find_dialog("About ImageViewer")
            if not about:
                fail("V did not open the About dialog")
            print("ok   V opens About ImageViewer")
            xdotool("windowfocus", "--sync", about)
            xdotool("key", "Escape")
            wait_until("About closes", lambda: not xdotool("search", "--onlyvisible", "--name", "^About ImageViewer$").stdout.split())

            def stored_previous():
                with open(settings_file, encoding="utf-8") as f:
                    return re.search(r'(?m)^Previous="N; PgUp"$', f.read()) is not None
            xdotool("key", "ctrl+comma")
            dialog = find_dialog("Settings")
            if not dialog:
                fail("Ctrl+, did not open the Settings dialog")
            xdotool("windowfocus", "--sync", dialog)
            xdotool("key", "ctrl+shift+Tab", "Tab")  # the last tab, Shortcuts; then its list
            xdotool("type", "--delay", "50", "Previous")  # Previous Image: Left, PgUp
            xdotool("key", "Tab")
            xdotool("key", "n")  # recorded in the Shortcut field in place of Left
            time.sleep(0.5)
            xdotool("key", "Tab", "Tab")  # through the Alternative field, unchanged
            info = dict(line.split("=", 1) for line in xdotool("getwindowgeometry", "--shell", dialog).stdout.split())
            xdotool("mousemove", "--sync", str(int(info["X"]) + int(info["WIDTH"]) - 50),
                    str(int(info["Y"]) + int(info["HEIGHT"]) - 24))
            xdotool("click", "1")  # Apply
            wait_until("a key recorded in the Shortcuts tab replaces the first shortcut, the others stay", stored_previous)
            xdotool("key", "Escape")
            wait_until("Settings closes", lambda: not xdotool("search", "--onlyvisible", "--name", "^Settings$").stdout.split())
            wait_until("and saves the tab it closed on (Shortcuts, the seventh)", lambda: stored(r"(?m)^settingsTab=6$"))
            focus_viewer()
            # The owner's shortcuts (D-78, D-79): O opens a file, Shift+O another application, M minimizes.
            for key, title in (("o", "Open Image"), ("shift+o", "Choose an Application")):
                xdotool("key", key)
                dialog = find_dialog(title)
                if not dialog:
                    fail(f"{key} did not open the dialog \"{title}\"")
                print(f"ok   {key} opens \"{title}\"")
                xdotool("windowfocus", "--sync", dialog)
                xdotool("key", "Escape")
                wait_until(f"\"{title}\" closes", lambda: not xdotool("search", "--onlyvisible", "--name", f"^{title}$").stdout.split())
                focus_viewer()
            iconify_requested = watch_iconify()
            xdotool("key", "m")
            wait_until("M asks the window manager to minimize the viewer (WM_CHANGE_STATE, IconicState)", iconify_requested)
            xdotool("key", "q")
            app.wait(15)

            # 8. The pointer and the toggles (D-56): still over the image, the pointer hides after
            # 2 s (the default) and a move shows it again; T and C are saved as settings.
            pointer_hidden = open_xfixes()
            app = subprocess.Popen([exe, os.path.join(zoom_dir, "small.png")], env=env, stdout=log, stderr=log)
            wait_until("small.png shown", lambda: viewer()[0] == "small.png — ImageViewer")
            x, y, w, h = window_geometry()
            xdotool("mousemove", "--sync", str(x + w // 2), str(y + h // 2))
            moved = time.monotonic()
            wait_until("a still pointer hides", pointer_hidden)
            if time.monotonic() - moved < 1.8:
                fail(f"the pointer hid {time.monotonic() - moved:.1f} s after a move, before 2 s")
            xdotool("mousemove", "--sync", str(x + w // 2 + 20), str(y + h // 2))
            wait_until("a move shows the pointer again", lambda: not pointer_hidden())
            xdotool("key", "t", "c")
            wait_until("T and C toggled from the keyboard are saved as settings",
                       lambda: stored(r"(?m)^toneMap=false$") and stored(r"(?m)^clipWarning=true$"))

            # The top overlay's checkbox in Settings (D-58) and I are one switch: I hides the overlay
            # (shown again by the E14 check), the dialog opens with the box clear, and ticking it
            # with Apply shows the overlay again.
            wait_until("the top overlay is shown (saved by the E14 check)", overlay_visible)
            xdotool("key", "i")
            wait_until("I hides it and saves the switch",
                       lambda: not overlay_visible() and stored(r"(?m)^showOverlay=false$"))
            xdotool("key", "ctrl+comma")
            dialog = find_dialog("Settings")
            if not dialog:
                fail("Ctrl+, did not open the Settings dialog")
            xdotool("windowfocus", "--sync", dialog)
            # It opens on Shortcuts, where it closed before the restart (D-84): three tabs on, past
            # General and Window, Information; past the panel's box, the overlay's.
            xdotool("key", "ctrl+Tab", "ctrl+Tab", "ctrl+Tab", "Tab", "Tab", "space")
            info = dict(line.split("=", 1) for line in xdotool("getwindowgeometry", "--shell", dialog).stdout.split())
            xdotool("mousemove", "--sync", str(int(info["X"]) + int(info["WIDTH"]) - 50),
                    str(int(info["Y"]) + int(info["HEIGHT"]) - 24))
            xdotool("click", "1")  # Apply
            wait_until("the overlay's checkbox, cleared by I, ticked and applied, shows the overlay",
                       lambda: stored(r"(?m)^showOverlay=true$") and overlay_visible())
            xdotool("key", "Escape")
            wait_until("Settings closes", lambda: not xdotool("search", "--onlyvisible", "--name", "^Settings$").stdout.split())
            focus_viewer()
            xdotool("key", "q")
            app.wait(15)

            # 9. The gallery (D-60, D-61, D-65). Twelve images in a 1280 px window: nine thumbnails of the
            # default 96 px, 8 px apart, centred, the current one in the middle; navigation loops, so
            # the gallery is a ring. A click shows a thumbnail's image, the wheel scrolls; behind the
            # current thumbnail and the one under the pointer, the highlight colour (emerald by
            # default; fainter under the pointer).
            gallery_dir = os.path.join(work, "gallery")
            os.makedirs(gallery_dir)
            for i in range(12):
                Image.new("RGB", (160, 120), (20 * i, 100, 200 - 15 * i)).save(os.path.join(gallery_dir, f"g{i + 1:02}.png"))
            edit_settings(("window", "title", "overlay"))  # D-58: the overlay's fields, in its style
            app = subprocess.Popen([exe, os.path.join(gallery_dir, "g01.png")], env=env, stdout=log, stderr=log)
            wait_until("the title shows the overlay's fields in its style",
                       lambda: re.fullmatch(r"g01\.png   ·   160 × 120   ·   .+ — ImageViewer", viewer()[0]) is not None)
            shown = lambda name: lambda: viewer()[0].startswith(name + "   ·   ")
            focus_viewer()
            xdotool("key", "g")
            wait_until("G shows the gallery and saves it", lambda: stored(r"(?m)^show=true$"))
            wid = xdotool("search", "--onlyvisible", "--name", "ImageViewer$").stdout.split()[0]
            xdotool("windowsize", "--sync", wid, "1280", "800")  # the session left a narrower window
            wait_until("the window is 1280 px wide", lambda: window_geometry()[2] == 1280)
            x, y, w, h = window_geometry()

            def slot_centre(slot, thumb=96, enlargement=0):
                """Screen centre of a thumbnail: `slot` from the middle one (0), for an odd count."""
                count = (w - 2 * 128 - 8 - enlargement) // (thumb + 8)
                count -= 1 - count % 2
                if count < 3:
                    fail(f"a {w} px window holds only {count} thumbnails of {thumb} px")
                # Below the bar, the margin: 16 px, or the enlarged tile's reach (100 / 2 + 3 - 8 = 45).
                margin = max(16, enlargement // 2 + 3 - 8)
                return x + w // 2 + slot * (thumb + 8), y + h - margin - 8 - thumb // 2

            def tile_colour(centre_x, top):
                """The highlight above a thumbnail: 160 × 120 fills 96 × 72, the tile shows above it."""
                shot = grab()
                return shot[top + 5, centre_x] if shot is not None else None

            def emerald_at(opacity):
                """Emerald at `opacity` % over the gallery's bar (black at 70 % over the #212121 background)."""
                bar = 0x21 * (1 - round(70 * 2.55) / 255)
                a = round(opacity * 2.55) / 255
                return [c * a + bar * (1 - a) for c in (80, 200, 120)]

            def near(colour, expected):
                return colour is not None and all(abs(int(c) - e) <= 3 for c, e in zip(colour, expected))
            mx, my = slot_centre(0)
            xdotool("mousemove", "--sync", str(x + w // 2), str(y + h // 3))  # off the gallery
            time.sleep(0.5)
            current = tile_colour(mx, my - 48)
            if not near(current, emerald_at(70)):
                fail(f"the current thumbnail's tile is {current}, not emerald at 70 %")
            print(f"ok   the current thumbnail sits on an emerald tile at 70 % {tuple(int(c) for c in current)}")
            hx, hy = slot_centre(2)
            xdotool("mousemove", "--sync", str(hx), str(hy))
            time.sleep(0.5)
            hovered = tile_colour(hx, hy - 48)  # not enlarged: the zoom is off by default (D-81)
            if not near(hovered, emerald_at(50)):  # 20 points fainter (D-85)
                fail(f"the hovered tile is {hovered}, not emerald at 50 %")
            print(f"ok   the thumbnail under the pointer sits on emerald at 50 % {tuple(int(c) for c in hovered)}")
            xdotool("click", "1")
            wait_until("a click on a thumbnail two to the right shows g03.png", shown("g03.png"))
            xdotool("key", "Home")
            wait_until("Home shows g01.png, in the middle again", shown("g01.png"))
            lx, ly = slot_centre(-1)
            xdotool("mousemove", "--sync", str(lx), str(ly))
            xdotool("click", "1")
            wait_until("left of the first image, the ring shows the last: g12.png", shown("g12.png"))
            xdotool("key", "Home")
            wait_until("Home shows g01.png", shown("g01.png"))
            xdotool("mousemove", "--sync", str(mx), str(my))
            xdotool("click", "--repeat", "2", "--delay", "100", "5")  # two wheel steps down
            time.sleep(0.5)
            xdotool("click", "1")
            wait_until("the wheel scrolls the gallery: two steps on, the middle shows g03.png", shown("g03.png"))
            # An editor saves the image beside it as editors do, a new file renamed over the old one
            # (the folder changes): the listing's size and time (D-82) give it a new thumbnail, a
            # 96 × 72 field of the new colour around the slot's centre.
            Image.new("RGB", (160, 120), (250, 250, 0)).save(os.path.join(gallery_dir, ".g04.tmp"), format="PNG")
            os.replace(os.path.join(gallery_dir, ".g04.tmp"), os.path.join(gallery_dir, "g04.png"))
            nx, ny = slot_centre(1)

            def rewritten_thumbnail():
                shot = grab()
                return shot is not None and np.abs(shot[ny, nx] - (250, 250, 0)).max() <= 2
            wait_until("a file saved again by an editor gets a new thumbnail", rewritten_thumbnail)
            xdotool("key", "g")
            wait_until("G again hides the gallery", lambda: stored(r"(?m)^show=false$"))
            xdotool("key", "q")
            app.wait(15)

            # Large thumbnails (128 px) in ruby, enlarged by the most (100 px), chosen in the settings.
            edit_settings(("gallery", "show", "true"), ("gallery", "thumbnailSize", "128"), ("gallery", "highlight", "#e0115f"),
                          ("gallery", "zoom", "true"), ("gallery", "enlargement", "100"))
            app = subprocess.Popen([exe, os.path.join(gallery_dir, "g01.png")], env=env, stdout=log, stderr=log)
            wait_until("g01.png shown with the gallery", shown("g01.png"))
            xdotool("windowsize", "--sync", xdotool("search", "--onlyvisible", "--name", "ImageViewer$").stdout.split()[0],
                    "1280", "800")
            wait_until("the window is 1280 px wide", lambda: window_geometry()[2] == 1280)
            x, y, w, h = window_geometry()
            mx, my = slot_centre(0, thumb=128, enlargement=100)
            xdotool("mousemove", "--sync", str(x + w // 2), str(y + h // 3))
            time.sleep(0.5)
            current = tile_colour(mx, my - 64)
            if current is None or not (current[0] > current[1] + 60 and current[0] > current[2] + 20):
                fail(f"the current thumbnail's tile is {current}, not ruby")
            print(f"ok   the highlight follows the setting: ruby {tuple(current)}")
            rx, ry = slot_centre(1, thumb=128, enlargement=100)
            xdotool("mousemove", "--sync", str(rx), str(ry))

            def tile_at_bottom():
                # 228 px enlarged, its tile 3 px around: it reaches past the 16 px margin, down to
                # the window's bottom row (D-61).
                shot = grab()
                if shot is None:
                    return False
                pixel = shot[y + h - 1, rx]
                return pixel[0] > pixel[1] + 40 and pixel[0] > pixel[2] + 10
            wait_until("enlarged by 100 px, the hovered thumbnail's tile reaches the window's bottom", tile_at_bottom)
            xdotool("click", "1")
            wait_until("128 px thumbnails: a click on the next one shows g02.png", shown("g02.png"))
            xdotool("key", "q")
            app.wait(15)

            # 10. An error (D-63): a file that cannot be read shows the warning sign in the middle
            # of the window, its message below it; the sign's amber is drawn exactly in SDR.
            broken = os.path.join(work, "broken.png")
            with open(broken, "wb") as f:
                f.write(b"\x89PNG\r\n\x1a\nnot an image")
            app = subprocess.Popen([exe, broken], env=env, stdout=log, stderr=log)

            def warning_in_the_middle():
                screen = grab()
                if screen is None:
                    return False
                x, y, w, h = window_geometry()
                ys, xs = np.nonzero(np.all(screen[y:y + h, x:x + w] == (255, 196, 0), axis=2))
                # The sign above the middle (the message is below it), centred across.
                return len(xs) > 2000 and abs(xs.mean() - w / 2) < 3 and h / 2 - 160 < ys.mean() < h / 2
            wait_until("an unreadable file shows the warning sign in the middle", warning_in_the_middle)
            xdotool("key", "q")
            app.wait(15)

            # 11 and 12 need the SVG renderer (scripts/build-svg.sh): a build without it skips them,
            # CI ($CI set) fails instead.
            if not svg_renderer:
                if os.environ.get("CI"):
                    fail("no SVG renderer in this build")
                print("skip 11 and 12: no SVG renderer in this build (scripts/build-svg.sh)")
            else:
                # 11. The zoom's detail (D-93): an SVG enlarged on screen is drawn again at the zoom
                # shown, in every orientation. A line a quarter of a pixel wide, on the left half of the
                # image, is a light grey at 100 %; at 931 % the enlarged image keeps that grey, and the
                # detail draws it black and 2 px wide, where the view turns and mirrors it.
                line_dir = os.path.join(work, "line")
                os.makedirs(line_dir)
                line = os.path.join(line_dir, "line.svg")
                with open(line, "w") as f:
                    f.write('<svg xmlns="http://www.w3.org/2000/svg" width="200" height="200">'
                            '<rect width="200" height="200" fill="#fff"/>'
                            '<path d="M20 100.5H100" stroke="#000" stroke-width="0.25"/></svg>')
                before = len(log_text())
                app = subprocess.Popen([exe, line], env=env, stdout=log, stderr=log)
                wait_until("the SVG shown", lambda: grab() is not None and "line.svg" in viewer()[0])
                x, y, w, h = window_geometry()
                cx, cy = x + w // 2, y + h // 2

                def sides():
                    """The darkest pixel left, right, above and below the window's centre."""
                    shot = grab()
                    if shot is None:
                        return None
                    s = shot.min(axis=2).astype(int)
                    return (s[cy - 8:cy + 9, cx - 80:cx - 10].min(), s[cy - 8:cy + 9, cx + 10:cx + 80].min(),
                            s[cy - 80:cy - 10, cx - 8:cx + 9].min(), s[cy + 10:cy + 80, cx - 8:cx + 9].min())
                xdotool("key", "1")
                wait_until("at 100 % the line is a light grey", lambda: (s := sides()) and 150 < s[0] < 230)
                for _ in range(10):
                    xdotool("key", "plus")
                wait_until("at 931 % the detail draws the line black, on the left", lambda: sides() == (0, 255, 255, 255))
                rows = (grab().min(axis=2)[cy - 10:cy + 11, cx - 40] < 128).sum()
                if not 2 <= rows <= 3:
                    fail(f"the line is {rows} px wide at 931 %, not 2.3 (see {shot_path})")
                for keys, want, where in ((("r",), (255, 255, 0, 255), "turned clockwise, above"),
                                          (("shift+h",), (255, 255, 255, 0), "flipped, below"),
                                          (("shift+r",), (255, 0, 255, 255), "turned back, on the right")):
                    xdotool("key", *keys)
                    wait_until(f"the detail follows the view: {where}", lambda: sides() == want)

                    # On the screen's pixel grid whatever the turn (D-94): across the line (2.3 px wide), at
                    # most four pixels short of white, one of them black; half a pixel off, the
                    # interpolation spreads it over five, none black. The detail drawn before the turn,
                    # turned by the shader, shows until the one drawn for the new view arrives.
                    def sharp(where=where):
                        shot = grab()
                        if shot is None:
                            return False
                        shot = shot.min(axis=2).astype(int)
                        across = shot[cy - 40, cx - 12:cx + 13] if where.endswith("above") else (
                            shot[cy + 40, cx - 12:cx + 13] if where.endswith("below") else shot[cy - 12:cy + 13, cx + 40])
                        grey = [int(v) for v in across if v < 250]
                        return 1 <= len(grey) <= 4 and min(grey) == 0
                    wait_until(f"... and sharp on the screen's pixel grid", sharp)
                # The details after the first draw the document their worker holds, sent once (D-95).
                if 'SVG document held by its worker: "line.svg"' not in log_text()[before:]:
                    fail("each detail sent and parsed the SVG again")
                xdotool("key", "1")
                wait_until("back at 100 %, the image's own pixels again", lambda: (s := sides()) and 150 < s[1] < 230)
                xdotool("key", "q")
                app.wait(15)
                # 100 % is the document's own size (D-99), sharp in every turn and mirror: an SVG larger
                # than the window, drawn at the window's size (D-94), gets its detail at 1, and its lines
                # one pixel wide are black on white. It was shown at 27 %; at 100 % (its sides rounded
                # up to whole texels) the detail was squeezed off the screen's pixel grid, mirrored too.
                large = os.path.join(line_dir, "large.svg")
                with open(large, "w") as f:
                    f.write('<svg xmlns="http://www.w3.org/2000/svg" width="4001" height="3001">'
                            '<rect width="4001" height="3001" fill="#fff"/>'
                            + "".join(f'<rect y="{v}" width="4001" height="1"/>' for v in range(5, 3001, 10))
                            + "".join(f'<rect x="{u}" width="1" height="3001"/>' for u in range(5, 4001, 10)) + '</svg>')
                before = len(log_text())
                app = subprocess.Popen([exe, large], env=env, stdout=log, stderr=log)
                wait_until("the large SVG shown", lambda: "decoded for display: large.svg" in log_text()[before:])
                focus_viewer()

                def lines_sharp():
                    shot = grab()
                    if shot is None:
                        return False
                    x, y, w, h = window_geometry()
                    middle = shot[y + h // 4:y + 3 * h // 4, x + w // 4:x + 3 * w // 4].astype(int).max(axis=2)
                    return not ((middle > 20) & (middle < 235)).any() and (middle <= 20).mean() > 0.1
                def details_at_100():
                    return len(re.findall(r"detail drawn: large\.svg .* at 1 in ", log_text()[before:]))
                drawn = 0  # the first may come before 1 is pressed: 100 % may be the zoom kept (D-65)
                for keys in ([], ["shift+w"], ["r"]):
                    for key in keys:
                        xdotool("key", key)
                    xdotool("key", "1")
                    what = "at 100 %" + (" after " + " ".join(keys) if keys else "")
                    wait_until(f"{what} the large SVG's detail is drawn one document pixel per device pixel",
                               lambda: details_at_100() > drawn)
                    wait_until(f"{what} its lines one pixel wide are sharp", lines_sharp)
                    drawn = details_at_100()
                xdotool("key", "q")
                app.wait(15)

                # 12. A folder of SVGs (D-94): each decoding thread starts one SVG worker and keeps it,
                # rather than one process per document; the interface answers throughout (the latency
                # log: the event loop more than 100 ms late), the owner's artwork with 17 filters too.
                # The window opens on a PNG of the folder, larger than the window (drawn interpolated, as
                # an SVG always is), and the measure starts once it is up: its first frames build the
                # renderer's pipelines on the GUI thread, and llvmpipe compiles each sampling it meets
                # (half a second to two, a PNG's as an SVG's), which is not reading SVGs.
                svg_dir = os.path.join(work, "svgs")
                os.makedirs(svg_dir)
                for i in range(6):
                    with open(os.path.join(svg_dir, f"s{i}.svg"), "w") as f:
                        f.write(f'<svg xmlns="http://www.w3.org/2000/svg" width="800" height="600">'
                                f'<rect width="800" height="600" fill="#{40 * i:02x}6090"/>'
                                f'<circle cx="400" cy="300" r="{60 + 30 * i}" fill="#fc0"/></svg>')
                shutil.copy(os.path.join(os.path.dirname(os.path.abspath(__file__)), "data", "IV_claude_v5_mix_v1.svg"),
                            os.path.join(svg_dir, "s6.svg"))
                Image.new("RGB", (2400, 1800), (40, 90, 150)).save(os.path.join(svg_dir, "a.png"))
                before = len(log_text())
                app = subprocess.Popen([exe, os.path.join(svg_dir, "a.png")], env=env, stdout=log, stderr=log)
                wait_until("the folder's PNG shown", lambda: "decoded for display: a.png" in log_text()[before:])
                time.sleep(3)  # the window's first frames (its pipelines) are done
                settled = len(log_text())
                for name in ("s0", "s1", "s2", "s3", "s4", "s5", "s6"):
                    xdotool("key", "Right")
                    wait_until(f"{name}.svg shown", lambda name=name: re.search(rf"(decoded for display|shown from the cache): {name}\.svg",
                                                                              log_text()[settled:]) is not None)
                time.sleep(1)
                xdotool("key", "q")
                app.wait(30)
                session = log_text()[before:]
                starts = session.count("SVG worker started")
                if not 1 <= starts <= 4:  # the decode thread, the detail's, the gallery's two
                    fail(f"{starts} SVG workers started for 7 documents and their thumbnails, at most 4 expected")
                print(f"ok   7 SVGs and their thumbnails: {starts} SVG workers started, one per decoding thread")
                # A late tick is the GPU's when render() named a frame as long just before it: llvmpipe
                # (OpenGL on the runners) compiles shader variants and makes mipmaps on the GUI thread.
                # Anything else that held the event loop up is ours, and fails.
                ours, frames, frame = [], [], 0
                for kind, ms in re.findall(r"(a frame took|the event loop answered) (\d+) ms", log_text()[settled:]):
                    if kind == "a frame took":
                        frame = max(frame, int(ms))
                        frames.append(int(ms))
                        continue
                    if int(ms) > 250 and frame < int(ms) - 60:
                        ours.append(int(ms))
                    frame = 0
                if ours:
                    fail(f"the interface did not answer for {max(ours)} ms while SVGs were read, outside drawing ({ours})")
                print(f"ok   the interface answered throughout: no stall but frames drawn ({frames or 'none'} ms)")
        finally:
            if app.poll() is None:
                app.terminate()
                app.wait(10)
finally:
    xvfb.stop(server)

if app.returncode != 0:
    print(open(log_path, encoding="utf-8", errors="replace").read())
    fail(f"ImageViewer exited with code {app.returncode}")
print(f"{rhi}: all interaction checks passed")
