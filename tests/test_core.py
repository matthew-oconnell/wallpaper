import os
import unittest
from pathlib import Path

from wallpaper import reddit, wallpaper as wp


class TestRedditExtraction(unittest.TestCase):
    def test_extract_image_urls(self):
        # minimal fake listing
        listing = {
            "data": {
                "children": [
                    {"data": {"url": "https://i.redd.it/example.jpg"}},
                    {"data": {"url": "https://imgur.com/abc123"}},
                ]
            }
        }
        urls = reddit.extract_image_urls_from_listing(listing)
        self.assertGreaterEqual(len(urls), 2)
        self.assertIn("i.redd.it/example.jpg", urls[0])
        self.assertTrue(urls[1].endswith(".jpg"))

    def test_pick_random_image_url(self):
        # fake listing and monkeypatch fetch_new_posts
        listing = {
            "data": {
                "children": [
                    {"data": {"url": "https://i.redd.it/one.jpg"}},
                    {"data": {"url": "https://i.redd.it/two.png"}},
                    {"data": {"url": "https://imgur.com/three"}},
                ]
            }
        }
        orig = reddit.fetch_new_posts
        try:
            reddit.fetch_new_posts = lambda subreddit, limit=10: listing
            url = reddit.pick_random_image_url("whatever", limit=3)
            self.assertIn(url, [
                "https://i.redd.it/one.jpg",
                "https://i.redd.it/two.png",
                "https://imgur.com/three.jpg",
            ])
        finally:
            reddit.fetch_new_posts = orig


class TestEnvDetection(unittest.TestCase):
    def test_detect_desktop_from_env(self):
        old = os.environ.get("XDG_CURRENT_DESKTOP")
        os.environ["XDG_CURRENT_DESKTOP"] = "GNOME"
        try:
            d = wp.detect_desktop()
            self.assertIsNotNone(d)
            self.assertIn("GNOME", d.upper())
        finally:
            if old is None:
                del os.environ["XDG_CURRENT_DESKTOP"]
            else:
                os.environ["XDG_CURRENT_DESKTOP"] = old


if __name__ == "__main__":
    unittest.main()
