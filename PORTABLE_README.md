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
4. Close and reopen every running application so it loads the new input method.
5. Restart Windows to fully reload the text service.
6. Open `neokey_config.exe` to select Telex, Simple Telex, or VNI and adjust
   correction, shorthand, and application settings.

The installer checks `neokey_manifest.json` before registration and sets
Neokey as the default input method for the Windows account that runs it.

Shorthand data is stored separately under `%LOCALAPPDATA%\Neokey` and is
migrated from an older portable file on first install, so replacing the
portable package does not discard it.

## Dynamic Shorthand

Each entry in **Shorthand Rules** keeps the existing `key=text` format, and all
static rules remain compatible. The text may contain these variables:

- `{{DD/MM/YYYY}}`: the current local date, for example `30/08/2026`.
- `{{DATE}}`: a short alias for `{{DD/MM/YYYY}}`.
- `{{TIME}}`: local 24-hour time as `HH:mm`.
- `{{WEEKDAY}}`: the current weekday in Vietnamese.
- `{{UUID}}`: a new UUID without braces, for example
  `12345678-1234-4abc-8def-1234567890ab`.
- `{{NEWLINE}}` and `{{TAB}}`: a Windows line break or tab character.
- `{{CLIPBOARD}}`: current Unicode clipboard text, preserving its casing and
  line breaks.
- `{{CLIPBOARD|TRIM}}`, `{{CLIPBOARD|UPPER}}`, and
  `{{CLIPBOARD|LOWER}}`: trim edge whitespace, uppercase, or lowercase a copy
  of the clipboard text. They never modify the clipboard itself.
- `{{SELECTION}}`: text selected before the shortcut's first key is typed. Type
  the shortcut while the selection is still active; moving the caret cancels
  the selection, and Neokey does not reuse stale selected text.
- `{{CURSOR}}`: remove the tag and restore the caret to that position after
  expansion. A rule may contain at most one `{{CURSOR}}` tag.

For example:

```text
eml=my_email@example.com
dday=Today is {{DD/MM/YYYY}}
hello=Hello {{CLIPBOARD}},
stamp={{DATE}} {{TIME}} - {{UUID}}
wrap=[{{SELECTION}}]{{CURSOR}}
clip={{CLIPBOARD|TRIM}}
```

Neokey reads the clipboard only when a matching rule is triggered and does not
write its contents to the log. If the clipboard is empty, locked, does not
contain Unicode text, or the result exceeds 16,384 characters, Neokey leaves
the typed shortcut unchanged instead of inserting a partial result.
`{{SELECTION}}` also fails closed when the host does not expose its selection.
For `{{CURSOR}}`, Neokey applies the expansion only when both the text write and
caret placement can be verified; a failure rolls back the text and still handles
the trigger key only once. To preserve the already verified Excel formula state
machine, `{{SELECTION}}` and `{{CURSOR}}` currently fail closed in Excel; the
text-only variables remain available. Unsupported variables remain literal text.
After saving or directly editing the shorthand file, Neokey checks for the new
table at the next expansion boundary; the typing application does not need to
be closed and reopened.

Reopening applications is necessary because a running process can retain an old
TSF DLL. If Neokey is missing or an application still uses the previous build,
restart Windows before further troubleshooting.

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
4. Close and reopen running applications, then restart Windows to load the new
   input method.

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

Neokey is released under the MIT License. See `LICENSE`. The bundled bilingual
English word data has its own attribution notice in `THIRD_PARTY_NOTICES.md`.
