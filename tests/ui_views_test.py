#!/usr/bin/env python3
"""Interaction test on Xvfb, driven like a user with xdotool: what the window draws besides the
image's own pixels (D-102: split from tests/ui_test.py, whose harness tests/ui_driver.py both share):
  1. the title in the overlay's style with its fields (D-58); the gallery (D-60, D-65): G shows
     it, the current thumbnail on the highlight colour and the hovered one fainter, its tooltip
     (D-116), a click
     shows a thumbnail's image, the ring past the first image, the wheel scrolls, a file saved
     again by an editor gets a new thumbnail (D-82), a folder that keeps changing gets every
     thumbnail (D-100), G hides it; large thumbnails and another highlight from the settings;
  2. an unreadable file shows the warning sign in the middle (D-63);
  3. the zoom's detail of an SVG (D-93, D-94, D-99): drawn at the zoom shown, sharp on the
     screen's pixel grid in every turn and mirror, 100 % by the document's own pixels;
  4. a folder of SVGs (D-94): one SVG worker per decoding thread, and the interface answers while
     they are read;
  5. the toolbar (D-87, D-112): T shows it, the top overlay below it, a click on a button runs its
     command, the slideshow's buttons on it and the slideshow's own toolbar only without them, the
     "…" panel of a narrow window, Settings and Quit on the bar beside it (D-115);
  6. the color picker (D-115): K turns it on, the pointer a cross over the image (D-116), its
     swatch shows the pixel under the pointer as the image shows it, the checks outside the image, a click picks, the file's own value is read
     again, a click on a value copies it, Esc leaves it; a drag in a closed hand; the information
     panel's histogram and its choice from the panel (D-116).
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
    edit_settings, fail, find_dialog, focus_viewer, grab, launch, log_path, log_text, open_xfixes, rhi, server, shot_path,
    stored, svg_renderer, viewer, wait_until, window_geometry, work, xdotool,
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
            # Waited for, not sampled once: a frame of llvmpipe takes up to 0.9 s on a busy runner.
            wait_until("the current thumbnail sits on an emerald tile at 70 %",
                       lambda: near(tile_colour(mx, my - 48), emerald_at(70)))
            hx, hy = slot_centre(2)
            xdotool("mousemove", "--sync", str(hx), str(hy))
            # Not enlarged: the zoom is off by default (D-81); 20 points fainter (D-85).
            wait_until("the thumbnail under the pointer sits on emerald at 50 %",
                       lambda: near(tile_colour(hx, hy - 48), emerald_at(50)))

            def gallery_tip():
                # Its tooltip (D-116) once the pointer rests on it: above the thumbnail, on the hover's
                # emerald; the column above it otherwise holds the background (the image is narrower).
                shot = grab()
                if shot is None:
                    return False
                column = shot[y + h // 2:hy - 60, hx, :3].astype(int)
                return int(((column[:, 1] - column[:, 0]) > 30).sum()) >= 10
            wait_until("resting on a thumbnail shows its tooltip above it", gallery_tip)
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

            # A JPEG's thumbnail is read reduced (D-111): a 3648 x 2736 photograph is read at an eighth
            # of its size (456 x 342, twice the 228 px the gallery asks for) instead of pixel by pixel,
            # and the thumbnail is still the whole image, turned as its EXIF says (orientation 6: the
            # stored bottom left, blue, comes top left).
            reduced_dir = os.path.join(work, "reduced")
            os.makedirs(reduced_dir)
            quadrants = np.zeros((2736, 3648, 3), dtype=np.uint8)
            quadrants[:1368, :1824], quadrants[:1368, 1824:] = (230, 30, 30), (30, 200, 30)   # red, green
            quadrants[1368:, :1824], quadrants[1368:, 1824:] = (30, 30, 230), (230, 220, 30)  # blue, yellow
            exif = Image.Exif()
            exif[0x0112] = 6
            Image.fromarray(quadrants).save(os.path.join(reduced_dir, "p1.jpg"), quality=90, exif=exif)
            del quadrants
            for i in (0, 2, 3, 4):
                Image.new("RGB", (160, 120), (90, 90, 90)).save(os.path.join(reduced_dir, f"p{i}.png"))
            before = len(log_text())
            app = launch([os.path.join(reduced_dir, "p0.png")], log)
            wait_until("p0.png shown with the gallery", shown("p0.png"))
            time.sleep(1)  # the window has taken its size
            x, y, w, h = window_geometry()
            xdotool("mousemove", "--sync", str(x + w // 2), str(y + h // 3))  # off the gallery
            jx, jy = slot_centre(-1)  # five images, all in view in their order: the second, left of the middle

            def turned_quadrants():
                # 72 x 96 in its 96 px place: a quadrant's middle is 18 px and 24 px from the centre.
                shot = grab()
                if shot is None:
                    return False
                expected = {(-18, -24): (30, 30, 230), (18, -24): (230, 30, 30), (18, 24): (30, 200, 30), (-18, 24): (230, 220, 30)}
                return all(abs(int(c) - e) <= 12 for (dx, dy), colour in expected.items()
                           for c, e in zip(shot[jy + dy, jx + dx][:3], colour))
            wait_until("a JPEG's thumbnail shows the whole image, turned as its EXIF says", turned_quadrants)
            if not re.search(r'"p1\.jpg": 456x342, read', log_text()[before:]):
                fail("the JPEG's thumbnail was not read at an eighth of its size (456 x 342)")
            print("ok   a JPEG's thumbnail is read at an eighth of its size, and shows the whole image turned as its EXIF says")
            xdotool("key", "q")
            app.wait(15)

            # Large thumbnails (128 px) in ruby, enlarged by the most (100 px), chosen in the settings.
            edit_settings(("gallery", "show", "true"), ("gallery", "thumbnailSize", "128"), ("app", "highlight", "#e0115f"),
                          ("gallery", "zoom", "true"), ("gallery", "enlargement", "100"))
            app = launch([os.path.join(gallery_dir, "g01.png")], log)
            wait_until("g01.png shown with the gallery", shown("g01.png"))
            xdotool("windowsize", "--sync", xdotool("search", "--onlyvisible", "--name", "ImageViewer$").stdout.split()[0],
                    "1280", "800")
            wait_until("the window is 1280 px wide", lambda: window_geometry()[2] == 1280)
            x, y, w, h = window_geometry()
            mx, my = slot_centre(0, thumb=128, enlargement=100)
            xdotool("mousemove", "--sync", str(x + w // 2), str(y + h // 3))

            def ruby(colour):
                return colour is not None and colour[0] > colour[1] + 60 and colour[0] > colour[2] + 20
            wait_until("the highlight follows the setting: ruby", lambda: ruby(tile_colour(mx, my - 64)))
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

            # 5. The toolbar (D-87, D-112): T shows it at the top margin and saves it, the top overlay
            # below it; a click on a button runs its command; while the slideshow runs, its button is
            # stop and pause, and the slideshow's own toolbar does not show, until the toolbar hides.
            # The default bar is 726 px wide: Open | First, Previous, Slideshow, Next, Last | the
            # rotations, flips and color picker | checkerboard, gallery, full screen | Settings, Quit,
            # buttons of 40 px 2 apart and separators of 13 px, 6 px inside the panel.
            edit_settings(("gallery", "show", "false"))
            app = launch([os.path.join(gallery_dir, "g01.png")], log)
            wait_until("g01.png shown without the gallery", shown("g01.png"))
            focus_viewer()
            xdotool("key", "t")
            wait_until("T shows the toolbar and saves it", lambda: stored(r"(?ms)^\[toolbar\]\n(?:[^\[]*\n)?show=true$"))
            x, y, w, h = window_geometry()
            left, middle = x + (w - 726) // 2, y + 16 + 26  # the bar's left edge, its buttons' middle row
            panel = 0x21 * (1 - round(70 * 2.55) / 255)  # black at 70 % over the background

            def bars_at_top():
                # The toolbar's panel at the top margin (in the first separator's slot, above its line),
                # the background in the 16 px below it, the top overlay's panel below that.
                shot = grab()
                if shot is None:
                    return False
                cx = x + w // 2
                return (abs(int(shot[y + 20, left + 52][0]) - panel) <= 3 and abs(int(shot[y + 16 + 52 + 8, cx][0]) - 0x21) <= 2
                        and abs(int(shot[y + 16 + 52 + 16 + 3, cx][0]) - panel) <= 3)
            wait_until("the toolbar at the top margin, the top overlay 16 px below it", bars_at_top)
            xdotool("mousemove", "--sync", str(left + 205), str(middle))  # Next

            def toolbar_tip():
                # Drawn by us (D-116): 8 px under the bar, below the button, on the hover's colour (the
                # highlight, ruby since section 1, over the panels' black), where the grey background
                # is without it.
                shot = grab()
                if shot is None:
                    return False
                tip = shot[y + 16 + 52 + 8 + 4, left + 205][:3].astype(int)
                return int(tip.max() - tip.min()) > 30
            wait_until("resting on a button shows its tooltip, 8 px under the bar, on the hover's colour", toolbar_tip)
            xdotool("click", "1")
            wait_until("a click on the toolbar's Next shows g02.png", shown("g02.png"))

            def slideshow_toolbar():
                """The slideshow's own toolbar below the top overlay: a column a little left of the
                middle, from below the toolbar's place down, crosses the overlay (30 rows) and, when
                it shows, the slideshow's toolbar (60 rows); None before a frame."""
                shot = grab()
                if shot is None:
                    return None
                column = shot[y + 60:y + 260, x + w // 2 - 30, 0].astype(int)
                return bool((np.abs(column - 0x21) > 6).sum() > 45)
            xdotool("mousemove", "--sync", str(x + w // 2), str(y + h // 2))
            xdotool("key", "s")
            time.sleep(1.5)  # frames to draw what would show
            if slideshow_toolbar():
                fail("the slideshow's own toolbar shows while the toolbar holds its buttons")
            print("ok   S: the slideshow's buttons on the toolbar, not its own toolbar as well")
            # Stop where start was, pause after it: the bar, one button wider, moved 21 px left.
            xdotool("mousemove", "--sync", str(left - 21 + 163), str(middle))  # stop
            xdotool("click", "1")
            xdotool("key", "t")
            wait_until("T hides the toolbar and saves it", lambda: stored(r"(?ms)^\[toolbar\]\n(?:[^\[]*\n)?show=false$"))
            xdotool("mousemove", "--sync", str(x + w // 2), str(y + h // 2))
            time.sleep(1.5)
            if slideshow_toolbar():
                fail("the slideshow still runs after the toolbar's stop")
            print("ok   the toolbar's stop ends the slideshow")
            xdotool("key", "s")
            wait_until("without the toolbar, the slideshow shows its own toolbar", lambda: slideshow_toolbar())
            xdotool("key", "s")
            # A window of 660 px holds the bar up to the flips with "…" after them, then Settings and
            # Quit, which stay on the bar after their separator (589 px of 628, D-115): the color
            # picker, the checkerboard, the gallery and full screen go under "…", whose panel lists
            # them below it (D-113); a click on a row runs its command.
            xdotool("key", "t")
            wait_until("T shows the toolbar again", lambda: stored(r"(?ms)^\[toolbar\]\n(?:[^\[]*\n)?show=true$"))
            xdotool("windowsize", "--sync", xdotool("search", "--onlyvisible", "--name", "ImageViewer$").stdout.split()[0],
                    "660", "700")
            wait_until("the window is 660 px wide", lambda: window_geometry()[2] == 660)
            x, y, w, h = window_geometry()
            bar_left = x + (w - 589) // 2
            xdotool("mousemove", "--sync", str(bar_left + 468), str(y + 16 + 26))  # "…"
            xdotool("click", "1")
            time.sleep(1)  # the panel drawn
            xdotool("mousemove", "--sync", str(bar_left + 468), str(y + 16 + 52 + 6 + 6 + 32 + 16))  # its second row
            xdotool("click", "1")
            wait_until("the \"…\" panel's second row, the checkerboard, runs its command",
                       lambda: stored(r"(?m)^checkerboard=true$"))
            xdotool("mousemove", "--sync", str(bar_left + 521), str(y + 16 + 26))  # Settings, still on the bar
            xdotool("click", "1")
            if not find_dialog("Settings"):
                fail("the Settings button beside \"…\" did not open Settings")
            print("ok   Settings and Quit stay on a narrow window's bar; Settings opens Settings")
            xdotool("key", "Escape")
            wait_until("Settings closes", lambda: not xdotool("search", "--onlyvisible", "--name", "^Settings$").stdout.split())
            # The size the next sections start from, which the session keeps (rememberGeometry).
            xdotool("windowsize", "--sync", xdotool("search", "--onlyvisible", "--name", "ImageViewer$").stdout.split()[0],
                    "1280", "800")
            wait_until("the window is 1280 px wide again", lambda: window_geometry()[2] == 1280)
            focus_viewer()
            xdotool("key", "q")
            app.wait(15)

            # 6. The color picker (D-115). An orange image and a 16-bit Display P3 one; the toolbar,
            # the checkerboard and the overlay off. K turns the picker on: its panel at the bottom
            # left, 190 px tall before a pick (the loupe, the swatches under it and the padding), the
            # swatch of the pixel under the pointer 10 px from its left, 130 from its top, 50 px a side
            # (D-116).
            picker_dir = os.path.join(work, "picker")
            os.makedirs(picker_dir)
            Image.new("RGB", (400, 300), (247, 145, 52)).save(os.path.join(picker_dir, "a-orange.png"))
            shutil.copy(os.path.join(os.path.dirname(os.path.abspath(__file__)), "data", "p3red16.png"),
                        os.path.join(picker_dir, "b-p3red16.png"))
            edit_settings(("toolbar", "show", "false"), ("window", "checkerboard", "false"),
                          ("window", "showOverlay", "false"))
            before = len(log_text())
            app = launch([os.path.join(picker_dir, "a-orange.png")], log)
            wait_until("a-orange.png shown", lambda: "decoded for display: a-orange.png" in log_text()[before:])
            focus_viewer()
            x, y, w, h = window_geometry()
            pointer_hidden = open_xfixes()
            pointer = pointer_hidden.shape
            # Beside the image and off the side zones (100 px, the folder holds two images), moved
            # twice: a still pointer hides after 2 s (D-113), its image blank.
            xdotool("mousemove", "--sync", str(x + 201), str(y + h // 2))
            xdotool("mousemove", "--sync", str(x + 200), str(y + h // 2))
            time.sleep(0.3)
            arrow = pointer()
            xdotool("key", "k")
            xdotool("mousemove", "--sync", str(x + w // 2), str(y + h // 2))
            wait_until("while picking, the pointer is a cross over the image (D-116)", lambda: pointer() != arrow)
            time.sleep(0.3)
            cross = pointer()
            if cross == arrow or pointer_hidden():
                fail("while picking, the pointer over the image is not a cross")
            swatch = (x + 16 + 35, y + h - 16 - 190 + 155)  # the middle of the swatch under the pointer

            def swatch_is(colour):
                shot = grab()
                return shot is not None and tuple(int(v) for v in shot[swatch[1], swatch[0]][:3]) == colour
            wait_until("K: the swatch shows the pixel under the pointer as the image does",
                       lambda: swatch_is(tuple(int(v) for v in grab()[y + h // 2, x + w // 2][:3]))
                       and swatch_is((247, 145, 52)))
            xdotool("mousemove", "--sync", str(x + 40), str(y + 40))  # beside the image

            def checks():
                shot = grab()
                if shot is None:
                    return False
                a, b = shot[swatch[1], swatch[0]][:3].astype(int), shot[swatch[1], swatch[0] + 5][:3].astype(int)
                return abs(a - b).max() > 10 and abs(a - (247, 145, 52)).max() > 30
            wait_until("beside the image, the swatch shows the checkerboard", checks)
            xdotool("mousemove", "--sync", str(x + w // 2 + 30), str(y + h // 2 + 20))
            xdotool("click", "1")
            wait_until("a click picks the pixel: its values in sRGB and Lab, no file's value for 8-bit sRGB",
                       lambda: re.search(r"picked a-orange\.png HEX=#F79134; RGB=rgb\(247, 145, 52\); HSL=hsl\(29, 92%, 59%\); "
                                         r"HSV=hsv\(29, 79%, 97%\); CMYK=cmyk\(0%, 41%, 79%, 3%\); "
                                         r"Lab=lab\(70\.3% 34\.8 63\.1\); Pixel=\d+, \d+$",
                                         log_text()[before:], re.MULTILINE) is not None)
            # The picked panel: its values on the right; the first, HEX, copied by a click on it.
            time.sleep(1)
            panel_top = y + h - 16 - int(re.findall(r"picker panel (\d+) px", log_text())[-1]) \
                if re.search(r"picker panel \d+ px", log_text()) else None
            if panel_top is None:
                fail("the picker's panel did not report its height")
            xdotool("mousemove", "--sync", str(x + 16 + 260), str(panel_top + 10 + 8))
            xdotool("click", "1")
            wait_until("a click on HEX copies it", lambda: "copied: #F79134" in log_text()[before:])
            # The P3 image: its file's own 16-bit codes, read again.
            xdotool("mousemove", "--sync", str(x + w // 2), str(y + h // 2))
            xdotool("key", "Right")
            wait_until("b-p3red16.png shown", lambda: re.search(r"(decoded for display|shown from the cache): b-p3red16\.png",
                                                                 log_text()[before:]) is not None)
            xdotool("mousemove", "--sync", str(x + w // 2 + 1), str(y + h // 2 + 1))
            xdotool("click", "1")
            wait_until("a pixel of the P3 image: the file's value read again, 16-bit Display P3 red",
                       lambda: "file value: 65535, 0, 0 (16-bit)" in log_text()[before:])
            xdotool("key", "Escape")

            def no_panel():  # the window's background where the panel's bottom left corner was
                shot = grab()
                return shot is not None and abs(int(shot[y + h - 16 - 6, x + 16 + 6][0]) - 0x21) <= 2
            wait_until("Esc leaves the picker: its panel goes", no_panel)
            # A drag holds the image in a closed hand, whatever the mode (the owner, D-116): the image
            # enlarged past the window, a press and a move; released, the arrow again.
            xdotool("key", "Left")
            xdotool("key", "plus", "plus", "plus", "plus", "plus")
            time.sleep(1)
            xdotool("mousemove", "--sync", str(x + w // 2), str(y + h // 2))
            xdotool("mousedown", "1")
            xdotool("mousemove", "--sync", str(x + w // 2 + 40), str(y + h // 2 + 40))
            wait_until("dragging the image, the pointer is a closed hand", lambda: pointer() not in (arrow, cross))
            hand = pointer()
            xdotool("mouseup", "1")
            # A move shows the pointer, which hides when still (D-113's delay, 2 s).
            xdotool("mousemove", "--sync", str(x + w // 2 + 41), str(y + h // 2 + 41))
            wait_until("released, the hand goes: the arrow again",
                       lambda: not pointer_hidden() and pointer() not in (hand, cross))
            xdotool("key", "0")  # fitted again
            # The information panel's histogram (P): the orange's three channels, each a coloured line.
            wait_until("a-orange.png again", lambda: re.search(r"(decoded for display|shown from the cache): a-orange\.png",
                                                               log_text()[before:].split("file value:")[-1]) is not None)
            xdotool("mousemove", "--sync", str(x + w // 2), str(y + h // 2))
            xdotool("key", "p")

            def histogram_lines():
                """The orange's three spikes on the panel's plot, wherever the panel's width puts it (a
                long folder path widens it): blue at 52, green at 145, red at 247 of 255, so green lies
                93/195 of the way from blue to red. Red is told from the orange image (R − G = 102)."""
                shot = grab()
                if shot is None:
                    return False
                band = shot[y + h // 2:y + h - 16, x:x + w, :3].astype(int)
                r, g, b = band[..., 0], band[..., 1], band[..., 2]

                def column(mask):
                    columns = np.nonzero(mask.sum(axis=0) >= 10)[0]
                    return float(columns.mean()) if len(columns) else None
                red = column((r > g + 120) & (r > b + 120))
                green = column((g > r + 60) & (g > b + 60))
                blue = column((b > r + 60) & (b > g + 40))
                return (None not in (red, green, blue) and blue < green < red
                        and abs((green - blue) / (red - blue) - 93 / 195) < 0.05)
            wait_until("P: the panel's histogram shows the orange's blue, green and red where their codes are",
                       histogram_lines)
            # Its row above the plot (D-116): "Histogram" and the choice, which opens a menu as a combo
            # box, drawn by us (D-117): its rows 30 px below the choice, the hovered one on the
            # interface's highlight (ruby since section 1), never the system's; None hides the plot, the
            # row stays. The choice is the brightest text of the row.
            shot = grab()
            band = shot[y + h // 2:y + h - 16, x:x + w, :3].astype(int)
            spikes = np.nonzero(((band[..., 1] > band[..., 0] + 60) & (band[..., 1] > band[..., 2] + 60)).any(axis=1))[0]
            plot_top = y + h // 2 + int(spikes.min()) - 4
            row = shot[plot_top - 26:plot_top - 4, x + 16:x + 16 + 400, :3].astype(int).min(axis=2)
            columns = np.nonzero((row > 200).any(axis=0))[0]
            if not len(columns):
                fail("the panel's histogram row: no text above the plot")
            words = np.split(columns, np.nonzero(np.diff(columns) > 12)[0] + 1)  # "Histogram", then the choice
            choice_x = x + 16 + int(words[-1].mean())
            xdotool("mousemove", "--sync", str(choice_x), str(plot_top - 15))
            xdotool("click", "1")
            none_y = plot_top + 79  # the third row's middle: the menu 4 px below the row, 6 inside, 30 a row
            xdotool("mousemove", "--sync", str(choice_x), str(none_y))

            def hovered_row():
                shot = grab()
                if shot is None:
                    return False
                tile = shot[none_y, choice_x - 30:choice_x + 30, :3].astype(int)
                return int((tile.max(axis=1) - tile.min(axis=1) > 40).sum()) > 20
            wait_until("the histogram's menu opens below its choice, the row under the pointer on the highlight",
                       hovered_row)
            xdotool("click", "1")  # None
            wait_until("the panel's choice, None, hides the plot and is saved",
                       lambda: stored(r"(?m)^histogram=off$") and not histogram_lines())
            xdotool("key", "p")
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
                # An SVG drawn for a smaller window (D-106): opened in the window, which then grows, it is
                # drawn again for the window once the window stays, in place of a detail of the whole
                # view; back at Fit after a zoom, its own texture serves, and no detail is drawn again.
                # Before, the fit needed a detail of the whole view, drawn again after every zoom.
                before = len(log_text())
                app = launch([large], log)
                wait_until("the large SVG shown", lambda: "decoded for display: large.svg" in log_text()[before:])
                focus_viewer()
                xdotool("key", "0")  # Fit to Window (100 % above is the zoom mode kept, D-65)
                time.sleep(1)
                if window_geometry()[2] >= 1600:
                    fail("the window did not open narrower than the screen")
                resized = len(log_text())
                wid = xdotool("search", "--onlyvisible", "--name", "ImageViewer$").stdout.split()[0]
                xdotool("windowsize", "--sync", wid, "1600", "1000")
                wait_until("the large SVG drawn again for the larger window (4001 x 3001 within 1600 x 1000)",
                           lambda: "drawn again for the window: large.svg 1334x1000" in log_text()[resized:])
                time.sleep(1)
                grown = log_text()[resized:]
                if re.search(r"detail drawn: large\.svg", grown):
                    fail("a detail of the whole view was drawn as well as the image for the window")
                for _ in range(2):
                    xdotool("key", "plus")
                wait_until("zoomed in past the window's size, the detail drawn",
                           lambda: re.search(r"detail drawn: large\.svg", log_text()[resized:]) is not None)
                zoomed = len(log_text())
                xdotool("key", "0")
                time.sleep(2)
                back = log_text()[zoomed:]
                if re.search(r"detail drawn: large\.svg|drawn again for the window", back):
                    fail("back at Fit, the SVG was drawn again (a detail or the image) instead of its own texture")
                xdotool("windowsize", "--sync", wid, "1280", "800")
                time.sleep(1)
                if "drawn again for the window" in log_text()[zoomed:]:
                    fail("a smaller window drew the SVG again")
                print("ok   an SVG drawn for a smaller window is drawn again once the window grows; Fit after a zoom needs no detail")
                xdotool("key", "q")
                app.wait(15)

                # An SVG turned a quarter (D-111) is drawn for the window as the turned view fits it: a
                # document of 1000 x 4000 in a window of 1280 x 800, 200 x 800 upright, is drawn again
                # 320 x 1280 once turned, and Fit after a zoom needs no detail. Before, it was drawn for
                # the window's other side, and every fit of the turned view needed a detail of all of it.
                tall_dir = os.path.join(work, "tall")
                os.makedirs(tall_dir)
                tall = os.path.join(tall_dir, "tall.svg")
                with open(tall, "w") as f:
                    f.write('<svg xmlns="http://www.w3.org/2000/svg" width="1000" height="4000">'
                            '<rect width="1000" height="4000" fill="#fff"/>'
                            '<path d="M100 100H900V3900H100Z" fill="none" stroke="#000" stroke-width="20"/></svg>')
                before = len(log_text())
                app = launch([tall], log)
                wait_until("the tall SVG shown", lambda: "decoded for display: tall.svg" in log_text()[before:])
                focus_viewer()
                xdotool("key", "0")
                wid = xdotool("search", "--onlyvisible", "--name", "ImageViewer$").stdout.split()[0]
                xdotool("windowsize", "--sync", wid, "1280", "800")
                wait_until("the window is 1280 x 800", lambda: window_geometry()[2:] == (1280, 800))
                time.sleep(2)  # drawn for this window, if it opened smaller (D-106)
                turned = len(log_text())
                xdotool("key", "r")
                wait_until("turned a quarter, the tall SVG is drawn again for the window (320 x 1280)",
                           lambda: "drawn again for the window: tall.svg 320x1280" in log_text()[turned:])
                time.sleep(1)
                if re.search(r"detail drawn: tall\.svg", log_text()[turned:]):
                    fail("a detail of the whole turned view was drawn as well as the image for the window")
                xdotool("key", "plus")
                wait_until("the turned SVG zoomed in, the detail drawn",
                           lambda: re.search(r"detail drawn: tall\.svg", log_text()[turned:]) is not None)
                zoomed = len(log_text())
                xdotool("key", "0")
                time.sleep(2)
                if re.search(r"detail drawn: tall\.svg|drawn again for the window", log_text()[zoomed:]):
                    fail("back at Fit, the turned SVG was drawn again (a detail or the image) instead of its own texture")
                print("ok   an SVG turned a quarter is drawn again for the window as it fits; Fit after a zoom needs no detail")
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

