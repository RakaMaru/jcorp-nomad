#!/usr/bin/env python3
"""
make_posters.py
Create poster JPGs matching each video file's base name.
- One .jpg per video file (same base name, .jpg extension)
- Supports .mp4, .mkv, .avi, .m4v
- Movies & TV: tries TMDb for posters using API key from make_posters.json
- NO placeholders; if no key, dry-run allowed, real run exits with instructions.
- Root is current directory by default; or pass a folder path positional arg.
- Optional folder filters: --only-folders, --exclude-folders

Usage examples:
  python make_posters.py
  python make_posters.py "C:\\common\\transfer\\NomadSSD"
  python make_posters.py Movies --dry-run
  python make_posters.py "D:\\Media" --only-folders Movies,Shows

Config:
  Place make_posters.json alongside this script with:
    {
      "TMDB_API_KEY": "your_api_key_here"
    }
"""
import argparse
import io
import json
import os
import re
import sys
from typing import Optional, Tuple, Dict, Any, Iterable

# Third-party deps
try:
    from PIL import Image
except ImportError:
    print("This script requires Pillow. Install with: pip install pillow", file=sys.stderr)
    sys.exit(1)

try:
    import requests
except ImportError:
    print("This script requires requests. Install with: pip install requests", file=sys.stderr)
    sys.exit(1)

VIDEO_EXTS = {".mp4", ".mkv", ".avi", ".m4v"}

TMDB_BASE = "https://api.themoviedb.org/3"
TMDB_IMG_BASE = "https://image.tmdb.org/t/p"

# Regex patterns
MOVIE_PAREN_RE = re.compile(r"^(?P<title>.+?)\s*\((?P<year>\d{4})\).*?\.(?P<ext>mp4|mkv|avi|m4v)$", re.IGNORECASE)
MOVIE_BRACKET_RE = re.compile(r"^(?P<title>.+?)\s*\[(?P<year>\d{4})\].*?\.(?P<ext>mp4|mkv|avi|m4v)$", re.IGNORECASE)
TV_RE = re.compile(
    r"^(?P<title>.+?)[ ._-]*[Ss](?P<season>\d{1,2})[ ._-]*[Ee](?P<ep>\d{1,3}).*?\.(?P<ext>mp4|mkv|avi|m4v)$",
    re.IGNORECASE
)
YEAR_IN_TEXT_RE = re.compile(r"(?:\(|\[)(?P<year>\d{4})(?:\)|\])")

def parse_size(s: str) -> Tuple[int, int]:
    m = re.match(r"^\s*(\d+)\s*[xX]\s*(\d+)\s*$", s)
    if not m:
        raise argparse.ArgumentTypeError("Size must be like 1000x1500")
    w, h = int(m.group(1)), int(m.group(2))
    if w < 100 or h < 100:
        raise argparse.ArgumentTypeError("Size seems too small; use at least 100x100")
    return w, h

def iter_top_level_entries(root: str) -> Iterable[str]:
    try:
        for name in os.listdir(root):
            yield os.path.join(root, name)
    except FileNotFoundError:
        return

def top_level_name(path: str, root: str) -> str:
    rel = os.path.relpath(path, root)
    parts = rel.split(os.sep)
    return parts[0] if parts else rel

def find_videos(root: str, only: Optional[set]=None, exclude: Optional[set]=None):
    root = os.path.abspath(root)
    for dirpath, dirnames, filenames in os.walk(root):
        tl = top_level_name(dirpath, root)
        if only and tl not in only:
            dirnames[:] = []
            continue
        if exclude and tl in exclude:
            dirnames[:] = []
            continue

        for name in filenames:
            ext = os.path.splitext(name)[1].lower()
            if ext in VIDEO_EXTS:
                yield os.path.join(dirpath, name)

def normalize_title(raw: str) -> str:
    # Replace dots with spaces, collapse spaces, strip odd dashes spacing
    title = raw.replace(".", " ")
    title = re.sub(r"\s*-\s*", " - ", title)
    title = re.sub(r"\s{2,}", " ", title).strip(" -_.")
    return title

