#!/usr/bin/env python3
"""Interaction test on Xvfb, driven like a user with xdotool: what the window draws besides the
image's own pixels (D-102: split from tests/ui_test.py, whose harness tests/ui_driver.py both share):
  1. the title in the overlay's style with its fields (D-58); the gallery (D-60, D-65): G shows
     it, the current thumbnail on the highlight colour and the hovered one fainter, a click
     shows a thumbnail's image, the ring past the first image, the wheel scrolls, a file saved
     again by an editor gets a new thumbnail (D-82), a folder that keeps changing gets every
     thumbnail (D-100), G hides it; large thumbnails and another highlight from the settings;
  2. an unreadable file shows the warning sign in the middle (D-63);
  3. the zoom's detail of an SVG (D-93, D-94, D-99): drawn at the zoom shown, sharp on the
     screen's pixel grid in every turn and mirror, 100 % by the document's own pixels;
  4. a folder of SVGs (D-94): one SVG worker per decoding thread, and the interface answers while
     they are read.
3 and 4 need the SVG renderer (scripts/build-svg.sh): a build without it skips them, CI fails.
The application runs with its own HOME, XDG_CONFIG_HOME and XDG_DATA_HOME (trash), so the
user's settings and trash are never touched.

usage: python3 tests/ui_views_test.py <ImageViewer> [vulkan|opengl]
needs: Xvfb, xdotool, xwd (x11-apps), ImageMagick `convert`, numpy, Pillow
env:   UI_TEST_DIR  where to keep the files, the log and the last screenshot (default: a temp dir)
"""
import os
import re
import shutil
import sys
import threading
import time

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ui_driver import (  # noqa: E402  the harness (D-102); it reads the command line
    edit_settings, fail, focus_viewer, grab, launch, log_path, log_text, rhi, server, shot_path, stored,
    svg_renderer, viewer, wait_until, window_geometry, work, xdotool,
)
import xvfb  # noqa: E402

# The gallery starts hidden: G shows it (the first check), as after tests/ui_test.py's G.
edit_settings(("gallery", "show", "false"))

app = None
try:
    with open(log_path, "w") as log:
        try:
            # 1. The gallery (D-60, D-61, D-65). Twelve images in a 1280 px window: nine thumbnails of the
            # default 96 px, 8 px apart, centred, the current one in the middle; navigation loops, so
            # the gallery is a ring. A click shows a thumbnail's image, the wheel scrolls; behind the
            # current thumbnail and the one under the pointer, the highlight colour (emerald by
            # default; fainter under the pointer).
            gallery_dir = os.path.join(work, "gallery")
            os.makedirs(gallery_dir)
            for i in range(12):
                Image.new("RGB", (160, 120), (20 * i, 100, 200 - 15 * i)).save(os.path.join(gallery_dir, f"g{i + 1:02}.png"))
            edit_settings(("window", "title", "overlay"))  # D-58: the overlay's fields, in its style
            app = launch([os.path.join(gallery_dir, "g01.png")], log)
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

            # A folder that keeps changing (a download beside the images, a tethered camera; D-100):
            # every listing dropped the thumbnails being decoded, so images slower to decode (6000 px,
            # about 1.4 s here) than the changes came (every 0.4 s, each one listed once the folder has
            # been still for 300 ms) never got one. Now only a thumbnail of a file found changed is.
            churn_dir = os.path.join(work, "churn")
            os.makedirs(churn_dir)
            rng = np.random.default_rng(7)
            blocks = np.kron(rng.integers(0, 216, size=(8, 8, 3), dtype=np.uint8), np.ones((750, 750, 1), dtype=np.uint8))
            blocks += rng.integers(0, 40, size=blocks.shape, dtype=np.uint8)
            Image.fromarray(blocks).save(os.path.join(churn_dir, "c0.png"), compress_level=1)
            del blocks
            for i in range(1, 5):
                shutil.copy(os.path.join(churn_dir, "c0.png"), os.path.join(churn_dir, f"c{i}.png"))
            edit_settings(("gallery", "show", "true"))
            stop_churn = threading.Event()

            def churn():
                n = 0
                while not stop_churn.wait(0.4):
                    with open(os.path.join(churn_dir, f"note-{n}.txt"), "w") as f:
                        f.write("x")
                    n += 1
            threading.Thread(target=churn, daemon=True).start()
            app = launch([os.path.join(churn_dir, "c0.png")], log)
            try:
                wait_until("c0.png shown with the gallery", shown("c0.png"))
                x, y, w, h = window_geometry()

                def churned_thumbnails():
                    # Five 96 px thumbnails of 8 × 8 coloured blocks, centred: drawn ones vary (a
                    # deviation of about 60), the placeholders of thumbnails still being decoded do not
                    # (at most 32, the current one's highlight).
                    shot = grab()
                    if shot is None:
                        return False
                    for slot in range(-2, 3):
                        cx, cy = slot_centre(slot)
                        if shot[cy - 30:cy + 30, cx - 30:cx + 30].std() < 45:
                            return False
                    return True
                wait_until("in a folder changing every 0.4 s, every slow image gets its thumbnail", churned_thumbnails)
            finally:
                stop_churn.set()
            xdotool("key", "q")
            app.wait(15)

            # Large thumbnails (128 px) in ruby, enlarged by the most (100 px), chosen in the settings.
            edit_settings(("gallery", "show", "true"), ("gallery", "thumbnailSize", "128"), ("gallery", "highlight", "#e0115f"),
                          ("gallery", "zoom", "true"), ("gallery", "enlargement", "100"))
            app = launch([os.path.join(gallery_dir, "g01.png")], log)
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

            # 2. An error (D-63): a file that cannot be read shows the warning sign in the middle
            # of the window, its message below it; the sign's amber is drawn exactly in SDR.
            broken = os.path.join(work, "broken.png")
            with open(broken, "wb") as f:
                f.write(b"\x89PNG\r\n\x1a\nnot an image")
            app = launch([broken], log)

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

            # 3 and 4 need the SVG renderer (scripts/build-svg.sh): a build without it skips them,
            # CI ($CI set) fails instead.
            if not svg_renderer:
                if os.environ.get("CI"):
                    fail("no SVG renderer in this build")
                print("skip 3 and 4: no SVG renderer in this build (scripts/build-svg.sh)")
            else:
                # 3. The zoom's detail (D-93): an SVG enlarged on screen is drawn again at the zoom
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
                app = launch([line], log)
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
                    wait_until("... and sharp on the screen's pixel grid", sharp)
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
                app = launch([large], log)
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

                # 4. A folder of SVGs (D-94): each decoding thread starts one SVG worker and keeps it,
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
                app = launch([os.path.join(svg_dir, "a.png")], log)
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
            if app is not None and app.poll() is None:
                app.terminate()
                app.wait(10)
finally:
    xvfb.stop(server)

if app is None or app.returncode != 0:
    print(open(log_path, encoding="utf-8", errors="replace").read())
    fail(f"ImageViewer exited with code {app.returncode}")
print(f"{rhi}: all interaction checks passed")

