# wallpaper

Small CLI tool to fetch the latest wallpaper from a subreddit and set it as the desktop wallpaper on Linux.

This is an initial prototype. It:
- fetches /r/WidescreenWallpaper/new.json
- picks the first direct image URL (handles i.redd.it, i.imgur.com, reddit-hosted galleries)
- downloads the image to a cache directory
- sets wallpaper via gsettings (GNOME) or feh fallback

Usage:

    python -m wallpaper.cli --subreddit WidescreenWallpaper --dry-run

See `src` for implementation and `tests` for unit tests.
