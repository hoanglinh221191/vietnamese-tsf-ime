# Neokey

[Tiếng Việt](README.vi.md)

Neokey is an open-source Vietnamese input method for Windows. It uses the
Windows Text Services Framework (TSF) and provides Telex, Simple Telex, and
VNI input methods in a native Windows IME.

## Highlights

- Native Windows TSF input method for 64-bit and 32-bit desktop applications.
- Telex, Simple Telex, and VNI typing methods.
- Configurable correction levels: Off, Normal, Advanced, and Experimental.
- English/code protection, shorthand expansion, and per-application input
  control.
- A small configuration and tray application (`neokey_config.exe`).
- Portable release packages with a SHA-256 manifest for the shipped binaries.

## Install The Portable Release

The simplest way to use Neokey is the portable package from the GitHub
Releases page.

1. Download `Neokey-portable.zip` and extract the `Neokey` folder to a stable
   location such as `C:\Neokey`.
2. Do not move that folder after installation. Windows stores the absolute DLL
   paths during registration.
3. Run `install.bat` and approve the Windows Administrator prompt.
4. Open `neokey_config.exe` to choose Telex, Simple Telex, or VNI and adjust
   correction, shorthand, and application settings.

`install.bat` verifies the release manifest before registration and sets
Neokey as the default input method for the Windows account that runs it.

For the full portable guide, read [PORTABLE_README.md](PORTABLE_README.md).
Vietnamese portable instructions are available in
[PORTABLE_README.vi.md](PORTABLE_README.vi.md).

## Verify Or Remove

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
package.bat -Zip
```

The portable folder is produced at `dist\Neokey`; the optional archive is
`dist\Neokey-portable.zip`. Always distribute the packaged folder or archive,
not files copied directly from `build`.

## About `neokey_config.exe`

`neokey_config.exe` is a separate configuration and tray application. The
typing engine itself is the TSF DLL registered by `install.bat`, so Neokey can
keep typing even when the configuration window is closed. Open this application
when you want to change the input method, correction level, shorthand, startup,
or application blocklist.

## Notes For Terminal Users

Common terminal hosts are bypassed by default so command input, completion,
and terminal editors remain native. To enable Neokey in a terminal, use
`neokey_config.exe` to edit the application blocklist.

## License

Neokey is released under the [MIT License](LICENSE).
