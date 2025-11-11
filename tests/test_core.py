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
