#!/usr/bin/env python3
"""Headless test of the browser version (site/play/) on Linux, in Firefox.

    engine/tools/build_web.sh                         # build site/play/opensimfarm.js + .wasm first
    python3 engine/tools/webtest.py [--game DIR] [--out DIR] [--keep]

It serves site/ on 127.0.0.1, starts a headless Firefox with a fresh, throw-away profile, and drives the play page
through Marionette (Firefox's own remote protocol; nothing to install besides Firefox). Choosing a folder is a
system dialog a script cannot use, so the test drops the SimFarm folder onto the page the way a browser does
for a dragged folder, with the files served by the test server only for this test. Nothing is written to the
SimFarm folder or to your own Firefox profile.

DIR is your SimFarm folder (default: the one the desktop game remembers in ~/.config/opensimfarm/gamepath.txt).
Screenshots go to --out (default: a new folder under /tmp, printed at the end). --keep leaves Firefox and the server
running at the end, for more checks by hand, until Enter is pressed.
Exit status 0 when every check passes.
"""
import argparse, base64, functools, http.server, json, os, shutil, socket, subprocess, sys, tempfile, threading, time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SITE = os.path.join(ROOT, "site")


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


# ---- the test server: site/, plus the SimFarm folder (and a broken one) under /_test/ for the simulated drop
class Handler(http.server.SimpleHTTPRequestHandler):
    game = None
    bad = {"MEGADATA.IDX": os.urandom(5000), "MEGADATA.EEA": os.urandom(90000)}   # right names, unusable contents

    def log_message(self, *a):
        pass

    def send_bytes(self, data, ctype="application/octet-stream"):
        self.send_response(200)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        p = self.path.split("?")[0]
        if p == "/_test/game.json":
            names = []
            for root, dirs, files in os.walk(self.game):
                rel = os.path.relpath(root, self.game)
                if rel.count(os.sep) >= 2:
                    dirs[:] = []
                    continue
                names += [os.path.normpath(os.path.join(rel, f)) for f in files]
            return self.send_bytes(json.dumps(sorted(names)).encode(), "application/json")
        if p.startswith("/_test/game/"):
            full = os.path.realpath(os.path.join(self.game, p[len("/_test/game/"):]))
            if not full.startswith(os.path.realpath(self.game) + os.sep) or not os.path.isfile(full):
                return self.send_error(404)
            return self.send_bytes(open(full, "rb").read())
        if p == "/_test/bad.json":
            return self.send_bytes(json.dumps(sorted(self.bad)).encode(), "application/json")
        if p.startswith("/_test/bad/") and p[len("/_test/bad/"):] in self.bad:
            return self.send_bytes(self.bad[p[len("/_test/bad/"):]])
        return super().do_GET()


# runs in the page: builds folder entries (as a browser gives for a dragged folder) from the served files and drops
# them onto the page's drop area
DROP_JS = r"""
var kind = arguments[0], done = arguments[arguments.length - 1];
fetch("/_test/" + kind + ".json").then(function (r) { return r.json(); }).then(function (paths) {
  function dirEntry(name, prefix) {
    var kids = {};
    paths.forEach(function (p) {
      if (prefix && p.indexOf(prefix + "/") !== 0) return;
      var rest = prefix ? p.slice(prefix.length + 1) : p;
      kids[rest.split("/")[0]] = rest.indexOf("/") >= 0 ? "dir" : "file";
    });
    return { isFile: false, isDirectory: true, name: name, createReader: function () {
      var given = false;
      return { readEntries: function (ok) {
        if (given) return ok([]);
        given = true;
        ok(Object.keys(kids).map(function (k) {
          var full = prefix ? prefix + "/" + k : k;
          return kids[k] === "dir" ? dirEntry(k, full) : fileEntry(k, full);
        }));
      } };
    } };
  }
  function fileEntry(name, full) {
    return { isFile: true, isDirectory: false, name: name, file: function (ok, bad) {
      fetch("/_test/" + kind + "/" + full).then(function (r) { return r.blob(); })
        .then(function (b) { ok(new File([b], name)); }, bad);
    } };
  }
  var root = dirEntry("SimFarm", "");
  var ev = new Event("drop", { bubbles: true, cancelable: true });
  Object.defineProperty(ev, "dataTransfer", { value: { items: [{ webkitGetAsEntry: function () { return root; } }] } });
  document.getElementById("drop").dispatchEvent(ev);
  done(paths.length);
});
"""