def clean_title_for_search(title: str) -> str:
    """
    Remove bracketed/parenthetical qualifiers that are NOT 4-digit years.
    E.g., "[Extended]", "[The Final Cut]", "(Director's Cut)", "(4K Remaster)".
    Keep (YYYY) and [YYYY].
    """
    # Remove (...) or [...] groups that do not contain a 4-digit year
    def _strip_groups(t: str) -> str:
        import re
        # Remove bracket/paren groups anywhere that don't include a 4-digit year
        pattern = re.compile(r'\s*[\(\[](?!\d{4}[\)\]])[^)\]]+[\)\]]')
        prev = None
        while prev != t:
            prev = t
            t = pattern.sub('', t)
        return t
    t = _strip_groups(title)
    # Collapse extra spaces and stray punctuation
    t = re.sub(r'\s{2,}', ' ', t).strip(' -_.')
    return t

def match_media(path: str) -> Optional[Dict[str, Any]]:
    """Return dict with kind ('movie' or 'tv'), parsed fields, and base name."""
    base = os.path.basename(path)

    # TV first
    t = TV_RE.match(base)
    if t:
        return {
            "kind": "tv",
            "title": normalize_title(t.group("title")),
            "season": int(t.group("season")),
            "episode": int(t.group("ep")),
            "ext": t.group("ext").lower(),
            "base": base
        }

    # Movies with (YYYY)
    m = MOVIE_PAREN_RE.match(base)
    if m:
        return {
            "kind": "movie",
            "title": normalize_title(m.group("title")),
            "year": int(m.group("year")),
            "ext": m.group("ext").lower(),
            "base": base
        }

    # Movies with [YYYY]
    b = MOVIE_BRACKET_RE.match(base)
    if b:
        return {
            "kind": "movie",
            "title": normalize_title(b.group("title")),
            "year": int(b.group("year")),
            "ext": b.group("ext").lower(),
            "base": base
        }

    # Try to infer year from parent folder if present
    parent = os.path.basename(os.path.dirname(path))
    y = YEAR_IN_TEXT_RE.search(parent)
    if y:
        # Title is filename without extension, with any bracket/paren year removed
        title_no_ext = os.path.splitext(base)[0]
        title_no_year = YEAR_IN_TEXT_RE.sub("", title_no_ext)
        return {
            "kind": "movie",
            "title": normalize_title(title_no_year),
            "year": int(y.group("year")),
            "ext": os.path.splitext(base)[1].lower().lstrip("."),
            "base": base
        }

    # Fallback: treat as movie by title only (no year)
    title_only = os.path.splitext(base)[0]
    return {
        "kind": "movie",
        "title": normalize_title(title_only),
        "year": None,
        "ext": os.path.splitext(base)[1].lower().lstrip("."),
        "base": base
    }

def ensure_dir(path: str):
    d = os.path.dirname(path)
    if d and not os.path.exists(d):
        os.makedirs(d, exist_ok=True)

def load_config() -> Dict[str, Any]:
    script_dir = os.path.dirname(os.path.abspath(sys.argv[0]))
    cfg_path = os.path.join(script_dir, "make_posters.json")
    if not os.path.exists(cfg_path):
        return {}
    try:
        with open(cfg_path, "r", encoding="utf-8") as f:
            return json.load(f) or {}
    except Exception:
        return {}

def tmdb_headers(api_key: str) -> Dict[str, str]:
    return {"Authorization": f"Bearer {api_key}"} if len(api_key) > 40 else {}

def tmdb_search_movie(api_key: str, title: str, year: Optional[int], lang: str = "en-US"):
    params = {
        "api_key": api_key,
        "query": title,
        "language": lang,
        "include_adult": "false"
    }
    if year:
        params["year"] = year
    r = requests.get(f"{TMDB_BASE}/search/movie", params=params, timeout=20)
    r.raise_for_status()
    data = r.json()
    results = data.get("results") or []
    # If no results and year provided, try again without year
    if not results and year:
        params.pop("year", None)
        r = requests.get(f"{TMDB_BASE}/search/movie", params=params, timeout=20)
        r.raise_for_status()
        data = r.json()
        results = data.get("results") or []
    return results[0] if results else None

def tmdb_search_tv(api_key: str, title: str, lang: str = "en-US"):
    params = {
        "api_key": api_key,
        "query": title,
        "language": lang,
        "include_adult": "false"
    }
    r = requests.get(f"{TMDB_BASE}/search/tv", params=params, timeout=20)
    r.raise_for_status()
    data = r.json()
    results = data.get("results") or []
    return results[0] if results else None

