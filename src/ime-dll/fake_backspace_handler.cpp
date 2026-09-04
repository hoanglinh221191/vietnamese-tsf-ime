#include "fake_backspace_handler.hpp"
#include "logger.hpp"
#include <algorithm>

namespace vn_ime::fake_backspace {

namespace {

inline bool EqualsIgnoreCase(std::wstring_view a, std::wstring_view b) noexcept {
    if (a.length() != b.length()) {
        return false;
    }
    return _wcsnicmp(a.data(), b.data(), a.length()) == 0;
}

inline std::wstring_view ExtractFileName(std::wstring_view path) noexcept {
    size_t last_slash = path.find_last_of(L"\\/");
    if (last_slash != std::wstring_view::npos) {
        return path.substr(last_slash + 1);
    }
    return path;
}

inline constexpr ULONG_PTR kSyntheticMarker = 0xDEADC0DEu;

inline void FillKeyInputPair(INPUT& down, INPUT& up, WORD vk) noexcept {
    const WORD scan_code = static_cast<WORD>(::MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
    down = INPUT{};
    down.type = INPUT_KEYBOARD;
    down.ki.wVk = vk;
    down.ki.wScan = scan_code;
    down.ki.dwExtraInfo = kSyntheticMarker;
    up = down;
    up.ki.dwFlags = KEYEVENTF_KEYUP;
}

inline void FillCharInputPair(INPUT& down, INPUT& up, wchar_t ch) noexcept {
    down = INPUT{};
    down.type = INPUT_KEYBOARD;
    down.ki.wVk = 0;
    down.ki.wScan = static_cast<WORD>(ch);
    down.ki.dwFlags = KEYEVENTF_UNICODE;
    down.ki.dwExtraInfo = kSyntheticMarker;
    up = down;
    up.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
}

inline void SecureClearString(std::wstring& str) noexcept {
    if (!str.empty()) {
        SecureZeroMemory(str.data(), str.size() * sizeof(wchar_t));
        str.clear();
    }
}

} // namespace

bool IsCorelDrawProcess(std::wstring_view process_name) noexcept {
    if (process_name.empty()) {
        return false;
    }
    std::wstring_view filename = ExtractFileName(process_name);
    if (EqualsIgnoreCase(filename, L"coreldrw.exe") ||
        EqualsIgnoreCase(filename, L"coreldraw.exe")) {
        return true;
    }
    std::wstring lower;
    lower.reserve(filename.length());
    for (wchar_t c : filename) {
        lower.push_back(static_cast<wchar_t>(::towlower(c)));
    }
    return lower.find(L"coreldrw") != std::wstring::npos ||
           lower.find(L"coreldraw") != std::wstring::npos;
}

bool IsTerminalProcess(std::wstring_view process_name) noexcept {
    if (process_name.empty()) {
        return false;
    }
    std::wstring_view filename = ExtractFileName(process_name);
    return EqualsIgnoreCase(filename, L"windowsterminal.exe") ||
           EqualsIgnoreCase(filename, L"openconsole.exe") ||
           EqualsIgnoreCase(filename, L"powershell.exe") ||
           EqualsIgnoreCase(filename, L"pwsh.exe") ||
           EqualsIgnoreCase(filename, L"cmd.exe") ||
           EqualsIgnoreCase(filename, L"conhost.exe") ||
           EqualsIgnoreCase(filename, L"wt.exe") ||
           EqualsIgnoreCase(filename, L"wsl.exe") ||
           EqualsIgnoreCase(filename, L"wslhost.exe") ||
           EqualsIgnoreCase(filename, L"bash.exe") ||
           EqualsIgnoreCase(filename, L"ssh.exe") ||
           EqualsIgnoreCase(filename, L"anydesk.exe") ||
           EqualsIgnoreCase(filename, L"pymol.exe") ||
           EqualsIgnoreCase(filename, L"mintty.exe");
}

bool IsVisualStudioProcess(std::wstring_view process_name) noexcept {
    if (process_name.empty()) {
        return false;
    }
    std::wstring_view filename = ExtractFileName(process_name);
    return EqualsIgnoreCase(filename, L"devenv.exe");
}

bool IsConsoleProcess(std::wstring_view process_name) noexcept {
    if (process_name.empty()) {
        return false;
    }
    std::wstring_view filename = ExtractFileName(process_name);
    return EqualsIgnoreCase(filename, L"cmd.exe") ||
           EqualsIgnoreCase(filename, L"conhost.exe") ||
           EqualsIgnoreCase(filename, L"wt.exe") ||
           EqualsIgnoreCase(filename, L"windowsterminal.exe") ||
           EqualsIgnoreCase(filename, L"openconsole.exe") ||
           EqualsIgnoreCase(filename, L"powershell.exe") ||
           EqualsIgnoreCase(filename, L"pwsh.exe") ||
           EqualsIgnoreCase(filename, L"wsl.exe") ||
           EqualsIgnoreCase(filename, L"wslhost.exe") ||
           EqualsIgnoreCase(filename, L"bash.exe") ||
           EqualsIgnoreCase(filename, L"ssh.exe") ||
           EqualsIgnoreCase(filename, L"mintty.exe");
}

bool IsExcelProcess(std::wstring_view process_name) noexcept {
    if (process_name.empty()) {
        return false;
    }
    std::wstring_view filename = ExtractFileName(process_name);
    return EqualsIgnoreCase(filename, L"excel.exe");
}

bool IsOutlookProcess(std::wstring_view process_name) noexcept {
    if (process_name.empty()) {
        return false;
    }
    std::wstring_view filename = ExtractFileName(process_name);
    if (EqualsIgnoreCase(filename, L"olk.exe") ||
        EqualsIgnoreCase(filename, L"outlook.exe")) {
        return true;
    }
    if (EqualsIgnoreCase(filename, L"msedgewebview2.exe")) {
        const wchar_t* cmdline = ::GetCommandLineW();
        if (cmdline) {
            std::wstring_view cmd(cmdline);
            if (cmd.find(L"olk.exe") != std::wstring_view::npos ||
                cmd.find(L"\\Microsoft\\Olk\\") != std::wstring_view::npos ||
                cmd.find(L"Microsoft.OutlookForWindows") != std::wstring_view::npos) {
                return true;
            }
        }
    }
    return false;
}

bool IsFakeBackspaceTargetApp(
    std::wstring_view host_process,
    std::wstring_view focused_process) noexcept {
    return IsTerminalProcess(host_process) ||
           IsTerminalProcess(focused_process) ||
           IsVisualStudioProcess(host_process) ||
           IsVisualStudioProcess(focused_process) ||
           IsConsoleProcess(host_process) ||
           IsConsoleProcess(focused_process) ||
           IsCorelDrawProcess(host_process) ||
           IsCorelDrawProcess(focused_process) ||
           IsOutlookProcess(host_process) ||
           IsOutlookProcess(focused_process);
}

bool IsLibreOfficeProcess(std::wstring_view process_name) noexcept {
    if (process_name.empty()) {
        return false;
    }
    std::wstring_view filename = ExtractFileName(process_name);
    return EqualsIgnoreCase(filename, L"soffice.bin") ||
           EqualsIgnoreCase(filename, L"soffice.exe") ||
           EqualsIgnoreCase(filename, L"scalc.exe") ||
           EqualsIgnoreCase(filename, L"swriter.exe") ||
           EqualsIgnoreCase(filename, L"simpress.exe") ||
           EqualsIgnoreCase(filename, L"sdraw.exe") ||
           EqualsIgnoreCase(filename, L"sbase.exe") ||
           EqualsIgnoreCase(filename, L"smath.exe");
}

bool IsNativeEnterReplayTargetApp(
    std::wstring_view host_process,
    std::wstring_view focused_process) noexcept {
    if (IsExcelProcess(host_process) || IsExcelProcess(focused_process) ||
        IsLibreOfficeProcess(host_process) || IsLibreOfficeProcess(focused_process)) {
        return true;
    }
    auto is_target = [](std::wstring_view process) noexcept {
        if (process.empty()) {
            return false;
        }
        std::wstring_view file = ExtractFileName(process);
        if (file.empty()) {
            return false;
        }
        if (EqualsIgnoreCase(file, L"telegram.exe") ||
            EqualsIgnoreCase(file, L"viber.exe") ||
            EqualsIgnoreCase(file, L"notepad++.exe")) {
            return true;
        }
        std::wstring lower(file);
        for (wchar_t& c : lower) {
            c = static_cast<wchar_t>(::towlower(c));
        }
        return lower.find(L"chrome") != std::wstring::npos ||
               lower.find(L"edge") != std::wstring::npos ||
               lower.find(L"firefox") != std::wstring::npos ||
               lower.find(L"brave") != std::wstring::npos ||
               lower.find(L"opera") != std::wstring::npos ||
               lower.find(L"vivaldi") != std::wstring::npos;
    };

    return is_target(host_process) || is_target(focused_process);
}

void SendSyntheticNativeKey(
    WORD vk,
    HWND target_hwnd,
    bool is_direct_post) {
    if (is_direct_post && target_hwnd) {
        UINT scanCode = ::MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
        LPARAM downLParam = 1 | (scanCode << 16) | (1 << 28);
        LPARAM upLParam = 0xC0000001 | (scanCode << 16) | (1 << 28);
        ::PostMessageW(target_hwnd, WM_KEYDOWN, vk, downLParam);
        ::PostMessageW(target_hwnd, WM_KEYUP, vk, upLParam);
        return;
    }

    INPUT inputs[2]{};
    FillKeyInputPair(inputs[0], inputs[1], vk);

    UINT sent = ::SendInput(2, inputs, sizeof(INPUT));
    if (sent != 2) {
        logger::LogFormat(logger::Level::Warning, L"SendSyntheticNativeKey sent %u of 2 inputs", sent);
    }
}

size_t BuildSyntheticEditInputs(
    size_t backspace_count,
    std::wstring_view chars,
    INPUT* out,
    size_t capacity) noexcept {
    if (!out) {
        return 0;
    }
    const size_t required = (backspace_count + chars.size()) * 2;
    if (required == 0 || required > capacity) {
        return 0;
    }
    size_t index = 0;
    for (size_t i = 0; i < backspace_count; ++i) {
        FillKeyInputPair(out[index], out[index + 1], VK_BACK);
        index += 2;
    }
    for (wchar_t ch : chars) {
        FillCharInputPair(out[index], out[index + 1], ch);
        index += 2;
    }
    return index;
}

void SendSyntheticEditBatch(
    size_t backspace_count,
    std::wstring_view chars,
    HWND target_hwnd,
    bool is_direct_post) {
    if (backspace_count == 0 && chars.empty()) {
        return;
    }

    // The direct-post path addresses one window queue, which already preserves
    // ordering, so keep dispatching message by message there.
    if (is_direct_post && target_hwnd) {
        for (size_t i = 0; i < backspace_count; ++i) {
            SendSyntheticNativeKey(VK_BACK, target_hwnd, true);
        }
        for (wchar_t ch : chars) {
            SendSyntheticUnicodeChar(ch, target_hwnd, true);
        }
        return;
    }

    // One SendInput call is what makes the edit atomic. Windows never
    // intersperses the events of a single array with real keystrokes, so the
    // host cannot see a half-applied backspace run even while the user keeps
    // typing. Oversized edits fall back to chunking, which no inline word
    // reaches in practice.
    if ((backspace_count + chars.size()) * 2 > kMaxSyntheticEditInputs) {
        const size_t chunk = kMaxSyntheticEditInputs / 2;
        size_t remaining = backspace_count;
        while (remaining > 0) {
            const size_t take = (std::min)(remaining, chunk);
            SendSyntheticEditBatch(take, std::wstring_view{}, target_hwnd, false);
            remaining -= take;
        }
        size_t offset = 0;
        while (offset < chars.size()) {
            const size_t take = (std::min)(chars.size() - offset, chunk);
            SendSyntheticEditBatch(0, chars.substr(offset, take), target_hwnd, false);
            offset += take;
        }
        return;
    }

    INPUT inputs[kMaxSyntheticEditInputs]{};
    const size_t count = BuildSyntheticEditInputs(
        backspace_count, chars, inputs, kMaxSyntheticEditInputs);
    if (count == 0) {
        return;
    }
    const UINT sent = ::SendInput(
        static_cast<UINT>(count), inputs, sizeof(INPUT));
    if (sent != count) {
        logger::LogFormat(
            logger::Level::Warning,
            L"SendSyntheticEditBatch sent %u of %zu inputs (backspaces=%zu, chars=%zu)",
            sent, count, backspace_count, chars.size());
    }
    // Replacement text lives in wScan, so scrub the staging buffer.
    SecureZeroMemory(inputs, sizeof(inputs));
}

void SendSyntheticUnicodeChar(
    wchar_t ch,
    HWND target_hwnd,
    bool is_direct_post) {
    if (is_direct_post && target_hwnd) {
        SHORT vkState = ::VkKeyScanW(ch);
        UINT scanCode = 0;
        if (vkState != -1) {
            scanCode = ::MapVirtualKeyW(LOBYTE(vkState), MAPVK_VK_TO_VSC);
        }
        LPARAM charLParam = 1 | (scanCode << 16) | (1 << 28);
        ::PostMessageW(target_hwnd, WM_CHAR, ch, charLParam);
        return;
    }

    INPUT inputs[2]{};
    FillCharInputPair(inputs[0], inputs[1], ch);

    UINT sent = ::SendInput(2, inputs, sizeof(INPUT));
    if (sent != 2) {
        logger::LogFormat(logger::Level::Warning, L"SendSyntheticUnicodeChar sent %u of 2 inputs", sent);
    }
}

namespace {

// Mirrors exactly what ProcessFakeBackspaceChar computes, on a copy of the
// engine, so the caller can decide how to dispatch before any state moves.
struct PlannedEdit {
    size_t backspaces = 0;
    std::wstring new_chars;
    bool valid = false;
};

PlannedEdit PlanFakeBackspaceEdit(
    const core::Engine& engine,
    wchar_t ch,
    size_t direct_inline_length) {
    PlannedEdit plan;
    if (ch == 0) {
        return plan;
    }
    core::Engine probe = engine;  // two short strings; cheap enough per key
    std::wstring old_display = probe.GetDisplayString();
    const bool can_replace_previous = direct_inline_length > 0;
    if (!can_replace_previous) {
        probe.Clear();
        old_display.clear();
    }
    probe.ProcessKey(ch);
    const std::wstring display = probe.GetDisplayString();
    if (display.empty()) {
        return plan;
    }
    size_t common_len = 0;
    if (can_replace_previous) {
        const size_t max_len = (std::min)(old_display.length(), display.length());
        while (common_len < max_len &&
               old_display[common_len] == display[common_len]) {
            ++common_len;
        }
    }
    plan.backspaces = old_display.length() - common_len;
    plan.new_chars = display.substr(common_len);
    plan.valid = true;
    return plan;
}

} // namespace

PlannedEditShape PlanFakeBackspaceEditShape(
    const core::Engine& engine,
    wchar_t ch,
    size_t direct_inline_length) {
    PlannedEdit plan = PlanFakeBackspaceEdit(engine, ch, direct_inline_length);
    PlannedEditShape shape;
    shape.valid = plan.valid;
    shape.backspaces = plan.backspaces;
    shape.appended = plan.new_chars.length();
    // Clamped: a host/engine desync could otherwise underflow this size_t.
    const size_t kept = plan.backspaces >= direct_inline_length
        ? 0
        : direct_inline_length - plan.backspaces;
    shape.resulting_length = plan.valid ? kept + plan.new_chars.length() : 0;
    SecureClearString(plan.new_chars);
    return shape;
}

bool IsIdentityAppendEdit(
    const core::Engine& engine,
    wchar_t ch,
    size_t direct_inline_length) {
    const PlannedEdit plan =
        PlanFakeBackspaceEdit(engine, ch, direct_inline_length);
    return plan.valid && plan.backspaces == 0 &&
           plan.new_chars.length() == 1 && plan.new_chars[0] == ch;
}

bool ProcessFakeBackspaceChar(
    core::Engine& engine,
    wchar_t ch,
    size_t& direct_inline_length,
    HWND target_hwnd,
    bool is_direct_post,
    HostInputDispatch dispatch,
    EditDispatchObserver* observer,
    WORD identity_replay_virtual_key) {
    if (ch == 0) {
        return false;
    }

    std::wstring old_display = engine.GetDisplayString();
    const bool can_replace_previous = direct_inline_length > 0;

    if (!can_replace_previous) {
        engine.Clear();
        direct_inline_length = 0;
        SecureClearString(old_display);
    }

    engine.ProcessKey(ch);
    std::wstring display = engine.GetDisplayString();
    if (display.empty()) {
        SecureClearString(display);
        SecureClearString(old_display);
        return false;
    }

    size_t common_len = 0;
    if (can_replace_previous) {
        size_t max_len = (std::min)(old_display.length(), display.length());
        while (common_len < max_len && old_display[common_len] == display[common_len]) {
            common_len++;
        }
    }

    size_t backspaces_to_send = old_display.length() - common_len;
    std::wstring new_chars = display.substr(common_len);
    if (dispatch == HostInputDispatch::SendToHost) {
        const bool identity_append =
            identity_replay_virtual_key != 0 && backspaces_to_send == 0 &&
            new_chars.length() == 1 && new_chars[0] == ch;
        // Arm before dispatching: the host may route the injected keys back
        // into this service before the dispatch call even returns.
        if (identity_append) {
            if (observer) {
                observer->OnBeforeSyntheticNativeKey(
                    identity_replay_virtual_key);
            }
            SendSyntheticNativeKey(
                identity_replay_virtual_key, target_hwnd, is_direct_post);
        } else {
            const bool taken_over =
                observer && observer->OnBeforeSyntheticEdit(
                                backspaces_to_send, new_chars);
            if (!taken_over) {
                SendSyntheticEditBatch(
                    backspaces_to_send, new_chars, target_hwnd, is_direct_post);
            }
        }
    }

    direct_inline_length = display.length();
    SecureClearString(new_chars);
    SecureClearString(display);
    SecureClearString(old_display);
    return true;
}

bool ProcessFakeBackspaceBackspace(
    core::Engine& engine,
    size_t& direct_inline_length,
    HWND target_hwnd,
    bool is_direct_post,
    HostInputDispatch dispatch,
    EditDispatchObserver* observer) {
    if (direct_inline_length == 0) {
        return false;
    }

    std::wstring old_display = engine.GetDisplayString();

    engine.BackspaceDisplayChar();
    std::wstring raw = engine.GetRawString();
    std::wstring display = engine.GetDisplayString();

    size_t common_len = 0;
    size_t max_len = (std::min)(old_display.length(), display.length());
    while (common_len < max_len && old_display[common_len] == display[common_len]) {
        common_len++;
    }

    size_t backspaces_to_send = old_display.length() - common_len;
    std::wstring new_chars = display.substr(common_len);
    if (dispatch == HostInputDispatch::SendToHost) {
        // Arm before dispatching: the host may route the injected keys back
        // into this service before SendSyntheticEditBatch even returns.
        const bool taken_over =
            observer && observer->OnBeforeSyntheticEdit(
                            backspaces_to_send, new_chars);
        if (!taken_over) {
            SendSyntheticEditBatch(
                backspaces_to_send, new_chars, target_hwnd, is_direct_post);
        }
    }

    if (raw.empty() || display.empty()) {
        direct_inline_length = 0;
    } else {
        direct_inline_length = display.length();
    }

    SecureClearString(new_chars);
    SecureClearString(old_display);
    SecureClearString(raw);
    SecureClearString(display);
    return true;
}

} // namespace vn_ime::fake_backspace