# runs in the page right after it loads: counts the animation frames the game asks for (it waits for one after every
# frame it draws), so the test can tell that the game's main loop is running
COUNT_FRAMES_JS = """
if (!window.__frames) {
  window.__frames = 0;
  var raf = window.requestAnimationFrame.bind(window);
  window.requestAnimationFrame = function (f) { window.__frames++; return raf(f); };
}
return true;
"""


# ---- a minimal Marionette client
class Marionette:
    def __init__(self, port, timeout=30):
        end = time.time() + timeout
        while True:
            try:
                self.sock = socket.create_connection(("127.0.0.1", port))
                break
            except OSError:
                if time.time() > end:
                    raise RuntimeError("Firefox did not open its Marionette port")
                time.sleep(0.3)
        self.buf, self.id = b"", 0
        self._read()
        self.cmd("WebDriver:NewSession", {"capabilities": {"alwaysMatch": {"acceptInsecureCerts": True}}})
        self.cmd("WebDriver:SetTimeouts", {"script": 60000})

    def _read(self):
        while b":" not in self.buf:
            self.buf += self.sock.recv(65536)
        n, rest = self.buf.split(b":", 1)
        n = int(n)
        while len(rest) < n:
            rest += self.sock.recv(1 << 20)
        self.buf = rest[n:]
        return json.loads(rest[:n])

    def cmd(self, name, params=None):
        self.id += 1
        msg = json.dumps([0, self.id, name, params or {}]).encode()
        self.sock.sendall(str(len(msg)).encode() + b":" + msg)
        while True:
            r = self._read()
            if r[0] == 1 and r[1] == self.id:
                if r[2]:
                    raise RuntimeError(r[2])
                return r[3]

    def js(self, script, *args):
        return self.cmd("WebDriver:ExecuteScript", {"script": script, "args": list(args)}).get("value")

    def ajs(self, script, *args):
        return self.cmd("WebDriver:ExecuteAsyncScript", {"script": script, "args": list(args)}).get("value")

    def go(self, url):
        self.cmd("WebDriver:Navigate", {"url": url})
        self.js(COUNT_FRAMES_JS)

    def shot(self, path):
        open(path, "wb").write(base64.b64decode(self.cmd("WebDriver:TakeScreenshot", {"full": False})["value"]))

    def keys(self, *ks):
        acts = []
        for k in ks:
            acts += [{"type": "keyDown", "value": k}, {"type": "pause", "duration": 80}, {"type": "keyUp", "value": k},
                     {"type": "pause", "duration": 400}]
        self.cmd("WebDriver:PerformActions", {"actions": [{"type": "key", "id": "kb", "actions": acts}]})
        self.cmd("WebDriver:ReleaseActions")

    def click(self, x, y):
        self.cmd("WebDriver:PerformActions", {"actions": [{"type": "pointer", "id": "mouse",
            "parameters": {"pointerType": "mouse"}, "actions": [
                {"type": "pointerMove", "x": int(x), "y": int(y), "duration": 0}, {"type": "pointerDown", "button": 0},
                {"type": "pause", "duration": 80}, {"type": "pointerUp", "button": 0}]}]})
        self.cmd("WebDriver:ReleaseActions")


