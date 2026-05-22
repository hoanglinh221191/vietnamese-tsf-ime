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
3. Run `install.bat` as Administrator.
4. Sign out/in or switch input methods if Windows does not show the keyboard
   immediately.
5. Test in at least one 64-bit app and one 32-bit app when possible.

`install.bat` verifies `neokey_manifest.json` before registration. The manifest
contains SHA-256 hashes and byte sizes for `neokey.dll`, `neokey32.dll`, and
`neokey_config.exe`; registration stops if any of those files are missing or do
not match the packaged hashes.

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
