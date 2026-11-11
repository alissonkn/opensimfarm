#!/usr/bin/env python3
"""Headless touch test of the browser version (site/play/) on Linux, in Firefox.
    engine/tools/build_web.sh                         # build site/play/opensimfarm.js + .wasm first
    python3 engine/tools/webtouchtest.py [--game DIR] [--out DIR] [--size WxH]
Like webtest.py (whose test server and Marionette client it uses), with touch events turned on in the throw-away
Firefox profile. The SimFarm folder is dropped onto the page, then everything is done with fingers: TouchEvents sent
to the game's canvas, as a phone's browser sends them. It taps past the intro pictures and starts the first prebuilt
farm (taps outside the farm map are mouse clicks), then on the farm map: a one-finger drag pans, a pinch zooms in
and out, a tap on a toolbar button opens its window, a tap on the map uses the chosen tool. Screenshots go to --out.
Exit status 0 when every check passes.
"""
import argparse, functools, http.server, os, shutil, subprocess, sys, tempfile, threading, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import warnings  # noqa: E402
warnings.filterwarnings("ignore", category=DeprecationWarning)  # Pillow's getdata
import webtest  # noqa: E402  (the test server, the folder drop and the Marionette client)

try:
    from PIL import Image, ImageChops
except ImportError:
    sys.exit("Pillow is needed (python3 -m pip install --user pillow).")

# runs in the page: send touch events to the canvas. arguments: type, the fingers down after the event as
# [[id, x, y], ...] in canvas CSS pixels, and the ids that changed
TOUCH_JS = r"""
var type = arguments[0], down = arguments[1], changed = arguments[2];
var c = document.getElementById("canvas"), r = c.getBoundingClientRect();
window.__fingers = window.__fingers || {};
function touch(id, x, y) {
  return new Touch({ identifier: id, target: c, clientX: r.left + x, clientY: r.top + y, pageX: r.left + x,
                     pageY: r.top + y, screenX: r.left + x, screenY: r.top + y, radiusX: 4, radiusY: 4, force: 1 });
}
down.forEach(function (f) { window.__fingers[f[0]] = f; });
var ch = changed.map(function (id) { var f = window.__fingers[id]; return touch(id, f[1], f[2]); });
var all = down.map(function (f) { return touch(f[0], f[1], f[2]); });
if (type === "touchend") changed.forEach(function (id) { delete window.__fingers[id]; });
c.dispatchEvent(new TouchEvent(type, { touches: all, targetTouches: all, changedTouches: ch, bubbles: true,
                                       cancelable: true, composed: true }));
return [r.width, r.height];
"""


