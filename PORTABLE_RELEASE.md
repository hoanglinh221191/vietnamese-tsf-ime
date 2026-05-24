# Neokey Portable Release

Neokey release builds are produced with Microsoft Visual C++ and Windows SDK. This
is the supported path for a portable folder that can be copied to another Windows
machine and registered in place.

## Build and package

Run from the repository root:

```bat
package.bat
```

The script performs these steps:

1. Builds the 64-bit TSF DLL: `neokey.dll`.
2. Builds the 32-bit TSF DLL: `neokey32.dll`.
3. Builds the configuration app: `neokey_config.exe`.
4. Runs `core_tests.exe`.
5. Creates a clean portable folder at `dist\Neokey`.

To also create a zip archive:

```bat
package.bat -Zip
```

## Files in the portable folder

The portable folder should contain only the files needed on the target machine:

- `neokey.dll`
- `neokey32.dll`
- `neokey_config.exe`
- `neokey_manifest.json`
- `VERSION`
- `register.ps1`
- `install.bat`
- `uninstall.bat`
- `neokey_shorthand.txt`
- `PORTABLE_RELEASE.md`

Keep both DLLs. Many modern apps are 64-bit, but some desktop apps can still load
32-bit text services, so `neokey32.dll` is required for compatibility.

## Install on another machine

1. Copy the `Neokey` folder to a stable path, for example `C:\Neokey`.
2. Do not move the folder after registration. COM registration stores absolute DLL
   paths.
3. Run `install.bat` normally. It requests Administrator privileges only for
   system-wide DLL registration, while user keyboard settings stay attached to
   the Windows account that launched the installer.
4. To make Neokey the default keyboard for that user after sign-in or reboot,
   run:

```bat
install.bat -SetDefault
```

5. Sign out/in or switch input methods if Windows does not show the keyboard
   immediately.
6. Test in at least one 64-bit app and one 32-bit app when possible.

`install.bat` verifies `neokey_manifest.json` before registration. The manifest
contains SHA-256 hashes and byte sizes for `neokey.dll`, `neokey32.dll`, and
`neokey_config.exe`, plus the package version from `VERSION`; registration stops
if any of those files are missing or do not match the packaged hashes.

To check which registered DLLs Windows is using:

```bat
powershell -NoProfile -ExecutionPolicy Bypass -File register.ps1 -Status
```

The status output also reports whether Neokey is in the current user's language
list and whether it is configured as that user's default input method.

Windows Terminal and common console hosts/shells are bypassed by default
(`windowsterminal.exe`, `openconsole.exe`, `powershell.exe`, `pwsh.exe`,
`cmd.exe`, `conhost.exe`) so command input, `Tab` completion, `Space`, and
terminal editors remain native and responsive. To enable Neokey inside a
terminal, open `neokey_config.exe`, edit the app blocklist, and remove the
relevant process names or disable the blocklist option.

Explorer rename edit controls use a dedicated Win32 backend instead of TSF
composition/ranges. Neokey detects the focused `Edit` control in
`explorer.exe`, updates text through `EM_GETSEL`, `EM_SETSEL`, and
`EM_REPLACESEL`, and bypasses anything that does not look like a native edit
control.
Reconversion never scans backward through whitespace, so modifier keys after
a space remain literal; for example, VNI input keeps `thị 1` instead of
changing it to `thí`.
For hosts that are still warming up their TSF context, such as Word immediately
after launch, Neokey lets the first alphabetic key go to the host and adopts it
back into the next composition only if the character still sits directly before
the caret. This prevents the first character from being swallowed without
turning the next key into an auto-capitalized word start.

To remove Neokey, run:

```bat
uninstall.bat
```

## Release policy

- MSVC is the supported compiler for release artifacts.
- Build flags use `/MT` so the Visual C++ runtime is statically linked.
- MSYS/Clang can be used for experiments, but it is not the packaging target.
- Do not ship files from `build` directly; ship `dist\Neokey`.
- Do not edit the three hashed binaries inside a portable folder after package;
  rebuild/package again so the manifest matches.
