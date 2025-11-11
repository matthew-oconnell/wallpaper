"""Wallpaper setting helpers for Linux (GNOME via gsettings, feh fallback).

Keep this small and avoid heavy dependencies for the prototype.
"""
from __future__ import annotations

import os
import subprocess
import shutil
import logging
from pathlib import Path
from typing import Optional

logger = logging.getLogger(__name__)


def detect_desktop() -> Optional[str]:
    """Detect the current desktop environment via environment variables.

    Returns a short identifier like 'GNOME', 'KDE', 'XFCE' or None if unknown.
    """
    xdg = os.environ.get("XDG_CURRENT_DESKTOP") or os.environ.get("DESKTOP_SESSION")
    if not xdg:
        return None
    # Some values can be like 'GNOME' or 'gnome'
    return xdg.split(":")[-1].strip()


def set_wallpaper_gnome(path: Path) -> bool:
    uri = f"file://{path.absolute()}"
    try:
        if not shutil.which("gsettings"):
            logger.debug("gsettings not found on PATH")
            return False
        subprocess.run(["gsettings", "set", "org.gnome.desktop.background", "picture-uri", uri], check=True)
        logger.debug("gsettings succeeded")
        return True
    except subprocess.CalledProcessError as e:
        logger.debug("gsettings call failed: %s", e)
        return False
    except Exception as e:
        logger.debug("Unexpected error calling gsettings: %s", e)
        return False


def set_wallpaper_with_feh(path: Path) -> bool:
    try:
        if not shutil.which("feh"):
            logger.debug("feh not found on PATH")
            return False
        subprocess.run(["feh", "--bg-scale", str(path)], check=True)
        logger.debug("feh succeeded")
        return True
    except subprocess.CalledProcessError as e:
        logger.debug("feh call failed: %s", e)
        return False
    except Exception as e:
        logger.debug("Unexpected error calling feh: %s", e)
        return False


def set_wallpaper_kde(path: Path) -> bool:
    # Try to use qdbus to instruct Plasma to change wallpaper
    try:
        if not shutil.which("qdbus"):
            logger.debug("qdbus not found on PATH")
            return False
        # Construct a small JS script for Plasma
        uri = f"file://{path.absolute()}"
        script = (
            "var allDesktops = desktops();"
            "for (i=0;i<allDesktops.length;i++) {"
            "d = allDesktops[i];"
            "d.wallpaperPlugin = 'org.kde.image';"
            "d.currentConfigGroup = ['Wallpaper','org.kde.image','General'];"
            f"d.writeConfig('Image', '{uri}');"
            "}"
        )
        subprocess.run(["qdbus", "org.kde.plasmashell", "/PlasmaShell", "org.kde.PlasmaShell.evaluateScript", script], check=True)
        logger.debug("qdbus plasma script succeeded")
        return True
    except subprocess.CalledProcessError as e:
        logger.debug("qdbus call failed: %s", e)
        return False
    except Exception as e:
        logger.debug("Unexpected error calling qdbus: %s", e)
        return False


def set_wallpaper_xwallpaper(path: Path) -> bool:
    try:
        if not shutil.which("xwallpaper"):
            logger.debug("xwallpaper not found on PATH")
            return False
        subprocess.run(["xwallpaper", "--stretch", str(path)], check=True)
        logger.debug("xwallpaper succeeded")
        return True
    except subprocess.CalledProcessError as e:
        logger.debug("xwallpaper call failed: %s", e)
        return False
    except Exception as e:
        logger.debug("Unexpected error calling xwallpaper: %s", e)
        return False


def set_wallpaper_swaybg(path: Path) -> bool:
    try:
        if not shutil.which("swaybg"):
            logger.debug("swaybg not found on PATH")
            return False
        subprocess.run(["swaybg", "-i", str(path), "-m", "fill"], check=True)
        logger.debug("swaybg succeeded")
        return True
    except subprocess.CalledProcessError as e:
        logger.debug("swaybg call failed: %s", e)
        return False
    except Exception as e:
        logger.debug("Unexpected error calling swaybg: %s", e)
        return False


def available_backends() -> dict:
    """Return a mapping of backend name -> availability (bool).

    This is a safe diagnostic that does not change the wallpaper.
    """
    return {
        "detected_desktop": detect_desktop(),
        "gsettings": bool(shutil.which("gsettings")),
        "qdbus": bool(shutil.which("qdbus")),
        "feh": bool(shutil.which("feh")),
        "xwallpaper": bool(shutil.which("xwallpaper")),
        "swaybg": bool(shutil.which("swaybg")),
    }


def set_wallpaper(path: Path, prefer: Optional[str] = None) -> bool:
    """Try to set wallpaper using the detected desktop environment with fallbacks.

    Returns True on success.
    """
    desktop = prefer or detect_desktop()
    logger.debug("Detected desktop: %s", desktop)

    # Try GNOME first when detected or when gsettings is present
    if desktop and "GNOME" in desktop.upper():
        if set_wallpaper_gnome(path):
            return True

    # Try KDE Plasma
    if desktop and "KDE" in desktop.upper():
        if set_wallpaper_kde(path):
            return True

    # Generic attempts: gsettings (maybe works even if desktop not reported), qdbus, feh, xwallpaper, swaybg
    if set_wallpaper_gnome(path):
        return True
    if set_wallpaper_kde(path):
        return True
    if set_wallpaper_with_feh(path):
        return True
    if set_wallpaper_xwallpaper(path):
        return True
    if set_wallpaper_swaybg(path):
        return True

    logger.debug("No wallpaper backend succeeded")
    return False
