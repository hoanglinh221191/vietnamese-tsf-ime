<p align="center">
  <img src="assets/neokey-logo.png" alt="Neokey logo" width="128">
</p>

<h1 align="center">Neokey</h1>

<p align="center"><a href="README.md">Tiếng Việt</a></p>

<p align="center">
  <strong>Type Vietnamese naturally. Keep English intact when it matters.</strong><br>
  Smart bilingual typing · Flexible shorthand · Native TSF · Windows 10/11
</p>

<p align="center">
  <a href="https://github.com/hoanglinh221191/vietnamese-tsf-ime/releases/latest"><strong>Download the latest Neokey</strong></a>
</p>

Neokey is an open-source Vietnamese input method for Windows. It uses the
Windows Text Services Framework (TSF) and provides Telex, Simple Telex, and
VNI input methods in a native Windows IME. Its standout features are bilingual
typing and flexible shorthand: Neokey keeps English words, URLs, email
addresses, and code intact while you remain in Vietnamese mode, then lets one
shortcut expand into reusable text, a fill-in template, or a dynamic snippet.

## Vietnamese–English Typing Without Constant Mode Switching

While **Vietnamese** mode is active, Neokey uses a two-tier lexicon of more than
5,000 words to recognize English without getting in the way of Vietnamese tone
entry. One regression-tested example is:

> Type `github vietes` with Telex → get `github viết`

- **Balanced** (default) preserves common English words for everyday bilingual
  writing.
- **English First** protects a broader vocabulary for technical writing and
  English-heavy content.
- **Off** disables only the English-word recognition layer when you want
  Vietnamese conversion to take full priority.
- **Protect URL, email, and code** keeps tokens such as `cmd.exe`,
  `name@example.com`, `CamelCase`, `sha256`, and `base64` unchanged.

<p align="center">
  <img src="assets/neokey-config.png" alt="Neokey 0.1.10 interface with Vietnamese-English typing controls" width="650">
</p>

## Highlights

- Native Windows TSF input method for 64-bit and 32-bit desktop applications.
- Telex, Simple Telex, and VNI typing methods.
- Configurable correction levels: Off, Normal, Advanced, and Experimental.
- Vietnamese–English typing with a two-tier 5,118-word lexicon and three
  protection levels.
- Dynamic shorthand with date, time, UUID, clipboard, selection, and caret
  variables.
- Automatic password-field protection that bypasses Vietnamese conversion for
  sensitive input.
- Per-application blocklist with optional automatic exclusion while an app is
  in English mode, then automatic restoration on return to Vietnamese mode.
- A small configuration and tray application (`neokey_config.exe`).
- A Windows installer with install, in-place update, and repair modes.
- Portable release packages with a SHA-256 manifest for the shipped binaries.
- A separate native ARM64 package marked **Preview** for Windows on ARM testing.

## Flexible Shorthand: Static, Dynamic, And Context-Aware

Each rule uses `key=content`; type the key and press Space to expand it. In
addition to static text, Neokey can insert dynamic values, use copied or
selected content, and position the caret after expansion. The shorthand table
supports up to **4,096 rules** and picks up saved changes without requiring you
to restart the application where you are typing.

| Example rule | Result |
| --- | --- |
| `today=Today is {{DATE}}` | Inserts the current date |
| `hello=Dear {{CLIPBOARD}},` | Uses the clipboard contents |
| `wrap=[{{SELECTION}}]{{CURSOR}}` | Wraps selected text and restores the caret |
| `stamp={{DATE}} {{TIME}} {{WEEKDAY}} - {{UUID}}` | Creates a timestamp with a UUID |

Supported variables include `DATE`, `DD/MM/YYYY`, `TIME`, `WEEKDAY`, `UUID`,
`NEWLINE`, `TAB`, `CURSOR`, `CLIPBOARD`, and `SELECTION`. Clipboard values can
also use the `TRIM`, `UPPER`, and `LOWER` transformations. In supported
contexts, Backspace can undo a recent expansion back to its original shortcut.

<p align="center">
  <img src="assets/neokey-shorthand.png" alt="Neokey dynamic shorthand table with snippet variables and examples" width="674">
</p>

## Install

The recommended option is `NeokeySetup.exe` from the GitHub Releases page.

1. Double-click `NeokeySetup.exe`. Do not use **Run as administrator**; Setup
   requests elevation itself when needed.
2. Approve the Windows Administrator prompt and choose **Install**.
3. Setup adds Neokey to Windows, makes it the default input method, and opens
   the configuration application when installation finishes.

> [!IMPORTANT]
> After installing or updating, close and reopen every running application so
> it loads the new input method. Restart Windows to fully reload the text
> service, especially if Neokey is missing or an app still uses the old build.

To update, run `NeokeySetup.exe` from a newer release. Setup detects the
installed version, shows an **Update** action, and preserves settings and
shorthand data. Running the same version enters repair mode; downgrades are
blocked.

### Portable Release

Advanced users can download `Neokey-portable.zip`, extract it to a stable path
such as `C:\Neokey`, and run `install.bat`. Do not move that folder after
registration because Windows stores absolute DLL paths. The script verifies the
manifest and makes Neokey the default input method.

After running `install.bat`, close and reopen running applications, then
restart Windows to ensure they load Neokey from the new portable package.

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

Neokey is released under the [MIT License](LICENSE). Bundled bilingual lexicon
data is attributed separately in [third-party notices](THIRD_PARTY_NOTICES.md).
