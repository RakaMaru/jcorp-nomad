/* Nomad library — v4.2
   - Strict origin-based filtering (media.json is source of truth)
   - Movies shows ONLY items from movies[]
   - Shows: Series grid → Episodes (grouped), plays inline with stall-retry
   - Music/Gallery/Books/Files also strict to their arrays
*/
"use strict";

console.log("Nomad library v4.2 loaded ✓");

/* =========================
   Config / Types
   ========================= */
const VIDEO_EXTS = new Set([".mp4", ".mov", ".mkv", ".webm", ".m4v", ".avi"]);
const AUDIO_EXTS = new Set([".mp3", ".wav", ".flac", ".m4a", ".aac", ".ogg"]);
const IMAGE_EXTS = new Set([".jpg", ".jpeg", ".png", ".webp", ".gif"]);

const PLACEHOLDER = "/placeholder.jpg";
const USE_STREAM_ROUTE = true;
const STREAM_PREFIX = "/media?file=";

/* =========================
   Helpers
   ========================= */
const numChunks = s => String(s).split(/(\d+)/).map(t => (/\d+/.test(t) ? Number(t) : t.toLowerCase()));
const ncmp = (a, b) => {
  const A = numChunks(a), B = numChunks(b);
  for (let i = 0; i < Math.max(A.length, B.length); i++) {
    if (A[i] === undefined) return -1;
    if (B[i] === undefined) return 1;
    if (A[i] === B[i]) continue;
    return A[i] < B[i] ? -1 : 1;
  }
  return 0;
};
const ext = p => { const i = String(p).lastIndexOf("."); return i >= 0 ? String(p).slice(i).toLowerCase() : ""; };
const ensureAbs = p => {
  if (!p) return p;
  const s = String(p);
  if (/^(https?:)?\/\//i.test(s) || s.startsWith("blob:")) return s;
  return s.startsWith("/") ? s : "/" + s;
};
const baseNoExt = p => String(p).replace(/\.[^.]+$/, "");
const fileName = p => decodeURIComponent(String(p).split("/").pop() || p);
const titleFrom = it => it.title || it.name || fileName(it.path).replace(/\.[^.]+$/, "");
const guessCoverFromPath = p => `${baseNoExt(p)}.jpg`;
const isVideo = p => VIDEO_EXTS.has(ext(p));
const isAudio = p => AUDIO_EXTS.has(ext(p));
const isImage = p => IMAGE_EXTS.has(ext(p));
const $ = sel => document.querySelector(sel);

/* =========================
   Fetch media.json
   ========================= */
async function fetchDB() {
  const url = `/media.json?v=${Date.now()}`; // cache-buster
  const res = await fetch(url, { cache: "no-store" });
  if (!res.ok) throw new Error(`Failed to load media.json (${res.status})`);
  return await res.json();
}

/* =========================
   Normalize (strict origin tagging)
   - origin: 'movies' | 'shows' | 'music' | 'gallery' | 'books' | 'files'
   - episodes from shows carry origin 'shows' and series name
   ========================= */
function normalizeItems(db) {
  const out = [];

  const pushItem = (x, origin, series = null) => {
    if (!x || typeof x !== "object") return;
    const p = x.path || x.file || x.url;
    if (!p) return;
    const cover = x.cover || x.thumbnail || null;

    // type is only for viewer behavior; origin decides page visibility.
    let t = x.type || null;
    if (!t) {
      const e = ext(p);
      if (origin === "movies") t = "movie";
      else if (origin === "shows") t = "show";
      else if (origin === "music") t = "music";
      else if (origin === "gallery") t = "image";
      else if (origin === "books") t = "book";
      else if (origin === "files") {
        if (isVideo(p)) t = "movie"; else if (isAudio(p)) t = "music"; else if (isImage(p)) t = "image"; else if (e === ".pdf" || e === ".epub") t = "book"; else t = "file";
      } else {
        // fallback by extension
        if (isVideo(p)) t = "movie"; else if (isAudio(p)) t = "music"; else if (isImage(p)) t = "image"; else if (p.toLowerCase().endsWith(".pdf") || p.toLowerCase().endsWith(".epub")) t = "book"; else t = "file";
      }
    }

    out.push({
      origin,
      series,                         // only for shows episodes
      title: x.title || x.name || fileName(p).replace(/\.[^.]+$/, ""),
      path: ensureAbs(p),
      cover: cover ? ensureAbs(cover) : null,
      type: String(t).toLowerCase(),
      _raw: x
    });
  };

  // Walkers per section ensure origin is correct.
  const readMovies = arr => Array.isArray(arr) && arr.forEach(it => pushItem(it, "movies"));
  const readMusic  = arr => Array.isArray(arr) && arr.forEach(it => pushItem(it, "music"));
  const readGallery= arr => Array.isArray(arr) && arr.forEach(it => pushItem(it, "gallery"));
  const readBooks  = arr => Array.isArray(arr) && arr.forEach(it => pushItem(it, "books"));
  const readFiles  = arr => Array.isArray(arr) && arr.forEach(it => pushItem(it, "files"));

  const readShows = arr => {
    if (!Array.isArray(arr)) return;
    for (const seriesObj of arr) {
      if (!seriesObj) continue;
      // Series metadata (series-level cover optional)
      const seriesName = seriesObj.title || seriesObj.name || seriesObj.series || null;
      const episodes = seriesObj.episodes || [];
      if (Array.isArray(episodes)) {
        for (const ep of episodes) pushItem(ep, "shows", seriesName);
      }
    }
  };

  readMovies(db.movies);
  readShows (db.shows);
  readMusic (db.music);
  readGallery(db.gallery);
  readBooks (db.books);
  readFiles (db.files);

  // De-dupe by path (keep first occurrence)
  const seen = new Set();
  return out.filter(it => {
    const k = (it.path || "").toLowerCase();
    if (!k || seen.has(k)) return false;
    seen.add(k);
    return true;
  });
}

/* =========================
   Category filter (strict by origin)
   ========================= */
function filterByCategory(items, category) {
  const q = new URLSearchParams(location.search).get("q")?.toLowerCase() || "";
  const passQ = it => !q || `${it.title} ${it.path}`.toLowerCase().includes(q);

  const wantOrigin =
    category === "movie" ? "movies"  :
    category === "show"  ? "shows"   :
    category === "music" ? "music"   :
    category === "image" ? "gallery" :
    category === "book"  ? "books"   :
    category === "file"  ? "files"   : null;

  return wantOrigin ? items.filter(it => it.origin === wantOrigin && passQ) : items.filter(passQ);
}

/* =========================
   Shared renderers
   ========================= */
function makeCardHTML(it, kind) {
  const label = titleFrom(it);
  const href  = it.path; // click behavior overrides
  let   thumb = it.cover || (kind === "image" ? it.path : guessCoverFromPath(it.path));
  return `
  <a class="card" href="${href}" data-title="${label}">
    <img loading="lazy" src="${thumb}" onerror="this.onerror=null;this.src='${PLACEHOLDER}';" alt="">
    <div class="title">${label}</div>
  </a>`;
}
function makeListItemHTML(it) {
  const label = titleFrom(it);
  const href  = it.path;
  const left  = it.cover || PLACEHOLDER;
  return `
  <a class="row" href="${href}" data-title="${label}">
    <img loading="lazy" class="thumb" src="${left}" onerror="this.onerror=null;this.src='${PLACEHOLDER}';" alt="">
    <div class="meta">
      <div class="label">${label}</div>
      <div class="sub">${fileName(it.path)}</div>
    </div>
  </a>`;
}
function applyCount(n) {
  const el = document.querySelector("#count");
  if (el) el.textContent = `${n} item${n === 1 ? "" : "s"}`;
}

/* Simple search form (legacy pages) */
function wireSearchForm() {
  const box = document.querySelector("#search");
  const form = document.querySelector("#searchForm");
  if (!form || !box) return;
  form.addEventListener("submit", ev => {
    ev.preventDefault();
    const q = box.value.trim();
    const url = new URL(location.href);
    if (q) url.searchParams.set("q", q); else url.searchParams.delete("q");
    location.href = url.toString();
  });
}

/* =========================
   Modal / viewers (shared)
   ========================= */
function mediaUrl(path) { const p = ensureAbs(path); return USE_STREAM_ROUTE ? (STREAM_PREFIX + encodeURIComponent(p)) : p; }
function downloadUrl(path) { return ensureAbs(path); }

function ensureActionModal() {
  let m = $("#actionModal");
  if (m) return m;
  m = document.createElement("div");
  m.id = "actionModal";
  m.className = "modal";
  m.innerHTML = `
    <div class="dialog">
      <h2>What would you like to do?</h2>
      <div class="actions">
        <button id="viewButton">View in Browser</button>
        <button id="downloadButton">Download</button>
      </div>
    </div>`;
  document.body.appendChild(m);
  return m;
}
function ensureVideoModal() {
  let v = $("#videoModal");
  if (v) return v;
  v = document.createElement("div");
  v.id = "videoModal";
  v.className = "modal video-modal";
  v.innerHTML = `
    <div class="dialog">
      <div class="titlebar">
        <div class="title">Playing…</div>
        <button type="button">Close</button>
      </div>
      <video id="videoPlayer" controls playsinline></video>
    </div>`;
  document.body.appendChild(v);
  return v;
}
function ensureImageModal() {
  let m = $("#imageModal");
  if (m) return m;
  m = document.createElement("div");
  m.id = "imageModal";
  m.className = "modal image-modal";
  m.innerHTML = `
    <div class="dialog">
      <div class="titlebar">
        <div class="title">Image</div>
        <button type="button">Close</button>
      </div>
      <img id="imageViewer" alt="" />
    </div>`;
  document.body.appendChild(m);
  return m;
}
function ensureAudioBar() {
  let b = $("#audioBar");
  if (b) return b;
  b = document.createElement("div");
  b.id = "audioBar";
  b.innerHTML = `
    <div class="ab-left">
      <button id="abPrev" title="Previous">⏮</button>
      <button id="abPlay" title="Play/Pause">▶</button>
      <button id="abNext" title="Next">⏭</button>
      <button id="abLoop" title="Loop">🔁</button>
      <button id="abShuffle" title="Shuffle">🔀</button>
      <div id="abTitle" class="ab-title">—</div>
    </div>
    <div class="ab-right">
      <input id="abSeek" type="range" min="0" max="1000" value="0" />
      <div id="abTime" class="ab-time">0:00 / 0:00</div>
      <audio id="abAudio"></audio>
    </div>`;
  document.body.appendChild(b);
  return b;
}

/* Stall watcher for video */
function attachStallWatcher(player, src) {
  const MAX_RETRIES = 2, STALL_MS = 8000;
  let tries = 0, timer;
  const schedule = () => {
    clearTimeout(timer);
    timer = setTimeout(() => {
      if (!player.paused && player.readyState < 3 && tries < MAX_RETRIES) {
        tries++;
        const bust = (src.includes("?") ? "&" : "?") + "_=" + Date.now();
        player.src = src + bust;
        player.load();
        try { player.currentTime = player.currentTime; } catch (_) {}
        player.play().catch(() => {});
        schedule();
      }
    }, STALL_MS);
  };
  player.onplaying = schedule;
  player.onpause = () => clearTimeout(timer);
  player.onended = () => clearTimeout(timer);
}
function fmtTime(sec) {
  if (!isFinite(sec)) return "0:00";
  const s = Math.floor(sec % 60).toString().padStart(2, "0");
  const m = Math.floor(sec / 60).toString();
  return `${m}:${s}`;
}

/* =========================
   Base renderer (unchanged API for simple pages)
   ========================= */
async function renderCategory(kind, { mode = "grid" } = {}) {
  try {
    wireSearchForm();
    const db = await fetchDB();
    const items = filterByCategory(normalizeItems(db), kind).sort((a, b) => ncmp(titleFrom(a), titleFrom(b)));
    applyCount(items.length);
    const container = document.querySelector("#grid") || document.querySelector("#list");
    if (!container) return;
    container.innerHTML = (mode === "list")
      ? items.map(makeListItemHTML).join("")
      : items.map(it => makeCardHTML(it, kind)).join("");
  } catch (err) {
    console.error(err);
    const container = document.querySelector("#grid") || document.querySelector("#list");
    if (container) container.innerHTML = `<div class="error">Failed to load library: ${String(err)}</div>`;
  }
}
window.Nomad = window.Nomad || {};
window.Nomad.renderCategory = renderCategory;

/* =========================
   Movies controller (now STRICT to origin 'movies')
   ========================= */
(function MoviesUI() {
  const state = {
    all: [], filtered: [],
    size: localStorage.getItem("nomadMoviesSize") || "medium",
    sort: localStorage.getItem("nomadMoviesSort") || "nameAsc",
    q: ""
  };

  function applyFilters() {
    const q = state.q;
    let arr = state.all;
    if (q) arr = arr.filter(it => (titleFrom(it) + " " + it.path).toLowerCase().includes(q));
    state.filtered = arr.slice().sort((a, b) =>
      state.sort === "nameDesc" ? ncmp(titleFrom(b), titleFrom(a)) : ncmp(titleFrom(a), titleFrom(b))
    );
  }
  function paintGrid() {
    applyFilters();
    const grid = $("#grid");
    if (!grid) return;
    grid.classList.remove("small", "medium", "large");
    grid.classList.add(state.size);
    applyCount(state.filtered.length);
    grid.innerHTML = state.filtered.map(it => makeCardHTML(it, "movie")).join("");
    grid.querySelectorAll(".card").forEach(card => {
      card.addEventListener("click", ev => {
        ev.preventDefault();
        const href  = card.getAttribute("href");
        const title = card.dataset.title || card.querySelector(".title")?.textContent || fileName(href);
        openActionModal(href, title);
      });
    });
  }
  function wireToolbar() {
    const input = $("#searchInput");
    if (input) {
      let t; input.addEventListener("input", e => {
        clearTimeout(t); t = setTimeout(() => { state.q = (e.target.value || "").trim().toLowerCase(); paintGrid(); }, 150);
      });
    }
    const sort = $("#sortSelect");
    if (sort) {
      sort.value = state.sort;
      sort.addEventListener("change", () => { state.sort = sort.value; localStorage.setItem("nomadMoviesSort", state.sort); paintGrid(); });
    }
    document.querySelectorAll(".view-btn").forEach(btn => {
      btn.classList.toggle("active", btn.dataset.size === state.size);
      btn.addEventListener("click", () => {
        document.querySelectorAll(".view-btn").forEach(b => b.classList.remove("active"));
        btn.classList.add("active");
        state.size = btn.dataset.size || "medium";
        localStorage.setItem("nomadMoviesSize", state.size);
        paintGrid();
      });
    });
  }

  const actionModal = ensureActionModal();
  const viewBtn     = $("#viewButton");
  const downloadBtn = $("#downloadButton");
  const vModal  = ensureVideoModal();
  const vTitle  = $("#videoModal .title");
  const vClose  = $("#videoModal .titlebar button");
  const vPlayer = $("#videoPlayer");

  function openActionModal(fileHref, title) {
    if (!actionModal || !viewBtn || !downloadBtn) { location.href = mediaUrl(fileHref); return; }
    actionModal.classList.add("open");
    viewBtn.onclick = () => { actionModal.classList.remove("open"); openInlinePlayer(mediaUrl(fileHref), title); };
    downloadBtn.onclick = () => {
      actionModal.classList.remove("open");
      const a = document.createElement("a");
      a.href = downloadUrl(fileHref);
      a.download = title || "file";
      a.click();
    };
  }
  actionModal.addEventListener("click", e => { if (e.target === actionModal) actionModal.classList.remove("open"); });

  function openInlinePlayer(src, title) {
    if (!vModal || !vPlayer) { location.href = src; return; }
    if (vTitle) vTitle.textContent = title || "Video";
    vModal.classList.add("open");
    attachStallWatcher(vPlayer, src);
    vPlayer.src = src;
    vPlayer.play().catch(() => {});
  }
  vModal.addEventListener("click", e => { if (e.target === vModal) { vPlayer.pause(); vModal.classList.remove("open"); } });
  vClose && vClose.addEventListener("click", () => { vPlayer.pause(); vModal.classList.remove("open"); });

  async function renderMovies() {
    const db = await fetchDB();
    const all = filterByCategory(normalizeItems(db), "movie"); // <-- strict origin
    state.all = all.sort((a, b) => ncmp(titleFrom(a), titleFrom(b)));
    wireToolbar();
    paintGrid();
  }
  window.Nomad.renderMovies = renderMovies;
})();

/* =========================
   Generic controller for other pages (strict)
   ========================= */
window.Nomad.renderWithControls = async function(kind, { mode = "grid" } = {}) {
  const state = {
    all: [], filtered: [],
    sizeKey: `nomad${kind}Size`,
    sortKey: `nomad${kind}Sort`,
    size: localStorage.getItem(`nomad${kind}Size`) || (mode === "list" ? "medium" : "medium"),
    sort: localStorage.getItem(`nomad${kind}Sort`) || "nameAsc",
    q: ""
  };

  const db = await fetchDB();
  const base = normalizeItems(db);
  const all  = filterByCategory(base, kind);
  state.all  = all.sort((a, b) => ncmp(titleFrom(a), titleFrom(b)));

  const input = $("#searchInput");
  if (input) {
    let t; input.addEventListener("input", e => {
      clearTimeout(t); t = setTimeout(() => { state.q = (e.target.value || "").trim().toLowerCase(); paint(); }, 150);
    });
  }
  const sort = $("#sortSelect");
  if (sort) {
    sort.value = state.sort;
    sort.addEventListener("change", () => {
      state.sort = sort.value; localStorage.setItem(state.sortKey, state.sort); paint();
    });
  }
  document.querySelectorAll(".view-btn").forEach(btn => {
    btn.classList.toggle("active", btn.dataset.size === state.size);
    btn.addEventListener("click", () => {
      document.querySelectorAll(".view-btn").forEach(b => b.classList.remove("active"));
      btn.classList.add("active");
      state.size = btn.dataset.size || "medium";
      localStorage.setItem(state.sizeKey, state.size);
      paint();
    });
  });

  const actionModal = ensureActionModal();
  const viewBtn     = $("#viewButton");
  const downloadBtn = $("#downloadButton");
  const vModal  = ensureVideoModal();
  const vTitle  = $("#videoModal .title");
  const vClose  = $("#videoModal .titlebar button");
  const vPlayer = $("#videoPlayer");
  const imgModal = ensureImageModal();
  const imgTitle = $("#imageModal .title");
  const imgClose = $("#imageModal .titlebar button");
  const imgView  = $("#imageViewer");
  const audioBar = ensureAudioBar();
  const abPrev   = $("#abPrev"),    abPlay = $("#abPlay"), abNext = $("#abNext");
  const abLoop   = $("#abLoop"),    abShuffle = $("#abShuffle");
  const abTitle  = $("#abTitle"),   abSeek = $("#abSeek"), abTime = $("#abTime");
  const abAudio  = $("#abAudio");

  let audioList = [], audioIndex = -1, audioLoop = false, audioShuffle = false;

  function setAudioSource(idx) {
    if (idx < 0 || idx >= audioList.length) return;
    audioIndex = idx;
    const it = audioList[audioIndex];
    abTitle.textContent = titleFrom(it);
    abAudio.src = mediaUrl(it.path);
    abAudio.play().catch(()=>{});
  }
  abPlay.onclick = () => { if (abAudio.paused) abAudio.play(); else abAudio.pause(); };
  abPrev.onclick = () => {
    if (!audioList.length) return;
    if (audioShuffle) setAudioSource(Math.floor(Math.random()*audioList.length));
    else setAudioSource((audioIndex - 1 + audioList.length) % audioList.length);
  };
  abNext.onclick = () => {
    if (!audioList.length) return;
    if (audioShuffle) setAudioSource(Math.floor(Math.random()*audioList.length));
    else setAudioSource((audioIndex + 1) % audioList.length);
  };
  abLoop.onclick = () => { audioLoop = !audioLoop; abLoop.classList.toggle("active", audioLoop); };
  abShuffle.onclick = () => { audioShuffle = !audioShuffle; abShuffle.classList.toggle("active", audioShuffle); };
  abAudio.addEventListener("timeupdate", () => {
    const p = abAudio.duration ? (abAudio.currentTime / abAudio.duration) : 0;
    abSeek.value = Math.round(p * 1000);
    abTime.textContent = `${fmtTime(abAudio.currentTime)} / ${fmtTime(abAudio.duration || 0)}`;
  });
  abSeek.addEventListener("input", () => {
    if (!abAudio.duration) return;
    abAudio.currentTime = (abSeek.value / 1000) * abAudio.duration;
  });
  abAudio.addEventListener("ended", () => {
    if (audioLoop) { abAudio.currentTime = 0; abAudio.play(); return; }
    abNext.onclick();
  });

  imgModal.addEventListener("click", e => { if (e.target === imgModal) imgModal.classList.remove("open"); });
  imgClose && imgClose.addEventListener("click", () => imgModal.classList.remove("open"));
  vModal.addEventListener("click", e => { if (e.target === vModal) { vPlayer.pause(); vModal.classList.remove("open"); } });
  vClose && vClose.addEventListener("click", () => { vPlayer.pause(); vModal.classList.remove("open"); });

  function openAction(it) {
    const title = titleFrom(it);
    const href  = it.path;
    actionModal.classList.add("open");
    viewBtn.onclick = () => {
      actionModal.classList.remove("open");
      if (isVideo(href)) {
        if (vTitle) vTitle.textContent = title;
        const src = mediaUrl(href);
        vModal.classList.add("open");
        attachStallWatcher(vPlayer, src);
        vPlayer.src = src; vPlayer.play().catch(()=>{});
      } else if (isAudio(href)) {
        audioList = state.filtered.filter(x => isAudio(x.path));
        const idx = audioList.findIndex(x => x.path === href);
        setAudioSource(Math.max(idx, 0));
        window.scrollTo({ top: document.body.scrollHeight, behavior: "smooth" });
      } else if (isImage(href)) {
        if (imgTitle) imgTitle.textContent = title;
        imgView.src = downloadUrl(href);
        imgModal.classList.add("open");
      } else if (href.toLowerCase().endsWith(".pdf")) {
        window.open(downloadUrl(href), "_blank");
      } else {
        location.href = downloadUrl(href);
      }
    };
    downloadBtn.onclick = () => {
      actionModal.classList.remove("open");
      const a = document.createElement("a");
      a.href = downloadUrl(href);
      a.download = title || "file";
      a.click();
    };
  }

  function applyFilters() {
    const q = state.q;
    let arr = state.all;
    if (q) arr = arr.filter(it => (titleFrom(it) + " " + it.path).toLowerCase().includes(q));
    state.filtered = arr.slice().sort((a, b) =>
      state.sort === "nameDesc" ? ncmp(titleFrom(b), titleFrom(a)) : ncmp(titleFrom(a), titleFrom(b))
    );
  }

  function paint() {
    applyFilters();
    const container = $("#grid") || $("#list");
    if (!container) return;
    container.classList.remove("small", "medium", "large");
    container.classList.add(state.size);

    applyCount(state.filtered.length);
    container.innerHTML = (mode === "list")
      ? state.filtered.map(makeListItemHTML).join("")
      : state.filtered.map(it => makeCardHTML(it, kind)).join("");

    container.querySelectorAll(mode === "list" ? ".row" : ".card").forEach(node => {
      node.addEventListener("click", ev => {
        ev.preventDefault();
        const href  = node.getAttribute("href");
        const title = node.dataset.title || node.querySelector(".title")?.textContent || fileName(href);
        const item  = state.filtered.find(x => x.path === href) || { path: href, title };
        openAction(item);
      });
    });
  }

  paint();
};

/* =========================
   Shows (grouped) — Series grid → Episodes
   ========================= */
window.Nomad.renderShowsGrouped = async function() {
  const state = {
    sizeKey: "nomadshowSize",
    sortKey: "nomadshowSort",
    size: localStorage.getItem("nomadshowSize") || "medium",
    sort: localStorage.getItem("nomadshowSort") || "nameAsc",
    q: "",
    currentSeries: new URLSearchParams(location.search).get("series") || null,
    seriesMap: new Map(),   // name -> { name, cover, episodes[] }
    seriesList: [],
    filteredSeries: [],
    filteredEpisodes: [],
  };

  // toolbar
  const input = $("#searchInput");
  if (input) {
    let t; input.addEventListener("input", e => {
      clearTimeout(t);
      t = setTimeout(() => { state.q = (e.target.value||"").trim().toLowerCase(); paint(); }, 150);
    });
  }
  const sort = $("#sortSelect");
  if (sort) {
    sort.value = state.sort;
    sort.addEventListener("change", () => {
      state.sort = sort.value; localStorage.setItem(state.sortKey, state.sort); paint();
    });
  }
  document.querySelectorAll(".view-btn").forEach(btn => {
    btn.classList.toggle("active", btn.dataset.size === state.size);
    btn.addEventListener("click", () => {
      document.querySelectorAll(".view-btn").forEach(b => b.classList.remove("active"));
      btn.classList.add("active");
      state.size = btn.dataset.size || "medium";
      localStorage.setItem(state.sizeKey, state.size);
      paint();
    });
  });

  // data
  const db   = await fetchDB();
  const all  = filterByCategory(normalizeItems(db), "show"); // strict origin: shows only

  // group into series
  const seriesPoster = name => `/Shows/${encodeURIComponent(name)}.jpg`;
  for (const it of all) {
    const s = it.series || null;
    if (!s) continue;
    if (!state.seriesMap.has(s)) state.seriesMap.set(s, { name: s, cover: seriesPoster(s), episodes: [] });
    state.seriesMap.get(s).episodes.push(it);
  }
  state.seriesList = Array.from(state.seriesMap.values())
    .map(x => ({ name: x.name, cover: x.cover, count: x.episodes.length }))
    .sort((a,b) => state.sort === "nameDesc" ? ncmp(b.name, a.name) : ncmp(a.name, b.name));

  // modals
  const actionModal = ensureActionModal();
  const viewBtn     = $("#viewButton");
  const downloadBtn = $("#downloadButton");
  const vModal  = ensureVideoModal();
  const vTitle  = $("#videoModal .title");
  const vClose  = $("#videoModal .titlebar button");
  const vPlayer = $("#videoPlayer");
  actionModal.addEventListener("click", e => { if (e.target === actionModal) actionModal.classList.remove("open"); });
  vModal.addEventListener("click", e => { if (e.target === vModal) { vPlayer.pause(); vModal.classList.remove("open"); } });
  vClose && vClose.addEventListener("click", () => { vPlayer.pause(); vModal.classList.remove("open"); });

  // breadcrumb line
  function ensureCrumb() {
    let bar = $("#showsCrumb");
    if (!bar) {
      bar = document.createElement("div");
      bar.id = "showsCrumb";
      bar.style.cssText = "margin:8px 0 6px 0; opacity:.9;";
      const container = document.querySelector(".container") || document.body;
      container.insertBefore(bar, container.firstChild);
    }
    return bar;
  }

  function applyFilters() {
    const q = state.q;
    if (!state.currentSeries) {
      state.filteredSeries = !q ? state.seriesList : state.seriesList.filter(s => s.name.toLowerCase().includes(q));
    } else {
      const eps = state.seriesMap.get(state.currentSeries)?.episodes || [];
      const sorted = eps.slice().sort((a,b) => state.sort === "nameDesc" ? ncmp(titleFrom(b), titleFrom(a)) : ncmp(titleFrom(a), titleFrom(b)));
      state.filteredEpisodes = !q ? sorted : sorted.filter(it => (titleFrom(it) + " " + it.path).toLowerCase().includes(q));
    }
  }

  function paint() {
    applyFilters();
    const grid = $("#grid");
    if (!grid) return;
    grid.classList.remove("small","medium","large");
    grid.classList.add(state.size);

    if (!state.currentSeries) {
      // series grid
      const list = state.filteredSeries;
      applyCount(list.length);
      grid.innerHTML = list.map(s => `
        <a class="card" href="?series=${encodeURIComponent(s.name)}" data-series="${s.name}">
          <img loading="lazy" src="${s.cover}" onerror="this.onerror=null;this.src='${PLACEHOLDER}'" alt="">
          <div class="title">${s.name} <span style="opacity:.7">(${s.count})</span></div>
        </a>`).join("");
      const crumb = ensureCrumb();
      crumb.innerHTML = "";
      grid.querySelectorAll(".card").forEach(card => {
        card.addEventListener("click", ev => {
          ev.preventDefault();
          const name = card.dataset.series;
          state.currentSeries = name;
          const url = new URL(location.href); url.searchParams.set("series", name); history.replaceState(null, "", url);
          paint();
        });
      });
    } else {
      // episode grid
      const eps = state.filteredEpisodes;
      applyCount(eps.length);
      const sCover = `/Shows/${encodeURIComponent(state.currentSeries)}.jpg`;
      grid.innerHTML = eps.map(it => {
        const label = titleFrom(it);
        const thumb = it.cover || guessCoverFromPath(it.path) || sCover;
        return `
          <a class="card" href="${it.path}" data-title="${label}">
            <img loading="lazy" src="${thumb}" onerror="this.onerror=null;this.src='${PLACEHOLDER}'" alt="">
            <div class="title">${label}</div>
          </a>`;
      }).join("");

      const crumb = ensureCrumb();
      crumb.innerHTML = `<a href="#" id="backSeries">← All Shows</a> &nbsp; <span style="opacity:.8">/</span> &nbsp; <strong>${state.currentSeries}</strong>`;
      const back = $("#backSeries");
      back && back.addEventListener("click", ev => {
        ev.preventDefault();
        state.currentSeries = null;
        const url = new URL(location.href); url.searchParams.delete("series"); history.replaceState(null, "", url);
        paint();
      });

      grid.querySelectorAll(".card").forEach(card => {
        card.addEventListener("click", ev => {
          ev.preventDefault();
          const href  = card.getAttribute("href");
          const title = card.dataset.title || card.querySelector(".title")?.textContent || fileName(href);
          actionModal.classList.add("open");
          viewBtn.onclick = () => {
            actionModal.classList.remove("open");
            if (vTitle) vTitle.textContent = `${state.currentSeries} · ${title}`;
            const src = mediaUrl(href);
            vModal.classList.add("open");
            attachStallWatcher(vPlayer, src);
            vPlayer.src = src; vPlayer.play().catch(()=>{});
          };
          downloadBtn.onclick = () => {
            actionModal.classList.remove("open");
            const a = document.createElement("a");
            a.href = downloadUrl(href);
            a.download = title || "episode";
            a.click();
          };
        });
      });
    }
  }

  paint();
};

/* =========================
   Debug overlay (?debug=1) + simple validator
   ========================= */
(function DebugOverlay(){
  const qs = new URLSearchParams(location.search);
  if (!qs.has("debug")) return;

  const box = document.createElement("div");
  box.style.cssText = "position:fixed;right:12px;bottom:12px;background:#111;color:#eaeaea;border:1px solid #333;border-radius:10px;padding:10px 12px;font:12px/1.3 system-ui;z-index:99999;box-shadow:0 4px 22px rgba(0,0,0,.4)";
  box.innerHTML = "<b>Nomad Debug</b><div id='ndg-status'>Loading…</div>";
  document.body.appendChild(box);

  (async () => {
    try {
      const r = await fetch("/media.json?v=" + Date.now(), { cache: "no-store" });
      const ok = r.ok ? "200 OK" : r.status + " " + r.statusText;
      const db = await r.json();
      const items = normalizeItems(db);
      const c = t => filterByCategory(items, t).length;

      // simple warnings: images inside movies, etc.
      const warn = [];
      (db.movies || []).forEach(m => { const p = (m.path||m.file||"").toLowerCase(); if (IMAGE_EXTS.has(ext(p))) warn.push("image in movies: " + (m.title||m.name||p)); });
      (db.books || []).forEach(b => { const p = (b.path||b.file||"").toLowerCase(); if (isVideo(p) || isAudio(p)) warn.push("non-book in books: " + (b.title||b.name||p)); });

      document.getElementById("ndg-status").innerText =
        `media.json: ${ok}\nitems: ${items.length}\nmovies: ${c("movie")}\nshows: ${c("show")}\nmusic: ${c("music")}\nimages: ${c("image")}\nbooks: ${c("book")}\nfiles: ${c("file")}\n` +
        (warn.length ? ("\nWARNINGS:\n- " + warn.join("\n- ")) : "");
    } catch (e) {
      document.getElementById("ndg-status").innerText = "ERR: " + (e.message || e);
    }
  })();
})();
