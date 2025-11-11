"""Simple PySide6 GUI with system tray for the wallpaper tool.

Usage:
    env PYTHONPATH=src python3 -m wallpaper.gui

Features:
- System tray icon with menu: New Random Wallpaper, Show, Quit
- Main window with a button to trigger a new random wallpaper
- Runs fetch/download/set in a background thread and shows notifications

Requires: PySide6 (install with `pip install PySide6`)
"""
from __future__ import annotations

import sys
import threading
from pathlib import Path
from typing import Optional

try:
    from PySide6.QtWidgets import (
        QApplication,
        QWidget,
        QVBoxLayout,
        QPushButton,
        QLabel,
        QSystemTrayIcon,
        QMenu,
        QMessageBox,
    )
    # QAction lives in QtGui for PySide6/Qt6
    from PySide6.QtGui import QIcon, QAction
    from PySide6.QtCore import Qt, Signal, QObject
except Exception as e:
    # Surface the original import error so it's easier to debug environment issues
    raise SystemExit(
        "PySide6 is required for the GUI. Install it: pip install PySide6\n"
        f"Original import error: {e}"
    )

from . import reddit, wallpaper, cli as cli_module


class MainWindow(QWidget):
    def __init__(self, subreddit: str = "WidescreenWallpaper", limit: int = 10):
        super().__init__()
        self.subreddit = subreddit
        self.limit = limit
        self.setWindowTitle("Wallpaper Setter")
        self.setWindowFlags(self.windowFlags() & ~Qt.WindowContextHelpButtonHint)
        self.resize(300, 120)

        layout = QVBoxLayout()
        self.label = QLabel(f"Subreddit: r/{self.subreddit}")
        layout.addWidget(self.label)

        self.button = QPushButton("New random wallpaper")
        self.button.clicked.connect(self.on_new_random)
        layout.addWidget(self.button)

        self.quit_btn = QPushButton("Quit")
        self.quit_btn.clicked.connect(QApplication.instance().quit)
        layout.addWidget(self.quit_btn)

        self.setLayout(layout)

        # tray
        self.tray_icon = QSystemTrayIcon(self)
        # default icon: use a generic icon if available
        self.tray_icon.setIcon(QIcon.fromTheme("image-x-generic"))
        menu = QMenu()
        new_action = QAction("New Random Wallpaper")
        new_action.triggered.connect(self.on_new_random)
        menu.addAction(new_action)
        show_action = QAction("Show")
        show_action.triggered.connect(self.show)
        menu.addAction(show_action)
        quit_action = QAction("Quit")
        quit_action.triggered.connect(QApplication.instance().quit)
        menu.addAction(quit_action)

        self.tray_icon.setContextMenu(menu)
        self.tray_icon.show()

    def notify(self, title: str, message: str) -> None:
        # QSystemTrayIcon.showMessage displays a brief popup in many DEs
        self.tray_icon.showMessage(title, message)

    def on_new_random(self) -> None:
        # disable UI while working
        self.button.setEnabled(False)

        # ensure we have a signal to re-enable the button from worker thread
        class WorkerSignals(QObject):
            finished = Signal()
            message = Signal(str, str)

        signals = WorkerSignals()
        # store signals on the instance so they aren't GC'd while the worker thread runs
        self._worker_signals = signals
        signals.finished.connect(lambda: self.button.setEnabled(True))
        signals.message.connect(lambda t, m: self.notify(t, m))

        def work():
            try:
                url = reddit.pick_random_image_url(self.subreddit, limit=self.limit)
                if not url:
                    signals.message.emit("Wallpaper", "No suitable image found on subreddit.")
                    return
                cache_dir = Path.home() / ".cache" / "wallpaper"
                path = cli_module.download_image(url, cache_dir)
                ok = wallpaper.set_wallpaper(path)
                if ok:
                    signals.message.emit("Wallpaper", f"Set wallpaper from {path.name}")
                else:
                    signals.message.emit("Wallpaper", "Failed to set wallpaper: no supported backend")
            except Exception as e:
                signals.message.emit("Wallpaper Error", str(e))
            finally:
                signals.finished.emit()

        t = threading.Thread(target=work, daemon=True)
        t.start()

    # note: we use Qt signals to re-enable UI from worker threads


def main(argv: Optional[list] = None) -> int:
    app = QApplication(argv or sys.argv)
    w = MainWindow()
    w.show()
    return app.exec()


if __name__ == "__main__":
    raise SystemExit(main())
