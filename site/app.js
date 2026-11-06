// OpenSimFarm site: install tabs, and the releases list read from GitHub when the page loads (so a new release
// shows up here without changing the page).
(function () {
  "use strict";
  var REPO = "alissonkn/opensimfarm";
  var RELEASES = "https://github.com/" + REPO + "/releases";

  // ---- install tabs
  var tabs = document.querySelectorAll(".tab");
  tabs.forEach(function (t) {
    t.addEventListener("click", function () { showTab(t.dataset.tab); });
  });
  function showTab(name) {
    tabs.forEach(function (t) {
      var on = t.dataset.tab === name;
      t.classList.toggle("active", on);
      t.setAttribute("aria-selected", on ? "true" : "false");
    });
    document.querySelectorAll(".panel").forEach(function (p) { p.classList.toggle("active", p.id === "tab-" + name); });
  }

  // ---- what each file is
  var KINDS = [
    { kind: "setup", test: /windows.*setup\.exe$/i, os: "🪟", label: "Windows installer", tab: "win" },
    { kind: "zip", test: /windows.*\.zip$/i, os: "📦", label: "Windows zip (no install)", tab: "zip" },
    { kind: "deb", test: /\.deb$/i, os: "🐧", label: "Debian / Ubuntu (.deb)", tab: "deb" },
    { kind: "rpm", test: /\.rpm$/i, os: "🎩", label: "Fedora / openSUSE (.rpm)", tab: "rpm" },
    { kind: "sums", test: /sha256/i, os: "🔒", label: "Checksums (SHA-256)", tab: null }
  ];
  function kindOf(name) {
    for (var i = 0; i < KINDS.length; i++) if (KINDS[i].test.test(name)) return KINDS[i];
    return { kind: "other", os: "📄", label: "Other file", tab: null };
  }
  function size(n) {
    return n > 1048576 ? (n / 1048576).toFixed(1) + " MB" : Math.max(1, Math.round(n / 1024)) + " KB";
  }
  function el(tag, cls, text) {
    var e = document.createElement(tag);
    if (cls) e.className = cls;
    if (text != null) e.textContent = text;
    return e;
  }

  // ---- the visitor's system: which download the big button offers
  function guessOS() {
    var ua = (navigator.userAgent || "").toLowerCase();
    if (ua.indexOf("windows") >= 0) return "setup";
    if (/ubuntu|debian|mint|pop!_os/.test(ua)) return "deb";
    if (/fedora|red hat|suse|centos/.test(ua)) return "rpm";
    if (ua.indexOf("linux") >= 0) return "linux";
    return null;
  }

  // ---- releases
  var list = document.getElementById("release-list");
  fetch("https://api.github.com/repos/" + REPO + "/releases?per_page=30", {
    headers: { Accept: "application/vnd.github.html+json" } // notes come back as HTML made by GitHub
  })
    .then(function (r) { if (!r.ok) throw new Error(r.status); return r.json(); })
    .then(function (rels) {
      rels = rels.filter(function (r) { return !r.draft; });
      list.textContent = "";
      if (!rels.length) {
        list.appendChild(el("p", "loading", "No releases yet. Check back soon!"));
        return;
      }
      var latest = rels.find(function (r) { return !r.prerelease; }) || rels[0];
      rels.forEach(function (r) { list.appendChild(renderRelease(r, r === latest)); });
      setCallToAction(latest);
      fillFileNames(latest);
    })
    .catch(function () {
      list.textContent = "";
      var p = el("p", "lead");
      p.append("The release list could not be loaded right now. All downloads are on ");
      var a = el("a", null, "GitHub Releases");
      a.href = RELEASES;
      p.append(a, ".");
      list.appendChild(p);
    });

  function renderRelease(r, isLatest) {
    var box = el("article", "release" + (isLatest ? "" : " older"));
    box.id = "release-" + r.tag_name;
    var head = el("div", "release-head");
    head.appendChild(el("h3", null, r.name || r.tag_name));
    if (isLatest) head.appendChild(el("span", "badge", "Latest"));
    if (r.prerelease) head.appendChild(el("span", "badge pre", "Pre-release"));
    var date = r.published_at ? new Date(r.published_at) : null;
    if (date) head.appendChild(el("span", "release-date",
      date.toLocaleDateString(undefined, { year: "numeric", month: "long", day: "numeric" })));
    box.appendChild(head);

    var body = el("div", "release-body");
    var dls = el("div", "downloads");
    var assets = (r.assets || []).slice().sort(function (a, b) {
      return order(kindOf(a.name).kind) - order(kindOf(b.name).kind);
    });
    assets.forEach(function (a) {
      var k = kindOf(a.name);
      var link = el("a", "dl" + (k.kind === "sums" || k.kind === "other" ? " other" : ""));
      link.href = a.browser_download_url;
      link.appendChild(el("span", "os", k.os));
      var txt = el("span");
      txt.appendChild(el("strong", null, k.label + " · " + size(a.size)));
      txt.appendChild(el("small", null, a.name));
      link.appendChild(txt);
      dls.appendChild(link);
    });
    var src = el("a", "dl other");
    src.href = r.zipball_url ? "https://github.com/" + REPO + "/archive/refs/tags/" + r.tag_name + ".zip" : RELEASES;
    src.appendChild(el("span", "os", "🧑‍💻"));
    var st = el("span");
    st.appendChild(el("strong", null, "Source code"));
    st.appendChild(el("small", null, r.tag_name + ".zip"));
    src.appendChild(st);
    dls.appendChild(src);
    body.appendChild(dls);

    if (r.body_html) {
      var notes = el("details", "notes");
      if (isLatest) notes.open = true;
      notes.appendChild(el("summary", null, "Release notes and install instructions"));
      var md = el("div", "md");
      md.innerHTML = r.body_html; // HTML rendered and cleaned by GitHub from the release's own notes
      notes.appendChild(md);
      body.appendChild(notes);
    }
    var gh = el("p");
    var a = el("a", null, "View on GitHub →");
    a.href = r.html_url;
    gh.appendChild(a);
    body.appendChild(gh);
    box.appendChild(body);
    return box;
  }
  function order(kind) { return ["setup", "zip", "deb", "rpm", "other", "sums"].indexOf(kind); }

  // the big Download button: the right file for this visitor, straight from the latest release
  function setCallToAction(rel) {
    var want = guessOS();
    var main = document.getElementById("cta-main");
    var note = document.getElementById("cta-note");
    var pick = null;
    (rel.assets || []).forEach(function (a) { if (!pick && want && kindOf(a.name).kind === want) pick = a; });
    var version = (rel.tag_name || "").replace(/^v/, "");
    if (pick) {
      var k = kindOf(pick.name);
      main.href = pick.browser_download_url;
      main.textContent = "⬇ Download for " + k.label.replace(/ \(.*\)$/, "") + " · v" + version;
      note.textContent = "";
      note.append(size(pick.size) + " · ");
      var other = el("a", null, "other systems and versions");
      other.href = "#releases";
      note.append(other, " · Free and open source (GPL-3.0). Needs your own copy of SimFarm.");
      if (k.tab) showTab(k.tab);
    } else {
      main.href = "#release-" + rel.tag_name;
      main.textContent = "⬇ Download v" + version;
      if (want === "linux") showTab("deb");
    }
  }

  // the install steps name the real files of the latest release
  function fillFileNames(rel) {
    document.querySelectorAll(".fname").forEach(function (n) {
      var a = (rel.assets || []).find(function (x) { return kindOf(x.name).kind === n.dataset.kind; });
      if (a) n.textContent = a.name;
    });
  }
})();
