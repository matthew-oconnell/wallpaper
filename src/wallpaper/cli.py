"""Command-line interface for the wallpaper tool."""
from __future__ import annotations

import argparse
import logging
import os
import sys
import tempfile
import urllib.request
from pathlib import Path

from . import reddit, wallpaper

logger = logging.getLogger("wallpaper.cli")


def download_image(url: str, dest_dir: Path) -> Path:
    dest_dir.mkdir(parents=True, exist_ok=True)
    # derive filename from url
    name = url.split("/")[-1].split("?")[0]
    if not name:
        name = "wallpaper.jpg"
    dest = dest_dir / name
    req = urllib.request.Request(url, headers={"User-Agent": reddit.USER_AGENT})
    with urllib.request.urlopen(req, timeout=30) as resp:
        with open(dest, "wb") as f:
            f.write(resp.read())
    return dest


def run(args: argparse.Namespace) -> int:
    if args.verbose:
        logging.basicConfig(level=logging.DEBUG)
    sub = args.subreddit
    print(f"Fetching latest from r/{sub}...")
    try:
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
    parser.add_argument("--verbose", action="store_true", help="enable debug logging")
    parser.add_argument("--prefer", help="prefer a desktop/backend (e.g. GNOME, KDE)")
    parser.add_argument("--diagnose", action="store_true", help="print available backends and exit (safe)")
    parser.add_argument("--cache-dir", help="where to store downloaded images")
    args = parser.parse_args(argv)
    return run(args)


if __name__ == "__main__":
    raise SystemExit(main())
