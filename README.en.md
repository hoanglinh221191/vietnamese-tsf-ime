# Neokey

[Tiếng Việt](README.md)

Neokey is an open-source Vietnamese input method for Windows. It uses the
Windows Text Services Framework (TSF) and provides Telex, Simple Telex, and
VNI input methods in a native Windows IME.

## Highlights

- Native Windows TSF input method for 64-bit and 32-bit desktop applications.
- Telex, Simple Telex, and VNI typing methods.
- Configurable correction levels: Off, Normal, Advanced, and Experimental.
- English/code protection, shorthand expansion, and per-application input
  control.
- Automatic password-field protection that bypasses Vietnamese conversion for
  sensitive input.
- Per-application blocklist with optional automatic exclusion while an app is
  in English mode, then automatic restoration on return to Vietnamese mode.
- A small configuration and tray application (`neokey_config.exe`).
- A Windows installer with install, in-place update, and repair modes.
- Portable release packages with a SHA-256 manifest for the shipped binaries.

## Install

The recommended option is `NeokeySetup.exe` from the GitHub Releases page.

1. Double-click `NeokeySetup.exe`. Do not use **Run as administrator**; Setup
   requests elevation itself when needed.
2. Approve the Windows Administrator prompt and choose **Install**.
3. Setup adds Neokey to Windows, makes it the default input method, and opens
   the configuration application when installation finishes.

To update, run `NeokeySetup.exe` from a newer release. Setup detects the
installed version, shows an **Update** action, and preserves settings and
shorthand data. Running the same version enters repair mode; downgrades are
blocked.

### Portable Release

Advanced users can download `Neokey-portable.zip`, extract it to a stable path
such as `C:\Neokey`, and run `install.bat`. Do not move that folder after
registration because Windows stores absolute DLL paths. The script verifies the
manifest and makes Neokey the default input method.

For the full portable guide, read [PORTABLE_README.md](PORTABLE_README.md).
Vietnamese portable instructions are available in
[PORTABLE_README.vi.md](PORTABLE_README.vi.md).

## Password And App Protection

When Windows identifies the active control as a password field, Neokey bypasses
Vietnamese conversion so sensitive text is entered literally.

Use `neokey_config.exe` to block Neokey in a specific application by process
name. **Auto-exclude app when switching to English** temporarily adds the
active application to the blocklist when you switch to English, then removes
only that automatic entry when you return to Vietnamese. Applications you add
to the blocklist yourself stay blocked.

## Verify Or Remove

For the Setup edition, remove Neokey from **Settings > Apps > Installed apps >
Neokey**.

From the portable folder, verify the installed package with:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\register.ps1 -Status
```

To remove Neokey, run `uninstall.bat` from the same folder that was used for
installation.

## Build From Source

Release builds require Microsoft Visual C++ Build Tools and a Windows SDK.
From the repository root:

```bat
build.bat
build\cxx23\core_tests.exe
package.bat -Zip -Installer
```

The portable folder is produced at `dist\Neokey`; release artifacts are
`dist\Neokey-portable.zip` and `dist\NeokeySetup.exe`. Building the installer
requires Inno Setup 6 or 7. Distribute artifacts from `dist`, not files copied
directly from `build`.

## About `neokey_config.exe`

`neokey_config.exe` is a separate configuration and tray application. The
typing engine itself is provided by the registered TSF DLLs, so Neokey keeps
typing even when the configuration window is closed. Open this application when
you want to change the input method, correction level, shorthand, startup, or
application blocklist.

## Notes For Terminal Users

Common terminal hosts are bypassed by default so command input, completion,
and terminal editors remain native. To enable Neokey in a terminal, use
`neokey_config.exe` to edit the application blocklist.

## License

Neokey is released under the [MIT License](LICENSE).
