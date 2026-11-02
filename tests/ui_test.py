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
The gallery, an unreadable file's warning sign and an SVG's detail and workers are
tests/ui_views_test.py's (D-102); the harness both share is tests/ui_driver.py.
The application runs with its own HOME, XDG_CONFIG_HOME and XDG_DATA_HOME (trash), so the
user's settings and trash are never touched.

usage: python3 tests/ui_test.py <ImageViewer> [vulkan|opengl]
needs: Xvfb, xdotool, xwd (x11-apps), ImageMagick `convert`, libXfixes, numpy, Pillow
env:   UI_TEST_DIR  where to keep the files, the log and the last screenshot (default: a temp dir)
"""
import configparser
import os
import re
import sys
import time

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ui_driver import (  # noqa: E402  the harness (D-102); it reads the command line
    DEADLINE_S, POLL_S, data, edit_settings, fail, find, find_dialog, focus_viewer, grab, launch, log_path,
    log_text, open_xfixes, pictures, rhi, server, settings_file, shot_path, stored, viewer, wait_for,
    wait_until, watch_iconify, window_geometry, work, xdotool,
)
import xvfb  # noqa: E402

refs = {}
for i, name in enumerate(("a", "b", "c", "e")):
    pixels = np.random.default_rng(i + 1).integers(0, 256, (64, 96, 3), dtype=np.uint8)
    if name != "e":  # e.png is created later, by "another program"
        Image.fromarray(pixels).save(os.path.join(pictures, f"{name}.png"))
    refs[name] = pixels.astype(int)


def overlay_visible():
    """The top overlay's text in the band below the window's top edge."""
    shot = grab()
    if shot is None:
        return False
    band = shot[y + 8:y + 48, x + w // 2 - 150:x + w // 2 + 150]
    return (band.min(axis=2) > 200).sum() > 30


app = None
try:
    with open(log_path, "w") as log:
        app = launch([os.path.join(pictures, "a.png")], log)
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
            app = launch([os.path.join(zoom_dir, "big1.png")], log)
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
            app = launch([os.path.join(wide_dir, "wide.png")], log)
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
            app = launch([os.path.join(zoom_dir, "small.png")], log)
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
            app = launch([os.path.join(zoom_dir, "small.png")], log)
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
            app = launch([os.path.join(zoom_dir, "small.png")], log)
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

