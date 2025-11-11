Flatpak packaging notes

This folder contains a minimal Flatpak manifest that builds the current repository tree (source: dir path = "."). It is a template intended to get you started; you may need to adjust the runtime/sdk versions and finish-args for your target environment.

Quick build (from repo root, fish shell):

    # create an isolated build dir for flatpak-builder's output
    mkdir -p flatpak-build

    # build with flatpak-builder (uses the local source directory as the module source)
    flatpak-builder --force-clean --repo=flatpak-repo flatpak-build flatpak/manifest.json

    # optional: create the final bundle (single-file) for distribution
  flatpak build-bundle flatpak-repo wallaroo.flatpak org.matthew.wallaroo

Notes and recommended adjustments

- Runtime / SDK: the manifest uses `org.kde.Platform` / `org.kde.Sdk` as a conservative Qt6-friendly platform; depending on which Qt6 extension is available you may want to use `org.kde.Sdk//6.x` / `org.kde.Platform//6.x` and add `sdk-extensions` like `org.kde.Sdk.Extension.Qt6`.

- Finish-args: the manifest requests `--filesystem=home`, `--socket=x11`, `--socket=wayland`, `--socket=session-bus`, and `--device=dri`. These are broad; you should tighten them as needed:
  - If you want to avoid giving full home access, replace `--filesystem=home` with `--filesystem=xdg-run/` or specific directories.
  - Setting the wallpaper from inside a Flatpak is tricky due to sandboxing; prefer using xdg-desktop-portal APIs where possible. If you need to run desktop-specific commands (e.g. KDE D-Bus calls), you may need `--talk-name` for the specific bus names or `--session-bus` socket access (already requested).

- Building from the repo: the `sources` entry uses `type: dir` and `path: .` so `flatpak-builder` should be invoked from the repo root. If you prefer manifests that fetch code from Git, change the module `sources` to a `git` source with the repo URL and commit.

Troubleshooting

- If the build fails because a Qt6 module is missing, switch the `sdk` to a KDE/Qt SDK that contains Qt6 (for example `org.kde.Sdk//6.4`) and add `"sdk-extensions": ["org.kde.Sdk.Extension.Qt6"]` at the top-level of the manifest.

- If your wallpaper-setting code fails at runtime, run the Flatpak app with extra permissions for debugging (for example temporarily add `--filesystem=host` to `finish-args`) to confirm which calls fail; then narrow permissions.

If you'd like, I can:
- create a `.desktop` file and app icon inside `data/` and add install rules in the CMakeLists so the Flatpak will include them automatically; or
- adapt the manifest to target a specific runtime version (KDE/Qt6) and add a tested set of finish-args for wallpaper setting on your desktop (KDE/GNOME) — tell me which desktop you primarily use and I'll update the manifest and test commands accordingly.
