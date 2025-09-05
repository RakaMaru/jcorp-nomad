/* /assets/library.js — v2r (recursive schema-aware) */
"use strict";

console.log("Nomad library v2r loaded ✓");

/* ---------- Config ---------- */
const VIDEO_EXTS = new Set([".mp4", ".mov", ".mkv", ".webm", ".m4v"]);
const AUDIO_EXTS = new Set([".mp3", ".wav", ".flac", ".m4a", ".aac", ".ogg"]);
const IMAGE_EXTS = new Set([".jpg", ".jpeg", ".png", ".webp", ".gif"]);
const PLACEHOLDER = "/placeholder.jpg"; // you said this is in the root

/* ---------- Helpers ---------- */
const numChunks = s => s.split(/(\d+)/).map(t => (/\d+/.test(t) ? Number(t) : t.toLowerCase()));
const ncmp = (a, b) => {
  const A = numChunks(a), B = numChunks(b);
  for (let i=0; i<Math.max(A.length,B.length); i++){
    if (A[i]===undefined) return -1;
    if (B[i]===undefined) return  1;
    if (A[i]===B[i]) continue;
    return A[i] < B[i] ? -1 : 1;
  }
  return 0;
};
const ext = p => { const i = p.lastIndexOf("."); return i>=0 ? p.slice(i).toLowerCase() : ""; };
const ensureAbs = p => {
  if (!p) return p;
  const s = String(p);
  if (/^(https?:)?\/\//i.test(s) || s.startsWith("blob:")) return s;
  return s.startsWith("/") ? s : "/" + s;
};
const baseNoExt = p => p.replace(/\.[^.]+$/, "");
const fileName = p => decodeURIComponent(p.split("/").pop() || p);
const titleFrom = it => it.title || it.name || fileName(it.path).replace(/\.[^.]+$/, "");
const guessCoverFromPath = p => `${baseNoExt(p)}.jpg`;

/* ---------- Data ---------- */
async function fetchDB() {
  const url = `/media.json?v=${Date.now()}`; // cache-buster
  const res = await fetch(url, { cache: "no-store" });
  if (!res.ok) throw new Error(`Failed to load media.json (${res.status})`);
  return await res.json();
}

/* Recursively extract entries from any shape (movies, shows.episodes, music, gallery, books, files, …) */
function normalizeItems(db) {
  const out = [];
  const pushIfMedia = (x, inferredType = null) => {
    const p = x?.path || x?.file || x?.url;
    if (!p) return;
    const t = (x.type || inferredType || guessType({ path: p })).toLowerCase();
    const cover = x.cover || x.thumbnail || null;
    out.push({
      ...x,
      title: x.title || x.name || fileName(p).replace(/\.[^.]+$/, ""),
      path: ensureAbs(p),
      cover: cover ? ensureAbs(cover) : null,
      type: t
    });
  };

  const walk = (node, keyCtx = null) => {
    if (Array.isArray(node)) {
      const inferred = keyCtx && /episodes/i.test(keyCtx) ? "show" : null;
      for (const v of node) {
        if (v && typeof v === "object" && (v.path || v.file || v.url)) pushIfMedia(v, inferred);
        else walk(v, keyCtx);
      }
    } else if (node && typeof node === "object") {
      if (node.path || node.file || node.url) pushIfMedia(node, keyCtx);
      for (const k of Object.keys(node)) {
        const child = node[k];
        const inferred =
          /movies?/i.test(k) ? "movie" :
          /shows?/i.test(k)  ? "show"  :
          /music|songs?/i.test(k) ? "music" :
          /gallery|images?/i.test(k) ? "image" :
          /books|pdfs?/i.test(k) ? "book" : null;
        if (Array.isArray(child)) {
          for (const v of child) {
            if (v && typeof v === "object" && (v.path || v.file || v.url)) pushIfMedia(v, inferred);
            else walk(v, k);
          }
        } else {
          walk(child, k);
        }
      }
    }
  };

  walk(db, null);

  // de-dupe by path
  const seen = new Set();
  return out.filter(it => {
    const k = (it.path || "").toLowerCase();
    if (!k || seen.has(k)) return false;
    seen.add(k);
    return true;
  });
}

function guessType(x) {
  const p = (x.path || x.file || "").toLowerCase();
  if (p.includes("/shows/") || p.includes("/tv/")) return "show";
  const e = ext(p);
  if (VIDEO_EXTS.has(e)) return "movie";
  if (AUDIO_EXTS.has(e)) return "music";
  if (IMAGE_EXTS.has(e)) return "image";
  if (p.endsWith(".pdf")) return "book";
  return "other";
}

function filterByCategory(items, category) {
  const q = new URLSearchParams(location.search).get("q")?.toLowerCase() || "";
  const passQ = it => !q || `${it.title} ${it.path}`.toLowerCase().includes(q);

  if (category === "movie") return items.filter(it => (it.type === "movie" || VIDEO_EXTS.has(ext(it.path))) && passQ);
  if (category === "show")  return items.filter(it => (it.type === "show"  || it.path.toLowerCase().includes("/shows/")) && passQ);
  if (category === "music") return items.filter(it => (it.type === "music" || AUDIO_EXTS.has(ext(it.path))) && passQ);
  if (category === "image") return items.filter(it => (it.type === "image" || IMAGE_EXTS.has(ext(it.path))) && passQ);
  return items.filter(passQ);
}

/* ---------- Rendering ---------- */
function makeCardHTML(it, kind) {
  let thumb = it.cover || (kind === "image" ? it.path : guessCoverFromPath(it.path));
  const label = titleFrom(it);
  const href = it.path;
  return `
  <a class="card" href="${href}" title="${label}">
    <img loading="lazy" src="${thumb}" onerror="this.onerror=null;this.src='${PLACEHOLDER}';" alt="">
    <div class="title">${label}</div>
  </a>`;
}

function makeListItemHTML(it) {
  const label = titleFrom(it);
  const href = it.path;
  const left = it.cover || PLACEHOLDER;
  return `
  <a class="row" href="${href}" title="${label}">
    <img loading="lazy" class="thumb" src="${left}" onerror="this.onerror=null;this.src='${PLACEHOLDER}';" alt="">
    <div class="meta">
      <div class="label">${label}</div>
      <div class="sub">${fileName(it.path)}</div>
    </div>
  </a>`;
}

function applyCount(n) {
  const el = document.querySelector("#count");
  if (el) el.textContent = `${n} item${n===1?"":"s"}`;
}

function wireSearch() {
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

async function renderCategory(kind, {mode="grid"} = {}) {
  try {
    wireSearch();
    const db = await fetchDB();
    const items = filterByCategory(normalizeItems(db), kind).sort((a,b) => ncmp(a.title, b.title));
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

/* expose */
window.Nomad = { renderCategory };

/* ---------- Debug overlay (?debug=1) ---------- */
(function(){
  const qs = new URLSearchParams(location.search);
  if (!qs.has("debug")) return;
  const box = document.createElement("div");
  box.style.cssText = "position:fixed;right:12px;bottom:12px;background:#111;color:#eaeaea;border:1px solid #333;border-radius:10px;padding:10px 12px;font:12px/1.3 system-ui;z-index:99999;box-shadow:0 4px 22px rgba(0,0,0,.4)";
  box.innerHTML = "<b>Nomad Debug</b><div id='ndg-status'>Loading…</div>";
  document.body.appendChild(box);
  (async () => {
    try {
      const r = await fetch("/media.json?v="+Date.now(), { cache:"no-store" });
      const ok = r.ok ? "200 OK" : r.status+" "+r.statusText;
      const db = await r.json();
      const items = normalizeItems(db);
      const cat = t => filterByCategory(items, t).length;
      document.getElementById("ndg-status").innerText =
        `media.json: ${ok}\nitems: ${items.length}\nmovies: ${cat("movie")}\nshows: ${cat("show")}\nmusic: ${cat("music")}\nimages: ${cat("image")}`;
    } catch (e) {
      document.getElementById("ndg-status").innerText = "ERR: " + (e.message || e);
    }
  })();
})();
