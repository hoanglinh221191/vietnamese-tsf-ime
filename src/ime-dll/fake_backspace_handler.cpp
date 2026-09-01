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
    return EqualsIgnoreCase(filename, L"coreldrw.exe") ||
           EqualsIgnoreCase(filename, L"coreldraw.exe");
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
           IsCorelDrawProcess(focused_process);
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
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = vk;
    inputs[0].ki.dwExtraInfo = 0xDEADC0DE;
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = vk;
    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
    inputs[1].ki.dwExtraInfo = 0xDEADC0DE;

    UINT sent = ::SendInput(2, inputs, sizeof(INPUT));
    if (sent != 2) {
        logger::LogFormat(logger::Level::Warning, L"SendSyntheticNativeKey sent %u of 2 inputs", sent);
    }
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
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = 0;
    inputs[0].ki.wScan = ch;
    inputs[0].ki.dwFlags = KEYEVENTF_UNICODE;
    inputs[0].ki.dwExtraInfo = 0xDEADC0DE;
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = 0;
    inputs[1].ki.wScan = ch;
    inputs[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
    inputs[1].ki.dwExtraInfo = 0xDEADC0DE;

    UINT sent = ::SendInput(2, inputs, sizeof(INPUT));
    if (sent != 2) {
        logger::LogFormat(logger::Level::Warning, L"SendSyntheticUnicodeChar sent %u of 2 inputs", sent);
    }
}

bool ProcessFakeBackspaceChar(
    core::Engine& engine,
    wchar_t ch,
    size_t& direct_inline_length,
    HWND target_hwnd,
    bool is_direct_post,
    HostInputDispatch dispatch) {
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
        for (size_t i = 0; i < backspaces_to_send; ++i) {
            SendSyntheticNativeKey(VK_BACK, target_hwnd, is_direct_post);
        }
        for (wchar_t wch : new_chars) {
            SendSyntheticUnicodeChar(wch, target_hwnd, is_direct_post);
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
    HostInputDispatch dispatch) {
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
        for (size_t i = 0; i < backspaces_to_send; ++i) {
            SendSyntheticNativeKey(VK_BACK, target_hwnd, is_direct_post);
        }
        for (wchar_t wch : new_chars) {
            SendSyntheticUnicodeChar(wch, target_hwnd, is_direct_post);
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
