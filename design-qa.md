# Neokey native UI design QA

**Comparison target**

- Source visual truth: `design/reference-ui/neokey-ui-concept-neutral-gray-v2.png`
- User defect capture: `design/qa/issue-app-button-missing-top-border.png`
- Rendered implementation: `design/qa/implementation-main-dark.png`, `design/qa/implementation-shorthand-dark.png`, and `design/qa/implementation-direct-dark.png`
- Combined comparison: `design/qa/reference-vs-implementation-dark.png`
- Focused border comparison: `design/qa/app-button-border-before-after.png`
- State: Windows dark theme, Vietnamese UI, VNI active, current saved Alt+Z hotkey active.
- Source pixels: 1448 x 1086.
- Implementation pixels: main 626 x 846; shorthand 898 x 831; Direct Inline/Commit 834 x 591.
- Comparison pixels: 2444 x 993. The source was normalized to 1185 px wide; the implementation used the main window at 850 px high and the two secondary windows at 540 px wide. Lanczos resizing was used only for the combined review image.
- Viewport/density: native Windows.Graphics.Capture window bounds in logical pixels; no browser CSS viewport or device-scale factor applies.

**Full-view comparison evidence**

- The final main window preserves the approved neutral-gray hierarchy, official Neokey icon, six feature groups, blue checked method/hotkey state, and gray language/startup drop-downs. In dark mode the outer canvas is near-black, cards are gray, and controls are a lighter gray; the light-mode palette reverses that luminance order.
- The final window fits the available desktop without a vertical scrollbar. All persistent controls and footer actions are visible simultaneously.
- The shorthand window follows the approved two-column composition: light variable/result and guidance panels on the left, a plain dark rules editor on the right, and no decorative three-dot window mockup.
- The Direct Inline/Commit window uses a wide help region and editor; its examples no longer overlap or wrap into the process list.

**Focused region comparison evidence**

- Typography: Segoe UI Variable Text is used throughout; the three method labels use a dedicated 10 pt regular face, while compact UI copy remains 9 pt and section headings 10 pt semibold.
- Alignment: Mức/Chế độ and the startup/language labels use vertically centered static controls. The `?` button shares the combo row top and visually matched height.
- Colors: dark mode uses near-black `#111111` outside, gray `#2A2A2A` feature cards, and lighter `#383838` controls. Light mode reverses the hierarchy with `#FAFAFA`, `#EBEBEB`, and `#DADADA`. Active method/hotkey controls use the Windows-blue accent, normal drop-downs remain gray, and shorthand tags are blue on both the rules editor and the variable reference panel.
- Dynamic-tag accuracy: RichEdit text is read using internal character positions, so CR/LF normalization no longer shifts blue ranges on later lines. Only complete `{{...}}` tags are colored.
- Copy: the shorthand guide now explains that `|` adds `TRIM`, `UPPER`, or `LOWER` transforms.
- Image quality: the existing ICO logo is retained; no replacement logo, generated icon, inline SVG, emoji, or placeholder artwork is used.

**Comparison history**

1. Initial implementation findings:
   - [P2] The main window required scrolling on the available desktop and flashed during whole-dialog scroll repaint.
   - [P2] Transparent statics created dark/white strips over feature cards, and the last correction row sat too close to the card edge.
   - [P2] Shorthand tag highlighting drifted after line breaks because RichEdit selection offsets did not match CR/LF-expanded text.
   - [P2] The main controls and `?` button had inconsistent vertical metrics; the main window was wider than requested.
   - [P2] Shorthand and Direct help panels did not match the approved light neutral reference surfaces.
2. Fixes made:
   - Reduced the main dialog to 390 x 407 dialog units, used 9 pt base metrics, retained 10 pt method labels, and removed the normal need for scrolling.
   - Added clipped-child painting, single-pass viewport redraw, explicit card-aware static backgrounds, and neutral rounded surfaces.
   - Replaced Win32 `WM_GETTEXT`-based syntax offsets with `EM_GETTEXTLENGTHEX`/`EM_GETTEXTEX` internal RichEdit positions.
   - Centered the relevant labels vertically and matched the `?` button to the correction combo row.
   - Added light information surfaces and explicit pipe-transform guidance.
3. Post-fix visual evidence:
   - `design/qa/reference-vs-implementation-dark.png` shows the final no-scroll main window, accurately colored shorthand tags, light help panels, and expanded Direct editor.
4. Hover repaint follow-up:
   - [P2] The standard Windows hover painter still competed with the custom card background on the three in-card action buttons, producing an intermittent top-edge flash.
   - The first inset-border revision still allowed a themed Win32 paint pass to replace the top edge temporarily, which is visible in the user-provided close-up.
   - The shorthand, Direct, and per-app action controls are now true owner-drawn buttons. Their fill and one-pixel rounded border are painted from one region in the parent draw transaction, so the native themed painter can no longer replace one edge independently.
   - `WM_PRINTCLIENT` and background erasure are also handled by the shared segmented-control painter, preventing capture/repaint paths from reintroducing a native frame.
   - A real Windows pointer check directly over `Ứng dụng...` retained a continuous four-sided border in `design/qa/implementation-main-dark.png`; opening the per-app dialog through the same owner-drawn button also succeeded.
   - `design/qa/app-button-border-before-after.png` compares the reported missing top edge with the final owner-drawn hover state at the same feature card.
5. Palette hierarchy follow-up:
   - Dark mode now reads from outside inward as near-black canvas, gray feature cards, then lighter gray controls.
   - Light mode uses the inverse ordering: near-white canvas, light-gray feature cards, then darker gray controls.

**Findings**

- No actionable P0, P1, or P2 visual mismatch remains in the captured dark-theme states.

**Open Questions**

- Light-theme palettes compile through the same native drawing path, but the current host was in dark mode, so this QA run did not change the user's Windows theme solely to capture a light-state screenshot.

**Implementation Checklist**

- [x] Main window fits without normal scrolling.
- [x] Active method/hotkey states are blue; language and startup drop-downs remain neutral.
- [x] All three in-card action buttons retain a continuous rounded border while hovered or invoked.
- [x] Shorthand variable tags are highlighted accurately after every line break.
- [x] Pipe-transform guidance is visible.
- [x] Direct Inline/Commit examples and editor do not overlap.
- [x] Existing Neokey logo and version source remain unchanged.

**Follow-up Polish**

- [P3] A future light-theme screenshot can be added when the host is already using Windows light mode.
- [P3] The Direct dialog's native gray OK button could be made accent-blue if primary-action emphasis is desired consistently across all secondary dialogs.

final result: passed