def tmdb_fetch_poster_bytes(api_key: str, poster_path: str) -> Optional[bytes]:
    # Get original size; we just need to save JPEG as-is to match name (no resize requirement given)
    url = f"{TMDB_IMG_BASE}/original{poster_path}"
    r = requests.get(url, timeout=30)
    if r.status_code == 200:
        return r.content
    return None

def process_video(path: str, overwrite: bool, dry_run: bool, api_key: Optional[str], lang: str) -> str:
    parsed = match_media(path)
    # Clean title for TMDb search
    if parsed.get('title'):
        parsed['title'] = clean_title_for_search(parsed['title'])
    base = parsed["base"]
    out_jpg = os.path.join(os.path.dirname(path), os.path.splitext(base)[0] + ".jpg")

    if not overwrite and os.path.exists(out_jpg):
        return f"EXISTS: {out_jpg}"

    if dry_run:
        return f"WOULD CREATE: {out_jpg}"

    if not api_key:
        return ("ERROR (no API key): " + path)

    try:
        if parsed["kind"] == "movie":
            found = tmdb_search_movie(api_key, parsed["title"], parsed.get("year"), lang=lang)
        else:
            found = tmdb_search_tv(api_key, parsed["title"], lang=lang)

        if found and found.get("poster_path"):
            poster_bytes = tmdb_fetch_poster_bytes(api_key, found["poster_path"])
            if poster_bytes:
                ensure_dir(out_jpg)
                with open(out_jpg, "wb") as f:
                    f.write(poster_bytes)
                return f"OK (TMDb): {out_jpg}"
            else:
                return f"ERROR (no poster bytes): {path}"
        else:
            return f"ERROR (no match): {path} -> title='{parsed['title']}' year={parsed.get('year')} kind={parsed['kind']}"
    except Exception as e:
        return f"ERROR: {path} -> {e}"

def main():
    parser = argparse.ArgumentParser(
        description="Create poster JPGs to match video files (movies & TV).",
        formatter_class=argparse.RawTextHelpFormatter
    )
    parser.add_argument(
        "root",
        nargs="?", default=os.getcwd(),
        help="Root folder to scan (default: current folder)"
    )
    parser.add_argument("--overwrite", action="store_true", help="Overwrite existing JPGs")
    parser.add_argument("--dry-run", action="store_true", help="List actions without writing files")
    parser.add_argument("--lang", default="en-US", help="TMDb language (default: en-US)")
    parser.add_argument("--only-folders", default="", help="Comma-separated top-level folders to include (e.g., Movies,Shows)")
    parser.add_argument("--exclude-folders", default="", help="Comma-separated top-level folders to skip (e.g., Samples,Extras)")
    args = parser.parse_args()

    root = args.root
    overwrite = args.overwrite
    dry = args.dry_run
    lang = args.lang
    only = set([s.strip() for s in args.only_folders.split(",") if s.strip()]) or None
    exclude = set([s.strip() for s in args.exclude_folders.split(",") if s.strip()]) or None

    # Load config (JSON only)
    cfg = load_config()
    api_key = cfg.get("TMDB_API_KEY")

    if not dry and not api_key:
        print("[ERROR] Config file not found or TMDB_API_KEY missing.\n", file=sys.stderr)
        print("To use this script for real downloads, create a file named make_posters.json", file=sys.stderr)
        print("in the same folder as make_posters.py with the following contents:\n", file=sys.stderr)
        print('{\n  "TMDB_API_KEY": "your_api_key_here"\n}', file=sys.stderr)
        print("\nGet a free key at: https://www.themoviedb.org/settings/api\n", file=sys.stderr)
        sys.exit(1)

    if dry and not api_key:
        print("[NOTICE] Dry run without API key: showing what WOULD be created.\n")

    total = 0
    done = 0
    skipped = 0
    for video in find_videos(root, only=only, exclude=exclude):
        total += 1
        status = process_video(video, overwrite, dry, api_key, lang)
        print(status)
        if status.startswith("OK"):
            done += 1
        elif status.startswith("EXISTS") or status.startswith("WOULD"):
            skipped += 1

    print("-" * 60)
    print(f"Videos found:   {total}")
    print(f"Posters made:   {done}")
    print(f"Skipped/other:  {skipped}")
    if dry and not api_key:
        print("Tip: create make_posters.json next to the script with your TMDb key to actually download posters.")
    else:
        print("Source: TMDb via make_posters.json (no environment variables).")

if __name__ == "__main__":
    main()
