# Neokey Portable

[Tiếng Việt](README.vi.md)

This folder is a portable Windows release of Neokey. It contains the 64-bit
and 32-bit TSF DLLs, the configuration application, and the installation
scripts needed to register the input method in place.

This edition is intended for users who specifically need a portable folder.
For a normal installation, use `NeokeySetup.exe` from GitHub Releases to get
in-place updates and standard removal through Windows Settings.

## Install

1. Keep this whole folder in a stable location, for example `C:\Neokey`.
   Do not move it after installation because Windows records the DLL paths.
2. Double-click `install.bat`.
3. Approve the Windows Administrator prompt when it appears.
4. Open `neokey_config.exe` to select Telex, Simple Telex, or VNI and adjust
   correction, shorthand, and application settings.

The installer checks `neokey_manifest.json` before registration and sets
Neokey as the default input method for the Windows account that runs it.

Shorthand data is stored separately under `%LOCALAPPDATA%\Neokey` and is
migrated from an older portable file on first install, so replacing the
portable package does not discard it.

If the keyboard does not appear immediately, switch input methods once or
sign out and sign in again.

## About `neokey_config.exe`

`neokey_config.exe` is a separate configuration and tray application. It is
not the Vietnamese typing engine itself: typing is performed by the TSF DLLs
registered during installation. You do not need to keep its configuration
window open to type Vietnamese. Start it whenever you want to change typing
method, correction, shorthand, startup, or application blocklist settings.

## Check The Installation

Open PowerShell in this folder and run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\register.ps1 -Status
```

The result should report registered 64-bit and 32-bit DLLs and matching
manifest hashes.

To verify only the release files before installation:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\register.ps1 -VerifyManifest
```

## Update

1. Run `uninstall.bat` from the old folder.
2. Extract the new `Neokey` folder to the stable location.
3. Run the new `install.bat`.

Do not edit or replace `neokey.dll`, `neokey32.dll`, or `neokey_config.exe`.
They are protected by the release manifest. Build a new package instead.

## Remove

Run `uninstall.bat` from this folder and approve the Administrator prompt.
Afterward, the folder can be deleted.

## Common Questions

**Neokey does not type Vietnamese in one application**

Open `neokey_config.exe`, make sure Vietnamese mode is selected, and check
whether the application is in the blocklist.

**Neokey does not run inside Windows Terminal or a command prompt**

Terminal hosts are bypassed by default to preserve command-line editing and
completion. Remove the relevant application from the blocklist in
`neokey_config.exe` when you want to enable it.

**I moved the folder after installation**

Move it back if possible. Otherwise run `uninstall.bat`, place the folder in
its new permanent location, and run `install.bat` again.

## License

Neokey is released under the MIT License. See `LICENSE`.