def main():
    ap = argparse.ArgumentParser(description="Headless test of the browser version (site/play/) in Firefox.")
    ap.add_argument("--game", help="your SimFarm folder (default: the one in ~/.config/opensimfarm/gamepath.txt)")
    ap.add_argument("--out", help="folder for the screenshots (default: a new folder under /tmp)")
    ap.add_argument("--firefox", default=shutil.which("firefox") or "firefox", help="the Firefox program")
    ap.add_argument("--size", default="1400x1000", help="window size, e.g. 1400x1000")
    ap.add_argument("--keep", action="store_true", help="leave Firefox and the server running until Enter is pressed")
    a = ap.parse_args()

    game = a.game
    if not game:
        try:
            game = open(os.path.expanduser("~/.config/opensimfarm/gamepath.txt")).read().strip()
        except OSError:
            sys.exit("Give your SimFarm folder with --game DIR.")
    if not os.path.isfile(os.path.join(game, "MEGADATA.IDX")) and not os.path.isfile(os.path.join(game, "megadata.idx")):
        sys.exit(f"{game} is not a SimFarm folder (no MEGADATA.IDX).")
    for f in ("opensimfarm.js", "opensimfarm.wasm"):
        if not os.path.isfile(os.path.join(SITE, "play", f)):
            sys.exit(f"site/play/{f} is missing: run engine/tools/build_web.sh first.")
    if not shutil.which(a.firefox) and not os.path.isfile(a.firefox):
        sys.exit("Firefox was not found (install it, or give its path with --firefox).")
    out = a.out or tempfile.mkdtemp(prefix="opensimfarm-webtest-")
    os.makedirs(out, exist_ok=True)
    w, h = (int(v) for v in a.size.lower().split("x"))

    Handler.game = game
    http_port, mn_port = free_port(), free_port()
    server = http.server.ThreadingHTTPServer(("127.0.0.1", http_port), functools.partial(Handler, directory=SITE))
    threading.Thread(target=server.serve_forever, daemon=True).start()
    base = f"http://127.0.0.1:{http_port}/play/"

    profile = tempfile.mkdtemp(prefix="opensimfarm-webtest-profile-")
    with open(os.path.join(profile, "user.js"), "w") as f:
        f.write(f'user_pref("marionette.port", {mn_port});\n'
                'user_pref("browser.shell.checkDefaultBrowser", false);\n'
                'user_pref("datareporting.policy.dataSubmissionEnabled", false);\n'
                'user_pref("media.autoplay.default", 0);\n')            # let the game's sound start without a click
    env = dict(os.environ, MOZ_HEADLESS="1")
    ff = subprocess.Popen([a.firefox, "--headless", "--marionette", "--no-remote", "--profile", profile,
                           "--window-size", f"{w},{h}"], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

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
                pass                                  # the page is reloading
            time.sleep(0.25)
        return False

    def frames(m):
        return m.js("return window.__frames || 0")

    def running(m, label):
        """the game is on screen and its main loop goes on"""
        ok = wait(m, "return !document.getElementById('game').hidden && document.getElementById('loading').hidden"
                     " && (window.__frames || 0) > 30", 60)
        f1 = frames(m)
        time.sleep(1.5)
        f2 = frames(m)
        return check(ok and f2 > f1 + 20, label, f"{f2 - f1} frames in 1.5 s")

    def shot(m, name):
        m.shot(os.path.join(out, name + ".png"))

    try:
        m = Marionette(mn_port)

        # 1. the page itself
        m.go(base)
        check(m.js("return !document.getElementById('setup').hidden"), "the setup card is shown")
        check(m.js("return !!document.querySelector('.beta-warn') && /beta/i.test(document.title)"),
              "the beta warning and title are there")
        check(wait(m, "return !!(window.Module && Module.FS)", 30), "the engine loads (opensimfarm.js + .wasm)")
        shot(m, "01-setup")

        # 2. an unusable folder: the game ends, the page starts over and says why
        m.ajs(DROP_JS, "bad")
        ok = wait(m, "var s = document.getElementById('status'); return s && s.classList.contains('error')"
                     " && s.textContent.length > 10 && !document.getElementById('setup').hidden", 60)
        check(ok, "an unusable folder is refused with a reason",
              (m.js("return document.getElementById('status').textContent") or "")[:90])
        check(m.js("return document.getElementById('stored').hidden"), "the unusable files are not kept")
        shot(m, "02-unusable-folder")

        # 3. the real folder, remembered: the game starts and runs
        m.go(base)
        wait(m, "return !!(window.Module && Module.FS)", 30)
        n = m.ajs(DROP_JS, "game")
        if running(m, f"the game starts from the dropped folder ({n} files)"):
            time.sleep(1)
            shot(m, "03-intro")
        r = m.js("var c = document.getElementById('canvas'), b = c.getBoundingClientRect();"
                 "return [c.width, c.height, Math.round(b.width), Math.round(b.height)]")
        check(abs(r[0] - r[2]) <= 2 and abs(r[1] - r[3]) <= 2, "the canvas fills its place on the page", f"{r}")
        audio = m.js("try { return Module.SDL2.audioContext.state + ' ' + Module.SDL2.audioContext.sampleRate }"
                     " catch (e) { return 'none' }")
        check(str(audio).startswith("running"), "the sound output runs", audio)

        # 4. keys reach the game: they move on past the intro pictures
        m.js("document.getElementById('canvas').focus(); return 1")
        m.keys("", "", "")                  # Enter
        time.sleep(2)
        shot(m, "04-after-keys")
        running(m, "the game still runs after key presses")

        # 5. the top bar can be hidden and brought back; the canvas follows
        m.js("document.getElementById('btn-hidebar').click(); return 1")
        time.sleep(1)
        r = m.js("var b = document.getElementById('canvas').getBoundingClientRect();"
                 "return [Math.round(b.height), innerHeight, document.querySelector('.bar').offsetHeight,"
                 " getComputedStyle(document.getElementById('showbar')).display]")
        check(abs(r[0] - r[1]) <= 2 and r[2] == 0 and r[3] != "none", "Hide bar gives the game the whole page", f"{r}")
        shot(m, "05-bar-hidden")
        m.js("document.getElementById('showbar').click(); return 1")
        time.sleep(1)
        r = m.js("return [document.querySelector('.bar').offsetHeight, localStorage.getItem('opensimfarm.hideBar')]")
        check(r[0] > 0 and r[1] is None, "the BETA tab brings the bar back", f"{r}")

        # 6. window size: the canvas follows
        m.cmd("WebDriver:SetWindowRect", {"width": 1000, "height": 720})
        time.sleep(1.5)
        r = m.js("var c = document.getElementById('canvas'), b = c.getBoundingClientRect();"
                 "return [c.width, c.height, Math.round(b.width), Math.round(b.height)]")
        check(abs(r[0] - r[2]) <= 2 and abs(r[1] - r[3]) <= 2, "the canvas follows a window resize", f"{r}")
        m.cmd("WebDriver:SetWindowRect", {"width": w, "height": h})

        # 7. Saves: upload a save, it is listed and kept in the browser's storage
        m.js("document.getElementById('btn-saves').click();"
             "var dt = new DataTransfer(); dt.items.add(new File([new Uint8Array(64)], 'WEBTEST.SFM'));"
             "var i = document.getElementById('upload'); i.files = dt.files;"
             "i.dispatchEvent(new Event('change')); return 1")
        check(wait(m, "return /WEBTEST\\.SFM/.test(document.getElementById('save-list').textContent)", 10),
              "an uploaded save is listed")
        shot(m, "06-saves")
        m.js("document.getElementById('saves-close').click(); return 1")
        time.sleep(6)                                      # the page writes to the browser's storage every 5 s

        # 8. a new visit: the files and the save are remembered, Play starts straight away
        m.go(base)
        check(wait(m, "return !document.getElementById('stored').hidden", 30), "the game files are remembered")
        check(m.js("try { return Module.FS.readdir('/persist/opensimfarm/saves').indexOf('WEBTEST.SFM') >= 0 }"
                   " catch (e) { return false }"), "the save is kept after a reload")
        m.js("document.getElementById('btn-play-stored').click(); return 1")
        if running(m, "the game starts again from the remembered files"):
            shot(m, "07-remembered")

        # 9. Forget: the files are gone on the next visit, the saves stay
        m.go(base)
        wait(m, "return !document.getElementById('stored').hidden", 30)
        m.js("document.getElementById('btn-forget').click(); return 1")
        time.sleep(2)
        m.go(base)
        wait(m, "return !!(window.Module && Module.FS) && document.getElementById('btn-pick').textContent.length > 0", 30)
        time.sleep(1)
        check(m.js("return document.getElementById('stored').hidden"), "Forget removes the remembered files")
        check(m.js("try { return Module.FS.readdir('/persist/opensimfarm/saves').indexOf('WEBTEST.SFM') >= 0 }"
                   " catch (e) { return false }"), "Forget keeps the saves")

        if a.keep:
            print(f"\nFirefox (Marionette port {mn_port}) and the server ({base}) are still running. Enter ends them.")
            input()
    except Exception as e:
        check(False, "the test ran to the end", str(e)[:300])
    finally:
        ff.terminate()
        try:
            ff.wait(10)
        except subprocess.TimeoutExpired:
            ff.kill()
        server.shutdown()
        shutil.rmtree(profile, ignore_errors=True)

    print(f"\nScreenshots: {out}")
    print("All checks passed." if not failures else f"{len(failures)} check(s) failed: " + "; ".join(failures))
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
