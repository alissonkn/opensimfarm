// OpenSimFarm in the browser.
// The player chooses the folder of their own SimFarm; its files are read by the browser and put into the game's
// in-memory file system (nothing is sent anywhere). The game's settings and saves live in /persist/opensimfarm, a
// folder kept in the browser's IndexedDB; the game files themselves are kept there too (/persist/game) when the player
// ticks "Remember", so the next visit can start straight away.
(function () {
  "use strict";
  var PERSIST = "/persist";
  var CONFIG = PERSIST + "/opensimfarm";      // the engine's ConfigDir() with XDG_CONFIG_HOME=/persist
  var KEPT_GAME = PERSIST + "/game";          // remembered game files
  var TEMP_GAME = "/game";                    // game files for this visit only
  var MAX_FILE = 32 * 1024 * 1024;            // a SimFarm folder is about 3 MB; skip anything unusually large
  var MAX_TOTAL = 96 * 1024 * 1024;

  var $ = function (id) { return document.getElementById(id); };
  var statusEl = $("status");
  function status(text, isError) {
    statusEl.textContent = text || "";
    statusEl.classList.toggle("error", !!isError);
  }

  // ---- the engine (opensimfarm.js + .wasm), loaded at once so it is ready when the player has chosen the files
  var ready = false, started = false, syncing = false, FS = null, filesProblem = null;
  var Module = window.Module = {
    canvas: $("canvas"),
    noInitialRun: true,
    print: function (t) { console.log(t); },
    printErr: function (t) { console.warn(t); },
    preRun: [function () { Module.ENV.XDG_CONFIG_HOME = PERSIST; }],
    onRuntimeInitialized: function () {
      FS = Module.FS;
      FS.mkdir(PERSIST);
      FS.mount(Module.IDBFS, {}, PERSIST);
      FS.syncfs(true, function (err) {
        if (err) console.warn("browser storage could not be read", err);
        mkdirs(CONFIG + "/saves");
        ready = true;
        showStored();
        if (pendingFiles) { var p = pendingFiles; pendingFiles = null; useFiles(p); }
      });
    },
    // the engine found the chosen files unusable: forget them and ask again, with the reason (the engine has ended, so
    // the page starts over)
    onGameFilesProblem: function (text) { filesProblem = text.replace(/\s*(\/persist)?\/game\b/g, ""); },
    onExit: function () {
      if (filesProblem !== null) {
        removeTree(KEPT_GAME);
        save(function () {
          try { sessionStorage.setItem("osf-problem", filesProblem); } catch (e) { /* private mode */ }
          location.reload();
        });
        return;
      }
      save(function () { $("game").hidden = true; $("tools").hidden = true; document.body.classList.remove("playing"); letSleep(); $("ended").hidden = false; });
    },
    onAbort: function (what) {
      $("game").hidden = true;
      $("tools").hidden = true;
      document.body.classList.remove("playing");
      letSleep();
      $("setup").hidden = false;
      status("The game stopped because of an error (" + what + "). Your saved games are kept; reload the page to play again.", true);
    }
  };
  var script = document.createElement("script");
  script.src = "opensimfarm.js";
  script.onerror = function () { status("The game could not be loaded. Check your connection and reload the page.", true); };
  document.body.appendChild(script);

  function mkdirs(path) {
    var parts = path.split("/").filter(Boolean), cur = "";
    parts.forEach(function (p) {
      cur += "/" + p;
      try { FS.mkdir(cur); } catch (e) { /* exists */ }
    });
  }
  function exists(path) { try { FS.stat(path); return true; } catch (e) { return false; } }
  function hasGame(dir) {
    try {
      var names = FS.readdir(dir).map(function (n) { return n.toUpperCase(); });
      return names.indexOf("MEGADATA.IDX") >= 0 && names.indexOf("MEGADATA.EEA") >= 0;
    } catch (e) { return false; }
  }
  function removeTree(path) {
    if (!exists(path)) return;
    FS.readdir(path).forEach(function (n) {
      if (n === "." || n === "..") return;
      var p = path + "/" + n;
      if (FS.isDir(FS.stat(p).mode)) removeTree(p); else FS.unlink(p);
    });
    FS.rmdir(path);
  }
  // writes the in-memory copy of /persist to the browser's storage; a request during a write runs once more after it,
  // so the last change (e.g. at the end of the game) is never left out
  var again = false, waiting = [];
  function save(done) {
    if (done) waiting.push(done);
    if (!FS) { flush(); return; }
    if (syncing) { again = true; return; }
    syncing = true;
    var these = waiting;
    waiting = [];
    FS.syncfs(false, function (err) {
      syncing = false;
      if (err) console.warn("could not write to browser storage", err);
      these.forEach(function (f) { f(); });
      if (again || waiting.length) { again = false; save(); }
    });
  }
  function flush() { var w = waiting; waiting = []; w.forEach(function (f) { f(); }); }

  // ---- the files the player chose: [{ path: "SimFarm/CROPS/CORN.CRP", file: File }]
  var pendingFiles = null;
  function useFiles(list) {
    if (!ready) { pendingFiles = list; status("Loading the game…"); return; }
    // the game folder is the one holding MEGADATA.IDX (the player may have chosen a folder above it)
    var base = null;
    list.forEach(function (f) {
      var parts = f.path.split("/");
      if (parts[parts.length - 1].toUpperCase() === "MEGADATA.IDX") {
        var dir = parts.slice(0, -1).join("/");
        if (base === null || dir.length < base.length) base = dir;
      }
    });
    if (base === null) {
      status("That folder has no MEGADATA.IDX. Choose the folder where SimFarm is installed (it holds MEGADATA.IDX and MEGADATA.EEA).", true);
      return;
    }
    var prefix = base ? base + "/" : "";
    var chosen = [], total = 0;
    list.forEach(function (f) {
      if (f.path.indexOf(prefix) !== 0 || f.file.size > MAX_FILE) return;
      var rel = f.path.slice(prefix.length);
      if (rel.split("/").length > 3) return; // the game folder and its CROPS and SOUND folders are enough
      total += f.file.size;
      if (total <= MAX_TOTAL) chosen.push({ rel: rel, file: f.file });
    });
    var remember = $("remember").checked;
    var dest = remember ? KEPT_GAME : TEMP_GAME;
    status("Reading " + chosen.length + " files…");
    Promise.all(chosen.map(function (c) {
      return c.file.arrayBuffer().then(function (buf) { return { rel: c.rel, data: new Uint8Array(buf) }; });
    })).then(function (files) {
      removeTree(KEPT_GAME);
      removeTree(TEMP_GAME);
      files.forEach(function (f) {
        var path = dest + "/" + f.rel;
        mkdirs(path.slice(0, path.lastIndexOf("/")));
        FS.writeFile(path, f.data);
      });
      if (!hasGame(dest)) { status("MEGADATA.EEA is missing from that folder.", true); return; }
      start(dest);
    }).catch(function (e) {
      status("The files could not be read (" + e + ").", true);
    });
  }

  // a phone or tablet: the game needs a mouse (right click, dragging)
  if (window.matchMedia && matchMedia("(pointer: coarse)").matches && !matchMedia("(any-pointer: fine)").matches)
    status("The game is played with a mouse, so it works best on a computer.");

  // keep the screen on while the game runs (a phone would dim and lock it while you watch the farm): the browser's
  // screen wake lock, taken again when the page comes back into view (the browser lets go of it when hidden) and on
  // a tap if a browser wants one first; let go when the game ends
  var wakeLock = null;
  function keepAwake() {
    if (wakeLock || !navigator.wakeLock || document.hidden || !document.body.classList.contains("playing")) return;
    navigator.wakeLock.request("screen").then(function (l) {
      wakeLock = l;
      l.addEventListener("release", function () { wakeLock = null; });
    }).catch(function () {});
  }
  function letSleep() { if (wakeLock) { wakeLock.release().catch(function () {}); wakeLock = null; } }

  function start(gameDir) {
    // remembered files and saves: ask the browser not to clear them when it runs short of space
    if (navigator.storage && navigator.storage.persist) navigator.storage.persist().catch(function () {});
    if (started) return;
    started = true;
    // one count per game started (GoatCounter, loaded by the page; it counts the page visit by itself)
    if (window.goatcounter && window.goatcounter.count)
      window.goatcounter.count({ path: "play/started", title: "Browser game started", event: true });
    FS.writeFile(CONFIG + "/gamepath.txt", gameDir + "\n");
    save();
    $("setup").hidden = true;
    $("game").hidden = false;
    $("tools").hidden = false;
    document.body.classList.add("playing");
    status("");
    // settings and saves go to the browser's storage now and then, and when the page is left
    setInterval(save, 5000);
    document.addEventListener("visibilitychange", function () { if (document.hidden) save(); else keepAwake(); });
    document.addEventListener("pointerdown", keepAwake);
    keepAwake();
    window.addEventListener("pagehide", function () { save(); });
    setTimeout(function () {
      $("loading").hidden = true;
      $("canvas").focus();
      Module.callMain([]);
    }, 30);
  }

  // ---- remembered files
  function showStored() {
    var kept = hasGame(KEPT_GAME);
    $("stored").hidden = !kept;
    $("btn-pick").className = "btn " + (kept ? "secondary" : "primary");
    $("btn-pick").textContent = kept ? "📂 Choose another folder" : "📂 Choose your SimFarm folder";
  }
  try {
    var problem = sessionStorage.getItem("osf-problem");
    if (problem) {
      sessionStorage.removeItem("osf-problem");
      status(problem.replace(/\s*\n\s*/g, " ") + " Choose the SimFarm folder again.", true);
    }
  } catch (e) { /* private mode */ }
  $("btn-play-stored").addEventListener("click", function () { if (ready) start(KEPT_GAME); });
  $("btn-forget").addEventListener("click", function () {
    removeTree(KEPT_GAME);
    save(showStored);
  });

  // ---- choosing the folder: the button, or dragging the folder onto the page
  $("btn-pick").addEventListener("click", function () { $("pick-input").click(); });
  $("pick-input").addEventListener("change", function (ev) {
    var list = [];
    Array.prototype.forEach.call(ev.target.files, function (f) {
      list.push({ path: f.webkitRelativePath || f.name, file: f });
    });
    if (list.length) useFiles(list);
  });
  var drop = $("drop");
  ["dragenter", "dragover"].forEach(function (t) {
    document.addEventListener(t, function (ev) { if (!started) { ev.preventDefault(); drop.classList.add("over"); } });
  });
  ["dragleave", "drop"].forEach(function (t) {
    document.addEventListener(t, function () { drop.classList.remove("over"); });
  });
  document.addEventListener("drop", function (ev) {
    if (started) return;
    ev.preventDefault();
    var items = ev.dataTransfer.items, entries = [];
    for (var i = 0; i < items.length; i++) {
      var en = items[i].webkitGetAsEntry && items[i].webkitGetAsEntry();
      if (en) entries.push(en);
    }
    status("Reading the folder…");
    Promise.all(entries.map(function (e) { return walk(e, ""); })).then(function (lists) {
      useFiles([].concat.apply([], lists));
    });
  });
  // every file below a dropped folder, with its path from the dropped item
  function walk(entry, dir) {
    var path = dir ? dir + "/" + entry.name : entry.name;
    if (entry.isFile) {
      return new Promise(function (res) {
        entry.file(function (f) { res([{ path: path, file: f }]); }, function () { res([]); });
      });
    }
    if (path.split("/").length > 6) return Promise.resolve([]);
    return new Promise(function (res) {
      var reader = entry.createReader(), all = [];
      (function more() {
        reader.readEntries(function (batch) {
          if (!batch.length) {
            Promise.all(all.map(function (e) { return walk(e, path); })).then(function (l) { res([].concat.apply([], l)); });
            return;
          }
          all = all.concat(Array.prototype.slice.call(batch));
          more();
        }, function () { res([]); });
      })();
    });
  }

  // ---- the top bar can be hidden while playing for more room; the choice is remembered
  var BAR_KEY = "opensimfarm.hideBar";
  function setBarHidden(hide) {
    document.body.classList.toggle("nobar", hide);
    try { if (hide) localStorage.setItem(BAR_KEY, "1"); else localStorage.removeItem(BAR_KEY); } catch (e) {}
    $("canvas").focus();
    window.dispatchEvent(new Event("resize")); // the game takes its new size (it listens to the window's resize)
  }
  // the game's place can also change without a window resize (a phone's address bar coming and going): tell the game
  if (window.ResizeObserver) {
    var lastW = 0, lastH = 0;
    new ResizeObserver(function (en) {
      var r = en[0].contentRect;
      if (Math.round(r.width) === lastW && Math.round(r.height) === lastH) return;
      lastW = Math.round(r.width); lastH = Math.round(r.height);
      window.dispatchEvent(new Event("resize"));
    }).observe($("game"));
  }
  try { if (localStorage.getItem(BAR_KEY)) document.body.classList.add("nobar"); } catch (e) {}
  $("btn-hidebar").addEventListener("click", function () { setBarHidden(true); });
  $("showbar").addEventListener("click", function () { setBarHidden(false); });

  // ---- full screen
  $("btn-full").addEventListener("click", function () {
    var g = $("game");
    (g.requestFullscreen || g.webkitRequestFullscreen).call(g);
    $("canvas").focus();
  });

  // ---- saved games: download and upload
  var SAVES = CONFIG + "/saves";
  function listSaves() {
    var ul = $("save-list");
    ul.textContent = "";
    var names = [];
    try { names = FS.readdir(SAVES).filter(function (n) { return /\.sfm$/i.test(n); }).sort(); } catch (e) { /* none */ }
    if (!names.length) {
      var li = document.createElement("li");
      li.className = "empty";
      li.textContent = "No saved games yet.";
      ul.appendChild(li);
    }
    names.forEach(function (n) {
      var li = document.createElement("li");
      var name = document.createElement("span");
      name.textContent = n;
      var a = document.createElement("a");
      a.href = "#";
      a.textContent = "⬇ Download";
      a.addEventListener("click", function (ev) {
        ev.preventDefault();
        var blob = new Blob([FS.readFile(SAVES + "/" + n)], { type: "application/octet-stream" });
        var link = document.createElement("a");
        link.href = URL.createObjectURL(blob);
        link.download = n;
        document.body.appendChild(link);
        link.click();
        setTimeout(function () { URL.revokeObjectURL(link.href); link.remove(); }, 1000);
      });
      li.appendChild(name);
      li.appendChild(a);
      ul.appendChild(li);
    });
  }
  $("btn-saves").addEventListener("click", function () { listSaves(); $("saves").hidden = false; });
  $("saves-close").addEventListener("click", function () { $("saves").hidden = true; $("canvas").focus(); });
  $("saves").addEventListener("click", function (ev) { if (ev.target === $("saves")) { $("saves").hidden = true; $("canvas").focus(); } });
  $("upload").addEventListener("change", function (ev) {
    Promise.all(Array.prototype.map.call(ev.target.files, function (f) {
      return f.arrayBuffer().then(function (b) {
        var n = f.name.replace(/[^A-Za-z0-9 _.-]/g, "_");
        if (!/\.sfm$/i.test(n)) n += ".SFM";
        FS.writeFile(SAVES + "/" + n, new Uint8Array(b));
      });
    })).then(function () { ev.target.value = ""; save(); listSaves(); });
  });
})();
