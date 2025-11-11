"""Reddit fetching utilities using the public JSON endpoints.

This module keeps dependencies to the standard library so the prototype is easy to run.
"""
from __future__ import annotations

import json
import urllib.request
from typing import Optional, Dict, Any, List


USER_AGENT = "wallpaper-fetcher/0.1 by local-script"


def fetch_new_posts(subreddit: str, limit: int = 10) -> Dict[str, Any]:
    url = f"https://www.reddit.com/r/{subreddit}/new.json?limit={limit}"
    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(req, timeout=15) as resp:
        body = resp.read()
    return json.loads(body.decode())


def extract_image_urls_from_listing(listing: Dict[str, Any]) -> List[str]:
    """Return candidate image URLs from a Reddit listing JSON payload.

    The function prefers direct image links and will try to coerce Imgur links.
    It handles simple reddit-hosted images and single-image posts. Galleries are
    handled by extracting the first media item if available.
    """
    candidates: List[str] = []
    children = listing.get("data", {}).get("children", [])
    for child in children:
        data = child.get("data", {})
        # direct url
        url = data.get("url_overridden_by_dest") or data.get("url")
        if not url:
            continue
        # gallery
        if data.get("is_gallery"):
            media = data.get("media_metadata", {})
            # pick first image in media_metadata
            for k, v in media.items():
                # media entries contain 's' with 'u' url sometimes
                s = v.get("s", {})
                u = s.get("u")
                if u:
                    # reddit encodes &amp; in urls
                    candidates.append(u.replace("&amp;", "&"))
                    break
            continue

        # imgu r without extension (e.g., imgur.com/abc123)
        if "imgur.com/" in url and not any(url.lower().endswith(ext) for ext in (".jpg", ".jpeg", ".png", ".gif", ".webp")):
            candidates.append(url + ".jpg")
            continue

        # reddit hosted images (i.redd.it) or direct image links
        if any(url.lower().endswith(ext) for ext in (".jpg", ".jpeg", ".png", ".gif", ".webp")):
            candidates.append(url)
            continue

        # known image hosters that include extensionless urls can be coerced
        if "i.redd.it" in url or "i.imgur.com" in url:
            candidates.append(url)
            continue

    return candidates


def pick_first_image_url(subreddit: str) -> Optional[str]:
    listing = fetch_new_posts(subreddit, limit=8)
    urls = extract_image_urls_from_listing(listing)
    return urls[0] if urls else None