def main():
    ap = argparse.ArgumentParser(description="Headless touch test of the browser version in Firefox.")
    ap.add_argument("--game", help="your SimFarm folder (default: the one in ~/.config/opensimfarm/gamepath.txt)")
    ap.add_argument("--out", help="folder for the screenshots (default: a new folder under /tmp)")
    ap.add_argument("--firefox", default=shutil.which("firefox") or "firefox", help="the Firefox program")
    ap.add_argument("--size", default="1000x700", help="window size, e.g. 1000x700")
    a = ap.parse_args()
    game = a.game or open(os.path.expanduser("~/.config/opensimfarm/gamepath.txt")).read().strip()
    for f in ("opensimfarm.js", "opensimfarm.wasm"):
        if not os.path.isfile(os.path.join(webtest.SITE, "play", f)):
            sys.exit(f"site/play/{f} is missing: run engine/tools/build_web.sh first.")
    out = a.out or tempfile.mkdtemp(prefix="opensimfarm-touchtest-")
    os.makedirs(out, exist_ok=True)
    w, h = (int(v) for v in a.size.lower().split("x"))

    webtest.Handler.game = game
    http_port, mn_port = webtest.free_port(), webtest.free_port()
    server = http.server.ThreadingHTTPServer(("127.0.0.1", http_port),
                                             functools.partial(webtest.Handler, directory=webtest.SITE))
    threading.Thread(target=server.serve_forever, daemon=True).start()
    base = f"http://127.0.0.1:{http_port}/play/"
    profile = tempfile.mkdtemp(prefix="opensimfarm-touchtest-profile-")
    with open(os.path.join(profile, "user.js"), "w") as f:
        f.write(f'user_pref("marionette.port", {mn_port});\n'
                'user_pref("browser.shell.checkDefaultBrowser", false);\n'
                'user_pref("datareporting.policy.dataSubmissionEnabled", false);\n'
                'user_pref("media.autoplay.default", 0);\n'
                'user_pref("dom.w3c_touch_events.enabled", 1);\n')     # touch events, as on a touch screen
    ff = subprocess.Popen([a.firefox, "--headless", "--marionette", "--no-remote", "--profile", profile,
                           "--window-size", f"{w},{h}"], env=dict(os.environ, MOZ_HEADLESS="1"),
                          stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    failures = []

    def check(ok, what, detail=""):
        print(("PASS  " if ok else "FAIL  ") + what + (f"  ({detail})" if detail else ""), flush=True)
        if not ok:
            failures.append(what)
        return ok

    def wait(m, cond_js, seconds):
        end = time.time() + seconds
        while time.time() < end:
            try:
                if m.js(cond_js):
                    return True
            except RuntimeError:
                pass
            time.sleep(0.25)
        return False

    def shot(m, name):
        p = os.path.join(out, name + ".png")
        m.shot(p)
        return Image.open(p).convert("RGB")

    def canvas_box(m):
        """the canvas on the screenshot: left, top, width, height"""
        return [round(v) for v in m.js("var b = document.getElementById('canvas').getBoundingClientRect();"
                                       "return [b.left, b.top, b.width, b.height]")]

    def touch(m, type_, down, changed):
        m.js(TOUCH_JS, type_, down, changed)

    def tap(m, x, y, hold=0.08):
        touch(m, "touchstart", [[1, x, y]], [1])
        time.sleep(hold)
        touch(m, "touchend", [], [1])
        time.sleep(0.6)

    def drag(m, x0, y0, x1, y1, steps=10):
        touch(m, "touchstart", [[1, x0, y0]], [1])
        for i in range(1, steps + 1):
            time.sleep(0.03)
            touch(m, "touchmove", [[1, x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * i / steps]], [1])
        touch(m, "touchend", [], [1])
        time.sleep(0.6)

    def pinch(m, cx, cy, d0, d1, steps=10):
        touch(m, "touchstart", [[1, cx - d0 / 2, cy]], [1])
        touch(m, "touchstart", [[1, cx - d0 / 2, cy], [2, cx + d0 / 2, cy]], [2])
        for i in range(1, steps + 1):
            time.sleep(0.03)
            d = d0 + (d1 - d0) * i / steps
            touch(m, "touchmove", [[1, cx - d / 2, cy], [2, cx + d / 2, cy]], [1, 2])
        touch(m, "touchend", [[2, cx + d1 / 2, cy]], [1])
        touch(m, "touchend", [], [2])
        time.sleep(0.8)

    def diff(a_, b_, box):
        d = ImageChops.difference(a_.crop(box), b_.crop(box)).convert("L")
        return sum(1 for p in d.getdata() if p > 40) / max(1, (box[2] - box[0]) * (box[3] - box[1]))

    def shift(a_, b_, box, dx, dy, tol=4):
        """how well b_ is a_ moved by (dx, dy) inside box (share of pixels that match), best within tol pixels"""
        best = (0.0, 0, 0)
        for ex in range(-tol, tol + 1, 2):
            for ey in range(-tol, tol + 1, 2):
                x0, y0, x1, y1 = box
                pa = a_.crop((x0, y0, x1, y1))
                pb = b_.crop((x0 + dx + ex, y0 + dy + ey, x1 + dx + ex, y1 + dy + ey))
                d = ImageChops.difference(pa, pb).convert("L")
                share = sum(1 for p in d.getdata() if p < 24) / ((x1 - x0) * (y1 - y0))
                best = max(best, (share, ex, ey))
        return best

    def scaled(a_, b_, ax, ay, r, half=120, tol=3):
        """how well b_ is a_ zoomed r times around (ax, ay) (share of pixels that match near the anchor)"""
        best = 0.0
        hb = half
        ha = half / r
        pb = b_.crop((ax - hb, ay - hb, ax + hb, ay + hb)).convert("RGB")
        for ex in range(-tol, tol + 1):
            for ey in range(-tol, tol + 1):
                pa = a_.crop((ax + ex - ha, ay + ey - ha, ax + ex + ha, ay + ey + ha)).convert("RGB")
                pa = pa.resize(pb.size, Image.NEAREST)
                d = ImageChops.difference(pa, pb).convert("L")
                best = max(best, sum(1 for p in d.getdata() if p < 24) / (pb.size[0] * pb.size[1]))
        return best

    def fence_x(g, y0, y1):
        """the x of vertical dark lines (fences) on the map: columns that are dark over most of the height"""
        xs = []
        for x in range(0, g.size[0], 2):
            if sum(1 for y in range(y0, y1, 8) if g.getpixel((x, y)) < 45) > (y1 - y0) / 8 * 0.4:
                xs.append(x)
        return xs

    try:
        m = webtest.Marionette(mn_port)
        m.go(base)
        check(wait(m, "return !!(window.Module && Module.FS)", 30), "the engine loads")
        check(m.js("return typeof TouchEvent === 'function' && typeof Touch === 'function'"), "touch events are on")
        n = m.ajs(webtest.DROP_JS, "game")
        ok = wait(m, "return !document.getElementById('game').hidden && document.getElementById('loading').hidden"
                     " && (window.__frames || 0) > 30", 60)
        check(ok, f"the game starts from the dropped folder ({n} files)")
        time.sleep(1.5)
        cl, ct, cw, ch = canvas_box(m)
        # the intro pictures: taps move on, as clicks do
        for k in range(2):
            tap(m, cw / 2, ch / 2)
            time.sleep(1.2)
        s0 = shot(m, "t01-region")
        # the region screen is the 640 x 480 picture, as large as the canvas allows, in its middle
        sc = min(cw / 640, ch / 480)
        ox, oy = (cw - 640 * sc) / 2, (ch - 480 * sc) / 2
        tap(m, ox + 167 * sc, oy + 244 * sc)       # USE THIS REGION: the first prebuilt farm
        time.sleep(4)
        s1 = shot(m, "t02-farm")
        check(diff(s0, s1, (cl, ct, cl + cw, ct + ch)) > 0.3, "a tap on USE THIS REGION starts the farm")
        # the interface scale: the farm screen's interface is 640 x 480 drawn k times (k from the canvas height)
        k = m.js("return 1") and max(1, min(cw // 640, ch // 480)) if min(cw / 640, ch / 480) >= 2 else 1
        tap(m, 330 * k, 294 * k)                   # the MAP window's CLOSE (the new game opens it)
        s2 = shot(m, "t03-map-closed")
        mapbox = (cl + cw // 3, ct + 120 * k, cl + cw - 60, ct + ch - 60)
        check(diff(s1, s2, (cl + 90 * k, ct + 70 * k, cl + 370 * k, ct + 330 * k)) > 0.2, "a tap on CLOSE closes the MAP window")

        # one finger on the map pans it: the picture follows the finger
        x0, y0 = cl and 0 or 0, 0
        drag(m, cw * 0.7, ch * 0.6, cw * 0.7 - 160, ch * 0.6 - 96)
        s3 = shot(m, "t04-panned")
        inner = (mapbox[0] + 170, mapbox[1] + 100, mapbox[2] - 20, mapbox[3] - 20)
        share, ex, ey = shift(s2, s3, inner, -160, -96, tol=6)
        # (the weather can change the soil's colours meanwhile, and the soil repeats every tile, so the match is
        # compared with the picture moved half a tile less)
        half, _, _ = shift(s2, s3, inner, -152, -88, tol=0)
        check(share > 0.7 and share > half + 0.2, "a one-finger drag pans the map with the finger",
              f"{share:.2f} of the picture moved by (-160, -96), off by ({ex}, {ey}); {half:.2f} half a tile off")
        check(diff(s2, s3, (cl + 400 * k, ct, cl + 1100 * k, ct + 60 * k)) < 0.01, "the toolbar stays as it was (no click, no tool)")

        # two fingers pinch: the picture grows with the finger distance (x 170/120, the smooth pinch), then shrinks back to
        # the same picture
        ax, ay = cw * 0.7, ch * 0.55
        pinch(m, ax, ay, 120, 170)
        s4 = shot(m, "t05-zoomed-in")
        same, _, _ = shift(s3, s4, inner, 0, 0, tol=8)
        sc = max((scaled(s3, s4, cl + ax, ct + ay, r), r) for r in (170 / 120, 4 / 3, 1.5))
        check(sc[0] > 0.8 and sc[0] > same + 0.2, "pinching out zooms in around the fingers",
              f"{same:.2f} still as it was, {sc[0]:.2f} matches the picture {sc[1]:.2f} times larger")
        pinch(m, ax, ay, 170, 120)
        s5 = shot(m, "t06-zoomed-out")
        back, ex, ey = shift(s3, s5, inner, 0, 0, tol=4)
        check(back > 0.75 and back > sc[0] - 0.1 or back > 0.9, "pinching in zooms back out to the same view",
              f"{back:.2f} as before, off by ({ex}, {ey})")

        # a finger held still on the map is a right click: with a tool chosen it lets go of the tool, and the status
        # row's tool help goes away. Examine is the starting tool; a tap on Move (the tool column's first button)
        # changes the help first.
        status = (cl + 40 * k, ct + 50 * k, cl + 640 * k, ct + 60 * k)

        def ink(img):
            return sum(1 for p in img.crop(status).convert("L").getdata() if p < 60)
        tap(m, 12 * k, 60 * k)
        s5a = shot(m, "t07-move")
        check(diff(s5, s5a, status) > 0.02 and ink(s5a) > 200, "a tap on the tool column chooses Move",
              f"{diff(s5, s5a, status):.3f} of the status row changed")
        tap(m, ax, ay, hold=0.9)
        s5b = shot(m, "t08-longpress")
        check(ink(s5b) < 50, "a long press on the map is a right click (lets go of the tool)", f"{ink(s5b)} dark pixels left in the status row")

        # a short tap on the map is a left click: with Examine chosen again, the plot's Examine card opens
        tap(m, 12 * k, 84 * k)
        s5c = shot(m, "t09-examine")
        # a rock: a cluster of dark pixels on the map, away from the fence line and the window's edges
        g = s5c.convert("L")
        rx = ry = None
        fences = fence_x(g, ct + 120 * k, ct + ch - 80)
        for yy in range(ct + 120 * k, ct + ch - 80, 4):
            for xx in range(cl + 80 * k, cl + cw - 80, 4):
                if any(abs(xx - fx) < 30 for fx in fences):
                    continue
                if sum(1 for dx in range(0, 12, 2) for dy in range(0, 12, 2) if g.getpixel((xx + dx, yy + dy)) < 45) >= 14:
                    rx, ry = xx + 6, yy + 6
                    break
            if rx:
                break
        check(rx is not None, "a rock to examine is on the map")
        tap(m, (rx or ax) - cl, (ry or ay) - ct)
        s5d = shot(m, "t10-card")
        check(diff(s5c, s5d, (cl, ct + 62 * k, cl + cw, ct + ch)) > 0.03, "a tap on the map is a click (Examine card)",
              f"{diff(s5c, s5d, (cl, ct + 62 * k, cl + cw, ct + ch)):.3f} of the map changed")

        # a tap on a toolbar button is its click: BUY opens its window
        tap(m, 16 * k, 30 * k)
        s6 = shot(m, "t11-buy")
        check(diff(s5, s6, (cl + 100 * k, ct + 60 * k, cl + 540 * k, ct + 420 * k)) > 0.3, "a tap on BUY opens the BUY window")
        print("canvas", cl, ct, cw, ch, "k", k)
    except Exception as e:  # noqa: BLE001
        check(False, "the test ran to the end", str(e)[:300])
    finally:
        try:
            ff.terminate()
            ff.wait(10)
        except Exception:  # noqa: BLE001
            ff.kill()
        server.shutdown()
        shutil.rmtree(profile, ignore_errors=True)
    print(f"\nScreenshots: {out}")
    print("All checks passed." if not failures else f"{len(failures)} check(s) failed: " + "; ".join(failures))
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
