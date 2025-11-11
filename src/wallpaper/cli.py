"""Command-line interface for the wallpaper tool."""
from __future__ import annotations

import argparse
import hashlib
import json
import logging
import os
import sys
import tempfile
import urllib.request
from pathlib import Path

from . import reddit, wallpaper

logger = logging.getLogger("wallpaper.cli")


def project_root() -> Path:
    # src/wallpaper/cli.py -> project root is two parents up
    return Path(__file__).resolve().parents[2]


def load_index(asset_dir: Path) -> dict:
    idx_file = asset_dir / "index.json"
    if not idx_file.exists():
        return {}
    try:
        return json.loads(idx_file.read_text(encoding="utf8"))
    except Exception:
        return {}


def save_index(asset_dir: Path, index: dict) -> None:
    asset_dir.mkdir(parents=True, exist_ok=True)
    idx_file = asset_dir / "index.json"
    tmp = idx_file.with_suffix(".tmp")
    tmp.write_text(json.dumps(index, indent=2), encoding="utf8")
    tmp.replace(idx_file)


def sha256_of_file(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(8192), b""):
            h.update(chunk)
    return h.hexdigest()


def download_image(url: str, dest_dir: Path) -> Path:
    """Download image to dest_dir/assets and avoid re-downloading known URLs.

    Behavior:
    - Maintains an `index.json` mapping URLs to filenames and sha256 hashes.
    - If URL already present and the file exists, return existing path (no download).
    - Otherwise download to a temp file, compute sha256, check for existing identical content
      in the index; if found, map URL to the existing filename and remove temp.
    - If new, move temp into assets directory with a safe filename and record in index.
    """
    index = load_index(dest_dir)
    # If we've seen this URL before and the file exists, reuse it
    if url in index:
        entry = index[url]
        existing = dest_dir / entry.get("filename", "")
        if existing.exists():
            logger.debug("URL already downloaded, reusing %s", existing)
            return existing

    # derive a suggested filename from URL
    name = url.split("/")[-1].split("?")[0]
    if not name:
        name = "wallpaper.jpg"

    # download to a temporary file first
    dest_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(delete=False) as tmpf:
        tmp_path = Path(tmpf.name)
    try:
        req = urllib.request.Request(url, headers={"User-Agent": reddit.USER_AGENT})
        with urllib.request.urlopen(req, timeout=30) as resp:
            with open(tmp_path, "wb") as f:
                f.write(resp.read())
    except Exception:
        # cleanup and re-raise
        try:
            tmp_path.unlink()
        except Exception:
            pass
        raise

    file_hash = sha256_of_file(tmp_path)
    # check if hash already exists in index
    for u, entry in index.items():
        if entry.get("sha256") == file_hash:
            existing = dest_dir / entry.get("filename")
            if existing.exists():
                logger.debug("Downloaded file matches existing %s; recording URL mapping", existing)
                index[url] = {"filename": entry.get("filename"), "sha256": file_hash}
                save_index(dest_dir, index)
                tmp_path.unlink()
                return existing

    # decide final filename, avoid collisions
    final = dest_dir / name
    base, ext = (final.stem, final.suffix)
    counter = 1
    while final.exists():
        final = dest_dir / f"{base}-{counter}{ext}"
        counter += 1

    try:
        tmp_path.replace(final)
    except OSError as e:
        # Invalid cross-device link: fallback to move (copy+remove)
        if getattr(e, "errno", None) == 18:
            import shutil

            shutil.move(str(tmp_path), str(final))
        else:
            raise
    index[url] = {"filename": final.name, "sha256": file_hash}
    save_index(dest_dir, index)
    logger.debug("Saved new image to %s", final)
    return final


def run(args: argparse.Namespace) -> int:
    if args.verbose:
        logging.basicConfig(level=logging.DEBUG)
    sub = args.subreddit
    print(f"Fetching latest from r/{sub}...")
    try:
        if getattr(args, "random", True):
            url = reddit.pick_random_image_url(sub, limit=getattr(args, "limit", 10))
        else:
            url = reddit.pick_first_image_url(sub)
    except Exception as e:
        print("Failed to fetch subreddit:", e, file=sys.stderr)
        return 2

    if not url:
        print("No suitable image found.")
        return 3

    print("Found image:", url)
    cache_dir = Path(args.cache_dir) if args.cache_dir else Path(os.path.expanduser("~")) / ".cache" / "wallpaper"
    try:
        path = download_image(url, cache_dir)
    except Exception as e:
        print("Failed to download image:", e, file=sys.stderr)
        return 4

    print("Saved to:", path)
    if args.dry_run:
        print("Dry-run mode; not setting wallpaper.")
        return 0

    if args.diagnose:
        # safe diagnostic: print available backends and exit
        info = wallpaper.available_backends()
        print("Backend availability:")
        for k, v in info.items():
            print(f"  {k}: {v}")
        return 0
    ok = wallpaper.set_wallpaper(path, prefer=args.prefer)
    if not ok:
        print("Failed to set wallpaper (no supported backend found).", file=sys.stderr)
        logger.debug("Tried backends; check debug logs for details.")
        return 5

    print("Wallpaper set.")
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(prog="wallpaper")
    parser.add_argument("--subreddit", default="WidescreenWallpaper", help="subreddit to fetch from")
    parser.add_argument("--dry-run", action="store_true", help="download but don't set wallpaper")
    parser.add_argument("--limit", type=int, default=10, help="how many recent posts to consider when picking randomly")
    parser.add_argument("--no-random", dest="random", action="store_false", help="do not pick randomly; use the first suitable image")
    parser.add_argument("--verbose", action="store_true", help="enable debug logging")
    parser.add_argument("--prefer", help="prefer a desktop/backend (e.g. GNOME, KDE)")
    parser.add_argument("--diagnose", action="store_true", help="print available backends and exit (safe)")
    parser.add_argument("--cache-dir", help="where to store downloaded images")
    args = parser.parse_args(argv)
    return run(args)


if __name__ == "__main__":
    raise SystemExit(main())
