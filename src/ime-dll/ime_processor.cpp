#include "ime_processor.hpp"
#include "logger.hpp"
#include "rules.hpp"
#include "speller.hpp"
#include "commit_transform.hpp"
#include "config.hpp"
#include "key_translation.hpp"
#include "password_context_policy.hpp"
#include "shorthand_template.hpp"
#include <inputscope.h>
#include <textstor.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <string_view>
#include <utility>
#include <vector>
#include <thread>
#include <commctrl.h>
#include <richedit.h>
#pragma comment(lib, "comctl32.lib")

extern HINSTANCE g_hInst;

namespace vn_ime {

namespace {

static_assert(
    FUZZY_INPUT_FLAG_L_N ==
        core::ToFuzzyInputFlags(core::FuzzyInputFlag::LAndN) &&
    FUZZY_INPUT_FLAG_TR_CH ==
        core::ToFuzzyInputFlags(core::FuzzyInputFlag::TrAndCh) &&
    FUZZY_INPUT_FLAG_S_X ==
        core::ToFuzzyInputFlags(core::FuzzyInputFlag::SAndX) &&
    FUZZY_INPUT_FLAG_R_D_GI ==
        core::ToFuzzyInputFlags(core::FuzzyInputFlag::RAndDAndGi) &&
    FUZZY_INPUT_FLAG_HOI_NGA ==
        core::ToFuzzyInputFlags(core::FuzzyInputFlag::HookAndTilde));

ULONG AddComRef(std::atomic<ULONG>& ref_count) noexcept {
    return ref_count.fetch_add(1, std::memory_order_relaxed) + 1;
}

ULONG ReleaseComRef(std::atomic<ULONG>& ref_count) noexcept {
    return ref_count.fetch_sub(1, std::memory_order_acq_rel) - 1;
}

thread_local HHOOK g_msg_hook = nullptr;
thread_local HHOOK g_call_wnd_hook = nullptr;
thread_local HHOOK g_mouse_hook = nullptr;
thread_local VietnameseIME* g_ime_instance = nullptr;
thread_local bool g_in_hook = false;

void RemoveThreadCompositionHooks() noexcept {
    g_ime_instance = nullptr;
    const auto remove_hook = [](HHOOK& hook, const wchar_t* hook_name) {
        if (!hook) return;
        const HHOOK current = hook;
        if (::UnhookWindowsHookEx(current)) {
            hook = nullptr;
            return;
        }

        const DWORD error = ::GetLastError();
        if (error == ERROR_INVALID_HOOK_HANDLE) {
            hook = nullptr;
        }
        logger::LogFormat(
            logger::Level::Warning,
            L"Thread-local composition hook removal failed: hook=%s, error=%u, retained=%d",
            hook_name, error, hook ? 1 : 0);
    };
    remove_hook(g_msg_hook, L"get_message");
    remove_hook(g_call_wnd_hook, L"call_wnd_proc");
    remove_hook(g_mouse_hook, L"mouse");
}

struct TelegramResumeTimerRegistration {
    UINT_PTR timer_id = 0;
    VietnameseIME* owner = nullptr;
};

thread_local TelegramResumeTimerRegistration g_telegram_resume_timer;
thread_local TelegramResumeTimerRegistration g_telegram_raw_replay_timer;

inline constexpr UINT kTelegramResumeTimerDelayMs = 5;
inline constexpr UINT kTelegramSelectionRetryDelayMs = 8;
inline constexpr UINT kTelegramRawReplayDelayMs = 16;

struct EditSessionDispatchResult {
    HRESULT sync_request_hr = E_FAIL;
    HRESULT sync_session_hr = E_FAIL;
    HRESULT async_request_hr = E_FAIL;
    HRESULT async_session_hr = E_FAIL;
    bool retried_async = false;
    bool deferred = false;

    [[nodiscard]] bool Accepted() const noexcept {
        const HRESULT request_hr = retried_async
            ? async_request_hr
            : sync_request_hr;
        const HRESULT session_hr = retried_async
            ? async_session_hr
            : sync_session_hr;
        return SUCCEEDED(request_hr) && SUCCEEDED(session_hr);
    }
};

EditSessionDispatchResult RequestEditSessionWithWordAsyncFallback(
    ITfContext* context,
    TfClientId client_id,
    ITfEditSession* session,
    DWORD sync_flags,
    bool is_word_app) {
    EditSessionDispatchResult result;
    if (!context || !session) {
        return result;
    }

    result.sync_request_hr = context->RequestEditSession(
        client_id, session, sync_flags, &result.sync_session_hr);
    const WordEditSessionDispatch policy = DecideWordEditSessionDispatch(
        is_word_app,
        SUCCEEDED(result.sync_request_hr),
        SUCCEEDED(result.sync_session_hr),
        result.sync_session_hr == TS_E_SYNCHRONOUS);
    if (policy != WordEditSessionDispatch::RetryAsync) {
        return result;
    }

    result.retried_async = true;
    const DWORD async_flags =
        (sync_flags & ~TF_ES_SYNC) | TF_ES_ASYNC;
    result.async_request_hr = context->RequestEditSession(
        client_id, session, async_flags, &result.async_session_hr);
    result.deferred = IsAcceptedWordAsyncEditSession(
        SUCCEEDED(result.async_request_hr),
        SUCCEEDED(result.async_session_hr)) &&
        result.async_session_hr == TF_S_ASYNC;
    return result;
}

void SecureEraseTelegramRawReplayPlan(
    std::vector<TelegramRawReplayKey>& plan) noexcept {
    if (!plan.empty()) {
        SecureZeroMemory(
            plan.data(), plan.size() * sizeof(TelegramRawReplayKey));
        plan.clear();
    }
}

struct HookGuard {
    HookGuard() { g_in_hook = true; }
    ~HookGuard() { g_in_hook = false; }
};

LRESULT CALLBACK MouseHookProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0 && g_ime_instance && !g_in_hook) {
        HookGuard guard;
        UINT uMsg = static_cast<UINT>(wParam);

        if (uMsg == WM_LBUTTONDOWN || uMsg == WM_RBUTTONDOWN || uMsg == WM_MBUTTONDOWN ||
            uMsg == 0x0246 || // WM_POINTERDOWN
            uMsg == WM_NCLBUTTONDOWN || uMsg == WM_NCRBUTTONDOWN) {
            logger::LogFormat(logger::Level::Info, L"MouseHookProc: Mouse message %u observed", uMsg);
            g_ime_instance->CommitActiveCompositionFromHook();
        }
        return CallNextHookEx(nullptr, nCode, wParam, lParam);
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

LRESULT CALLBACK CallWndProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0 && g_ime_instance && !g_in_hook) {
        HookGuard guard;
        CWPSTRUCT* msg = reinterpret_cast<CWPSTRUCT*>(lParam);
        UINT uMsg = msg->message;
        if (uMsg == WM_LBUTTONDOWN || uMsg == WM_RBUTTONDOWN || uMsg == WM_MBUTTONDOWN ||
            uMsg == 0x0246 || // WM_POINTERDOWN
            uMsg == WM_NCLBUTTONDOWN || uMsg == WM_NCRBUTTONDOWN ||
            uMsg == WM_LBUTTONUP || uMsg == 0x0247 || uMsg == WM_NCLBUTTONUP ||
            uMsg == WM_KILLFOCUS || uMsg == WM_MOUSEACTIVATE) {
            logger::LogFormat(logger::Level::Info, L"CallWndProc: Message %u observed", uMsg);
            g_ime_instance->CommitActiveCompositionFromHook();
        }
        return CallNextHookEx(nullptr, nCode, wParam, lParam);
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

LRESULT CALLBACK GetMessageHookProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0 && g_ime_instance && !g_in_hook) {
        HookGuard guard;
        MSG* msg = reinterpret_cast<MSG*>(lParam);
        if (msg) {
            const UINT uMsg = msg->message;
            if (uMsg == WM_LBUTTONDOWN || uMsg == WM_RBUTTONDOWN ||
                uMsg == WM_MBUTTONDOWN || uMsg == 0x0246 ||
                uMsg == WM_NCLBUTTONDOWN || uMsg == WM_NCRBUTTONDOWN ||
                uMsg == WM_LBUTTONUP || uMsg == 0x0247 ||
                uMsg == WM_NCLBUTTONUP) {
                logger::LogFormat(
                    logger::Level::Info,
                    L"GetMessageHookProc: Message %u observed", uMsg);
                g_ime_instance->CommitActiveCompositionFromHook();
            }
        }
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

LRESULT CALLBACK InkscapeSubclassProc(
    HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
    [[maybe_unused]] UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    auto* ime = reinterpret_cast<VietnameseIME*>(dwRefData);
    if (uMsg == WM_KEYDOWN || uMsg == WM_KEYUP || uMsg == WM_CHAR) {
        if (ime && ime->IsInkscapeKeySuppressed(wParam)) {
            return 0;
        }
    }
    return ::DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

void SecureEraseString(std::wstring& value) {
    if (!value.empty()) {
        SecureZeroMemory(value.data(), value.size() * sizeof(wchar_t));
        value.clear();
    }
}

class SensitiveWString final {
public:
    SensitiveWString() = default;
    SensitiveWString(const SensitiveWString&) = delete;
    SensitiveWString& operator=(const SensitiveWString&) = delete;

    ~SensitiveWString() {
        SecureEraseString(value);
    }

    std::wstring value;
};

class ClipboardReadScope final {
public:
    ClipboardReadScope() noexcept
        : opened_(::OpenClipboard(nullptr) != FALSE) {}

    ClipboardReadScope(const ClipboardReadScope&) = delete;
    ClipboardReadScope& operator=(const ClipboardReadScope&) = delete;

    ~ClipboardReadScope() {
        if (opened_) {
            ::CloseClipboard();
        }
    }

    [[nodiscard]] bool IsOpen() const noexcept {
        return opened_;
    }

private:
    bool opened_ = false;
};

class ClipboardGlobalLockScope final {
public:
    explicit ClipboardGlobalLockScope(HGLOBAL handle) noexcept
        : handle_(handle), data_(::GlobalLock(handle)) {}

    ClipboardGlobalLockScope(const ClipboardGlobalLockScope&) = delete;
    ClipboardGlobalLockScope& operator=(
        const ClipboardGlobalLockScope&) = delete;

    ~ClipboardGlobalLockScope() {
        if (data_) {
            ::GlobalUnlock(handle_);
        }
    }

    [[nodiscard]] const wchar_t* Data() const noexcept {
        return static_cast<const wchar_t*>(data_);
    }

private:
    HGLOBAL handle_ = nullptr;
    void* data_ = nullptr;
};

bool ReadUnicodeClipboardTextBounded(
    size_t max_chars, std::wstring& destination) {
    SecureEraseString(destination);

    ClipboardReadScope clipboard;
    if (!clipboard.IsOpen()) {
        return false;
    }

    const HANDLE clipboard_data = ::GetClipboardData(CF_UNICODETEXT);
    if (!clipboard_data) {
        return false;
    }

    const HGLOBAL global_data = static_cast<HGLOBAL>(clipboard_data);
    const SIZE_T byte_size = ::GlobalSize(global_data);
    if (byte_size < sizeof(wchar_t) ||
        byte_size % sizeof(wchar_t) != 0) {
        return false;
    }

    ClipboardGlobalLockScope locked(global_data);
    const wchar_t* const text = locked.Data();
    if (!text) {
        return false;
    }

    const size_t available_chars =
        static_cast<size_t>(byte_size / sizeof(wchar_t));
    const size_t scan_limit = available_chars <= max_chars
        ? available_chars
        : max_chars + 1;
    size_t length = 0;
    while (length < scan_limit && text[length] != L'\0') {
        ++length;
    }
    if (length == 0 || length == scan_limit || length > max_chars) {
        return false;
    }

    destination.assign(text, length);
    return true;
}

bool GenerateShorthandUuid(std::wstring& destination) {
    SecureEraseString(destination);
    GUID guid{};
    if (FAILED(::CoCreateGuid(&guid))) {
        return false;
    }

    wchar_t buffer[39] = {};
    const int written = ::StringFromGUID2(
        guid, buffer, static_cast<int>(std::size(buffer)));
    if (written != static_cast<int>(std::size(buffer)) ||
        buffer[0] != L'{' || buffer[37] != L'}') {
        SecureZeroMemory(buffer, sizeof(buffer));
        return false;
    }

    destination.assign(buffer + 1, 36);
    for (wchar_t& ch : destination) {
        ch = core::rules::ToLower(ch);
    }
    SecureZeroMemory(buffer, sizeof(buffer));
    return true;
}

bool MapShorthandTextCaseBounded(
    std::wstring_view source, DWORD map_flag, size_t max_chars,
    std::wstring& destination) {
    SecureEraseString(destination);
    if (source.empty()) {
        return true;
    }
    if (source.length() >
        static_cast<size_t>((std::numeric_limits<int>::max)())) {
        return false;
    }

    const int source_length = static_cast<int>(source.length());
    const int required = ::LCMapStringEx(
        LOCALE_NAME_INVARIANT, map_flag,
        source.data(), source_length, nullptr, 0,
        nullptr, nullptr, 0);
    if (required <= 0 || static_cast<size_t>(required) > max_chars) {
        return false;
    }

    destination.resize(static_cast<size_t>(required));
    const int mapped = ::LCMapStringEx(
        LOCALE_NAME_INVARIANT, map_flag,
        source.data(), source_length, destination.data(), required,
        nullptr, nullptr, 0);
    if (mapped != required) {
        SecureEraseString(destination);
        return false;
    }
    return true;
}

void SecureEraseStringUtf8(std::string& value) {
    if (!value.empty()) {
        SecureZeroMemory(value.data(), value.size() * sizeof(char));
        value.clear();
    }
}

bool ConvertWideToUtf8(const std::wstring& source, std::string& destination) {
    SecureEraseStringUtf8(destination);
    if (source.empty() || source.length() > static_cast<size_t>((std::numeric_limits<int>::max)())) {
        return source.empty();
    }

    const int source_length = static_cast<int>(source.length());
    const int required_length = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, source.data(), source_length,
        nullptr, 0, nullptr, nullptr);
    if (required_length <= 0) {
        return false;
    }

    destination.resize(static_cast<size_t>(required_length));
    const int converted_length = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, source.data(), source_length,
        destination.data(), required_length, nullptr, nullptr);
    if (converted_length != required_length) {
        SecureEraseStringUtf8(destination);
        return false;
    }
    return true;
}

bool ConvertUtf8ToWideBounded(
    std::string_view source, size_t max_chars, std::wstring& destination) {
    SecureEraseString(destination);
    if (source.empty() ||
        source.length() >
            static_cast<size_t>((std::numeric_limits<int>::max)())) {
        return false;
    }
    const int source_length = static_cast<int>(source.length());
    const int required = ::MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS,
        source.data(), source_length, nullptr, 0);
    if (required <= 0 || static_cast<size_t>(required) > max_chars) {
        return false;
    }
    destination.resize(static_cast<size_t>(required));
    const int converted = ::MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS,
        source.data(), source_length, destination.data(), required);
    if (converted != required) {
        SecureEraseString(destination);
        return false;
    }
    return true;
}

bool IsSameComObject(IUnknown* left, IUnknown* right) noexcept {
    if (!left || !right) {
        return false;
    }

    ComPtr<IUnknown> left_identity;
    ComPtr<IUnknown> right_identity;
    if (FAILED(left->QueryInterface(IID_IUnknown,
                                     reinterpret_cast<void**>(left_identity.GetAddressOf()))) ||
        FAILED(right->QueryInterface(IID_IUnknown,
                                      reinterpret_cast<void**>(right_identity.GetAddressOf())))) {
        return false;
    }
    return left_identity.Get() == right_identity.Get();
}

class SelectionUpdateScope {
public:
    explicit SelectionUpdateScope(bool& flag) noexcept
        : flag_(flag), previous_(flag) {
        flag_ = true;
    }

    ~SelectionUpdateScope() noexcept {
        flag_ = previous_;
    }

    SelectionUpdateScope(const SelectionUpdateScope&) = delete;
    SelectionUpdateScope& operator=(const SelectionUpdateScope&) = delete;

private:
    bool& flag_;
    bool previous_;
};

template <typename T>
void SecureEraseVector(std::vector<T>& value) {
    if (!value.empty()) {
        SecureZeroMemory(value.data(), value.size() * sizeof(T));
        value.clear();
    }
}

bool TryGetVerifiedTsfTextImmediatelyBeforeCaret(
    TfEditCookie ec,
    ITfRange* caret_range,
    std::wstring_view expected_display,
    ComPtr<ITfRange>& verified_range) {
    verified_range.Reset();
    if (!caret_range || expected_display.empty() ||
        expected_display.length() > kMaxCommitUndoDisplayChars ||
        expected_display.length() >
            static_cast<size_t>((std::numeric_limits<LONG>::max)())) {
        return false;
    }

    BOOL is_empty = FALSE;
    if (FAILED(caret_range->IsEmpty(ec, &is_empty)) || !is_empty) {
        return false;
    }

    if (FAILED(caret_range->Clone(verified_range.GetAddressOf())) ||
        !verified_range) {
        return false;
    }
    verified_range->Collapse(ec, TF_ANCHOR_END);

    const LONG expected_shift =
        -static_cast<LONG>(expected_display.length());
    LONG shifted = 0;
    if (FAILED(verified_range->ShiftStart(
            ec, expected_shift, &shifted, nullptr)) ||
        shifted != expected_shift) {
        return false;
    }

    std::vector<wchar_t> current_text(
        expected_display.length() + 1, L'\0');
    ULONG fetched = 0;
    const HRESULT hr = verified_range->GetText(
        ec, 0, current_text.data(),
        static_cast<ULONG>(expected_display.length()), &fetched);
    bool matches = false;
    if (SUCCEEDED(hr) && fetched == expected_display.length()) {
        const std::wstring_view current_view(
            current_text.data(), static_cast<size_t>(fetched));
        const auto verified = FindVerifiedTextBeforeCaret(
            current_view, current_view.length(), expected_display);
        matches = verified && verified->start == 0 &&
            verified->end == current_view.length();
    }
    SecureEraseVector(current_text);
    if (!matches) {
        verified_range.Reset();
    }
    return matches;
}

bool VerifyTsfTextImmediatelyBeforeCaret(
    TfEditCookie ec,
    ITfRange* caret_range,
    std::wstring_view expected_display) {
    ComPtr<ITfRange> verified_range;
    return TryGetVerifiedTsfTextImmediatelyBeforeCaret(
        ec, caret_range, expected_display, verified_range);
}

bool ReadBoundedTsfTextBeforeCaret(
    TfEditCookie ec,
    ITfRange* caret_range,
    size_t max_chars,
    std::wstring& text,
    bool& truncated_left) {
    SecureEraseString(text);
    truncated_left = false;
    if (!caret_range || max_chars == 0 ||
        max_chars > kMaxCommitUndoDisplayChars ||
        max_chars > static_cast<size_t>((std::numeric_limits<LONG>::max)())) {
        return false;
    }

    ComPtr<ITfRange> read_range;
    ComPtr<ITfContext> context;
    if (SUCCEEDED(caret_range->GetContext(context.GetAddressOf())) && context) {
        ComPtr<ITfRange> doc_range;
        if (SUCCEEDED(context->GetStart(ec, doc_range.GetAddressOf())) && doc_range) {
            if (SUCCEEDED(doc_range->ShiftEndToRange(ec, caret_range, TF_ANCHOR_END)) &&
                SUCCEEDED(doc_range->Collapse(ec, TF_ANCHOR_END))) {
                read_range = std::move(doc_range);
            }
        }
    }
    if (!read_range) {
        if (FAILED(caret_range->Clone(read_range.GetAddressOf())) || !read_range ||
            FAILED(read_range->Collapse(ec, TF_ANCHOR_END))) {
            return false;
        }
    }

    LONG shifted = 0;
    const LONG requested = -static_cast<LONG>(max_chars);
    if (FAILED(read_range->ShiftStart(
            ec, requested, &shifted, nullptr)) || shifted > 0) {
        return false;
    }
    const size_t available = static_cast<size_t>(-shifted);
    truncated_left = shifted == requested;
    if (available == 0) {
        return false;
    }

    std::vector<wchar_t> buffer(available + 1, L'\0');
    ULONG fetched = 0;
    const HRESULT hr = read_range->GetText(
        ec, 0, buffer.data(), static_cast<ULONG>(available), &fetched);
    const bool success = SUCCEEDED(hr) && fetched > 0;
    if (success) {
        text.assign(buffer.data(), fetched);
    }
    SecureEraseVector(buffer);
    return success;
}

std::wstring ReadImmediatePreviousTokenFromTsf(
    TfEditCookie ec,
    ITfRange* caret_range,
    std::wstring_view current_token) {
    std::wstring suffix;
    bool truncated_left = false;
    constexpr size_t kMaxFuzzyPairChars =
        2 * core::kMaxFuzzyInputTokenLength + 1;
    if (!ReadBoundedTsfTextBeforeCaret(
            ec, caret_range, kMaxFuzzyPairChars, suffix,
            truncated_left)) {
        return {};
    }

    const auto previous = core::ExtractImmediatePreviousToken(
        suffix, current_token, truncated_left);
    std::wstring result = previous
        ? std::wstring(*previous)
        : std::wstring{};
    SecureEraseString(suffix);
    return result;
}

void SecureEraseBuffer(wchar_t* buffer, size_t count) {
    if (buffer && count > 0) {
        SecureZeroMemory(buffer, count * sizeof(wchar_t));
    }
}

bool IsLowerChar(wchar_t c) {
    return c >= L'a' && c <= L'z';
}

bool IsUpperChar(wchar_t c) {
    return c >= L'A' && c <= L'Z';
}

bool IsValidCompositionChar(wchar_t ch, core::InputMethod method) {
    if (method == core::InputMethod::Telex || method == core::InputMethod::SimpleTelex) {
        return (ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z');
    } else if (method == core::InputMethod::VNI) {
        return (ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') || (ch >= L'0' && ch <= L'9');
    }
    return false;
}

bool IsReconversionKey(wchar_t ch, core::InputMethod method) {
    if (core::rules::IsToneKey(ch, method)) return true;
    wchar_t lch = core::rules::ToLower(ch);
    if (method == core::InputMethod::Telex || method == core::InputMethod::SimpleTelex) {
        return (lch == L'w');
    } else if (method == core::InputMethod::VNI) {
        return (lch == L'6' || lch == L'7' || lch == L'8' || lch == L'9');
    }
    return false;
}

bool IsAutoCapWhitespace(wchar_t ch) noexcept {
    return ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L'\n';
}

bool IsSentenceEndPunctuation(wchar_t ch) noexcept {
    return ch == L'.' || ch == L'?' || ch == L'!';
}

bool IsPasswordInputScope(InputScope scope) noexcept {
    return scope == IS_PASSWORD ||
           scope == IS_NUMERIC_PASSWORD ||
           scope == IS_NUMERIC_PIN ||
           scope == IS_ALPHANUMERIC_PIN ||
           scope == IS_ALPHANUMERIC_PIN_SET;
}

bool IsEnterReplayInputScope(InputScope scope) noexcept {
    return scope == IS_SEARCH ||
           scope == IS_SEARCH_INCREMENTAL ||
           scope == IS_URL ||
           scope == IS_FILE_FULLFILEPATH ||
           scope == IS_FILE_FILENAME;
}

bool GetForegroundGuiThreadInfo(GUITHREADINFO* info) noexcept {
    if (!info) return false;
    HWND foreground = ::GetForegroundWindow();
    if (!foreground) return false;

    DWORD thread_id = ::GetWindowThreadProcessId(foreground, nullptr);
    if (thread_id == 0) return false;

    info->cbSize = sizeof(*info);
    return ::GetGUIThreadInfo(thread_id, info) != FALSE;
}

HWND GetBestFocusWindow() noexcept {
    HWND hwnd = ::GetFocus();
    if (hwnd) return hwnd;

    GUITHREADINFO info{};
    if (GetForegroundGuiThreadInfo(&info)) {
        if (info.hwndFocus) return info.hwndFocus;
        if (info.hwndCaret) return info.hwndCaret;
        if (info.hwndActive) return info.hwndActive;
    }

    return ::GetForegroundWindow();
}

std::wstring GetClassNameOrEmpty(HWND hwnd) {
    if (!hwnd) return L"";
    wchar_t class_name[128] = {0};
    if (::GetClassNameW(hwnd, class_name, static_cast<int>(std::size(class_name))) == 0) {
        return L"";
    }
    return class_name;
}

bool ClassNameEquals(HWND hwnd, const wchar_t* expected) {
    if (!hwnd || !expected) return false;
    std::wstring class_name = GetClassNameOrEmpty(hwnd);
    return _wcsicmp(class_name.c_str(), expected) == 0;
}

bool WindowOrAncestorHasClass(HWND hwnd, const wchar_t* expected) {
    for (HWND current = hwnd; current != nullptr; current = ::GetParent(current)) {
        if (ClassNameEquals(current, expected)) {
            return true;
        }
    }
    return false;
}

bool IsExplorerNativeSurfaceWindow(HWND hwnd) {
    if (!hwnd) return false;

    // The file list lives below SHELLDLL_DefView. Keep it completely native so
    // Explorer's type-to-select handles keys like "d" without a TSF composition.
    if (WindowOrAncestorHasClass(hwnd, L"SHELLDLL_DefView")) {
        return true;
    }

    if (ClassNameEquals(hwnd, L"SysListView32") ||
        ClassNameEquals(hwnd, L"UIItemsView") ||
        ClassNameEquals(hwnd, L"SysTreeView32") ||
        ClassNameEquals(hwnd, L"NamespaceTreeControl")) {
        return true;
    }

    return false;
}

bool IsSingleLineWin32EditWindow(HWND hwnd) {
    if (!ClassNameEquals(hwnd, L"Edit")) {
        return false;
    }

    LONG_PTR style = ::GetWindowLongPtrW(hwnd, GWL_STYLE);
    if ((style & ES_MULTILINE) != 0 || (style & ES_PASSWORD) != 0) {
        return false;
    }

    DWORD_PTR password_char = 0;
    if (::SendMessageTimeoutW(hwnd,
                              EM_GETPASSWORDCHAR,
                              0,
                              0,
                              SMTO_ABORTIFHUNG | SMTO_BLOCK,
                              50,
                              &password_char) == 0) {
        return false;
    }

    return password_char == 0;
}

bool IsKeyDown(int vk) noexcept {
    return (::GetKeyState(vk) & 0x8000) != 0;
}

bool HasAltOrWinModifier() noexcept {
    return IsKeyDown(VK_MENU) || IsKeyDown(VK_LWIN) || IsKeyDown(VK_RWIN);
}

bool HasTextShortcutModifier() noexcept {
    return IsKeyDown(VK_CONTROL) || HasAltOrWinModifier();
}

HotkeyKey ClassifyHotkeyKey(WPARAM wParam) noexcept {
    if (wParam == VK_CONTROL || wParam == VK_LCONTROL ||
        wParam == VK_RCONTROL) {
        return HotkeyKey::Control;
    }
    if (wParam == VK_SHIFT || wParam == VK_LSHIFT ||
        wParam == VK_RSHIFT) {
        return HotkeyKey::Shift;
    }
    return wParam == 'Z' ? HotkeyKey::Z : HotkeyKey::Other;
}

HotkeyModifiers ReadHotkeyModifiers() noexcept {
    return {
        IsKeyDown(VK_MENU),
        IsKeyDown(VK_CONTROL),
        IsKeyDown(VK_SHIFT),
    };
}

bool IsHorizontalCaretNavigationKey(WPARAM wParam) noexcept {
    if (wParam != VK_LEFT && wParam != VK_RIGHT) {
        return false;
    }
    return !HasAltOrWinModifier();
}

bool IsCaretNavigationKey(WPARAM wParam) noexcept {
    if (wParam == VK_LEFT || wParam == VK_RIGHT || wParam == VK_UP || wParam == VK_DOWN ||
        wParam == VK_HOME || wParam == VK_END || wParam == VK_PRIOR || wParam == VK_NEXT) {
        return !HasAltOrWinModifier();
    }
    return false;
}


HWND GetContextViewWindow(ITfContext* pic) {
    if (!pic) return nullptr;

    ComPtr<ITfContextView> view;
    if (FAILED(pic->GetActiveView(view.GetAddressOf())) || !view) {
        return nullptr;
    }

    HWND hwnd = nullptr;
    if (FAILED(view->GetWnd(&hwnd))) {
        return nullptr;
    }
    return hwnd;
}

const wchar_t* ExplorerFocusKindName(int kind) noexcept {
    switch (kind) {
        case 0: return L"NotExplorer";
        case 1: return L"NativeSurface";
        case 2: return L"Win32Edit";
        case 3: return L"TsfTextInput";
        default: return L"Unknown";
    }
}


bool HasSentenceBoundaryBeforeCaret(std::wstring_view preceding_text) {
    if (preceding_text.empty() || !IsAutoCapWhitespace(preceding_text.back())) {
        return false;
    }

    size_t idx = preceding_text.find_last_not_of(L" \t\r\n");
    if (idx == std::wstring_view::npos) {
        return false;
    }

    return IsSentenceEndPunctuation(preceding_text[idx]);
}

bool ShouldAutoCapitalizeAtRange(TfEditCookie ec, ITfRange* range) {
    if (!range) return false;

    ComPtr<ITfRange> context_range;
    if (FAILED(range->Clone(context_range.GetAddressOf())) || !context_range) {
        return false;
    }

    context_range->Collapse(ec, TF_ANCHOR_START);
    LONG shifted = 0;
    context_range->ShiftStart(ec, -20, &shifted, nullptr);
    if (shifted == 0) {
        return false;
    }

    wchar_t buf[32] = {0};
    ULONG fetched = 0;
    if (FAILED(context_range->GetText(ec, 0, buf, 31, &fetched)) || fetched == 0) {
        SecureEraseBuffer(buf, 32);
        return false;
    }

    std::wstring_view preceding_text(buf, fetched);
    const bool result = HasSentenceBoundaryBeforeCaret(preceding_text);
    SecureEraseBuffer(buf, 32);
    return result;
}

bool ShouldAutoCapitalizeAtFocusedControl() {
    HWND hwnd = GetBestFocusWindow();
    if (!hwnd) return false;

    wchar_t class_name[64] = {0};
    if (::GetClassNameW(hwnd, class_name, 64) == 0) {
        return false;
    }

    // Notepad++ edits text through Scintilla. Some Scintilla/TSF paths do not
    // expose preceding text reliably via ITfRange, so read the byte before caret
    // through Scintilla's native messages. This only checks ASCII punctuation.
    if (_wcsicmp(class_name, L"Scintilla") == 0) {
        constexpr UINT SCI_GETCHARAT = 2007;
        constexpr UINT SCI_GETCURRENTPOS = 2008;

        LRESULT pos = ::SendMessageW(hwnd, SCI_GETCURRENTPOS, 0, 0);
        if (pos <= 0) return false;

        bool saw_trailing_space = false;
        for (LRESULT i = pos - 1; i >= 0 && i >= pos - 32; --i) {
            LRESULT ch = ::SendMessageW(hwnd, SCI_GETCHARAT, static_cast<WPARAM>(i), 0);
            if (ch == 0) break;
            if (IsAutoCapWhitespace(static_cast<wchar_t>(ch))) {
                saw_trailing_space = true;
                continue;
            }
            return saw_trailing_space && IsSentenceEndPunctuation(static_cast<wchar_t>(ch));
        }
    }

    return false;
}

} // namespace


class VietnameseDisplayAttributeInfo : public ITfDisplayAttributeInfo {
public:
    VietnameseDisplayAttributeInfo() noexcept : ref_count_(1) {
        ClassFactory::IncrementActiveObjects();
    }
    virtual ~VietnameseDisplayAttributeInfo() noexcept {
        ClassFactory::DecrementActiveObjects();
    }

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (riid == IID_IUnknown || riid == IID_ITfDisplayAttributeInfo) {
            *ppv = static_cast<ITfDisplayAttributeInfo*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    STDMETHODIMP_(ULONG) AddRef() override { return AddComRef(ref_count_); }
    STDMETHODIMP_(ULONG) Release() override {
        const ULONG count = ReleaseComRef(ref_count_);
        if (count == 0) delete this;
        return count;
    }

    STDMETHODIMP GetGUID(GUID* pguid) override {
        if (!pguid) return E_INVALIDARG;
        *pguid = GUID_VietnameseDisplayAttribute;
        return S_OK;
    }

    STDMETHODIMP GetDescription(BSTR* pbstrDesc) override {
        if (!pbstrDesc) return E_INVALIDARG;
        *pbstrDesc = SysAllocString(L"Vietnamese IME Display Attribute");
        return *pbstrDesc ? S_OK : E_OUTOFMEMORY;
    }

    STDMETHODIMP GetAttributeInfo(TF_DISPLAYATTRIBUTE* pda) override {
        if (!pda) return E_INVALIDARG;
        pda->crText.type = TF_CT_NONE;
        pda->crBk.type = TF_CT_NONE;
        pda->lsStyle = TF_LS_DOT;
        pda->fBoldLine = FALSE;
        pda->crLine.type = TF_CT_NONE;
        pda->bAttr = TF_ATTR_INPUT;
        return S_OK;
    }

    STDMETHODIMP SetAttributeInfo(const TF_DISPLAYATTRIBUTE*) override { return E_NOTIMPL; }
    STDMETHODIMP Reset() override { return S_OK; }

private:
    std::atomic<ULONG> ref_count_;
};

class VietnameseEnumDisplayAttributeInfo : public IEnumTfDisplayAttributeInfo {
public:
    VietnameseEnumDisplayAttributeInfo() noexcept : ref_count_(1) {
        ClassFactory::IncrementActiveObjects();
    }
    virtual ~VietnameseEnumDisplayAttributeInfo() noexcept {
        ClassFactory::DecrementActiveObjects();
    }

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (riid == IID_IUnknown || riid == IID_IEnumTfDisplayAttributeInfo) {
            *ppv = static_cast<IEnumTfDisplayAttributeInfo*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    STDMETHODIMP_(ULONG) AddRef() override { return AddComRef(ref_count_); }
    STDMETHODIMP_(ULONG) Release() override {
        const ULONG count = ReleaseComRef(ref_count_);
        if (count == 0) delete this;
        return count;
    }

    STDMETHODIMP Clone(IEnumTfDisplayAttributeInfo** ppEnum) override {
        if (!ppEnum) return E_INVALIDARG;
        *ppEnum = new (std::nothrow) VietnameseEnumDisplayAttributeInfo();
        return *ppEnum ? S_OK : E_OUTOFMEMORY;
    }

    STDMETHODIMP Next(ULONG celt, ITfDisplayAttributeInfo** rgelt, ULONG* pceltFetched) override {
        if (!rgelt || (!pceltFetched && celt != 1)) return E_INVALIDARG;
        if (pceltFetched) *pceltFetched = 0;
        
        if (celt > 0 && index_ == 0) {
            rgelt[0] = new (std::nothrow) VietnameseDisplayAttributeInfo();
            if (!rgelt[0]) return E_OUTOFMEMORY;
            index_ = 1;
            if (pceltFetched) *pceltFetched = 1;
            return S_OK;
        }
        return S_FALSE;
    }

    STDMETHODIMP Reset() override {
        index_ = 0;
        return S_OK;
    }

    STDMETHODIMP Skip(ULONG celt) override {
        index_ += celt;
        if (index_ > 1) {
            index_ = 1;
            return S_FALSE;
        }
        return S_OK;
    }

private:
    std::atomic<ULONG> ref_count_;
    ULONG index_ = 0;
};

constexpr size_t kReconversionContextChars = 32;
constexpr size_t kReconversionMaxSelectedChars = 64;

struct ResolvedReconversionTarget {
    ComPtr<ITfRange> range;
    std::wstring word;
    core::rules::ReconversionSpan span;
};

struct ResolvedBrowserUrlToken {
    ComPtr<ITfRange> range;
    std::wstring token;
};

HRESULT ResolveBrowserUrlTokenBeforeCaret(
    TfEditCookie ec,
    ITfRange* selection_range,
    ResolvedBrowserUrlToken* target) {
    if (!selection_range || !target) {
        return E_INVALIDARG;
    }

    BOOL is_empty = FALSE;
    HRESULT hr = selection_range->IsEmpty(ec, &is_empty);
    if (FAILED(hr) || !is_empty) {
        return FAILED(hr) ? hr : S_FALSE;
    }

    ComPtr<ITfRange> scan_range;
    hr = selection_range->Clone(scan_range.GetAddressOf());
    if (FAILED(hr) || !scan_range) {
        return FAILED(hr) ? hr : E_FAIL;
    }
    hr = scan_range->Collapse(ec, TF_ANCHOR_END);
    if (FAILED(hr)) {
        return hr;
    }

    constexpr size_t kScanChars =
        core::kMaxRawKeysPerComposition + 1;
    LONG shifted = 0;
    hr = scan_range->ShiftStart(
        ec, -static_cast<LONG>(kScanChars), &shifted, nullptr);
    if (FAILED(hr)) {
        return hr;
    }

    std::array<wchar_t, kScanChars> text_buf{};
    const auto finish = [&](HRESULT result) {
        SecureEraseBuffer(text_buf.data(), text_buf.size());
        return result;
    };
    ULONG fetched = 0;
    hr = scan_range->GetText(
        ec, 0, text_buf.data(), static_cast<ULONG>(text_buf.size()),
        &fetched);
    if (FAILED(hr)) {
        return finish(hr);
    }

    size_t token_start = fetched;
    while (token_start > 0 &&
           core::rules::IsWordChar(text_buf[token_start - 1])) {
        --token_start;
    }
    const size_t token_length = fetched - token_start;
    if (token_length == 0 ||
        token_length > core::kMaxRawKeysPerComposition) {
        return finish(S_FALSE);
    }

    ComPtr<ITfRange> token_range;
    hr = selection_range->Clone(token_range.GetAddressOf());
    if (FAILED(hr) || !token_range) {
        return finish(FAILED(hr) ? hr : E_FAIL);
    }
    hr = token_range->Collapse(ec, TF_ANCHOR_END);
    if (FAILED(hr)) {
        return finish(hr);
    }
    shifted = 0;
    const LONG token_shift = -static_cast<LONG>(token_length);
    hr = token_range->ShiftStart(
        ec, token_shift, &shifted, nullptr);
    if (FAILED(hr) || shifted != token_shift) {
        return finish(FAILED(hr) ? hr : S_FALSE);
    }

    target->range = std::move(token_range);
    target->token.assign(
        text_buf.data() + token_start, token_length);
    return finish(S_OK);
}

HRESULT ResolveReconversionTarget(TfEditCookie ec, ITfRange* source_range, ResolvedReconversionTarget* target) {
    if (!source_range || !target) return E_INVALIDARG;

    ComPtr<ITfRange> left_range;
    ComPtr<ITfRange> right_range;
    ComPtr<ITfRange> selected_range;
    HRESULT hr = source_range->Clone(left_range.GetAddressOf());
    if (FAILED(hr)) return hr;
    hr = source_range->Clone(right_range.GetAddressOf());
    if (FAILED(hr)) return hr;
    hr = source_range->Clone(selected_range.GetAddressOf());
    if (FAILED(hr)) return hr;

    left_range->Collapse(ec, TF_ANCHOR_START);
    LONG moved = 0;
    hr = left_range->ShiftStart(ec, -static_cast<LONG>(kReconversionContextChars), &moved, nullptr);
    if (FAILED(hr)) return hr;

    right_range->Collapse(ec, TF_ANCHOR_END);
    hr = right_range->ShiftEnd(ec, static_cast<LONG>(kReconversionContextChars), &moved, nullptr);
    if (FAILED(hr)) return hr;

    std::array<wchar_t, kReconversionContextChars + 1> left_buf{};
    std::array<wchar_t, kReconversionContextChars + 1> right_buf{};
    std::array<wchar_t, kReconversionMaxSelectedChars + 1> selected_buf{};
    ULONG left_fetched = 0;
    ULONG right_fetched = 0;
    ULONG selected_fetched = 0;
    hr = left_range->GetText(ec, 0, left_buf.data(), static_cast<ULONG>(kReconversionContextChars), &left_fetched);
    if (FAILED(hr)) return hr;
    hr = right_range->GetText(ec, 0, right_buf.data(), static_cast<ULONG>(kReconversionContextChars), &right_fetched);
    if (FAILED(hr)) return hr;
    hr = selected_range->GetText(ec, 0, selected_buf.data(), static_cast<ULONG>(kReconversionMaxSelectedChars + 1), &selected_fetched);
    if (FAILED(hr)) return hr;
    if (selected_fetched > kReconversionMaxSelectedChars) return S_FALSE;

    std::wstring context(left_buf.data(), left_fetched);
    const size_t selection_start = context.length();
    context.append(selected_buf.data(), selected_fetched);
    const size_t selection_end = context.length();
    context.append(right_buf.data(), right_fetched);

    auto span = core::rules::ResolveReconversionSpan(
        context,
        selection_start,
        selection_end,
        left_fetched == kReconversionContextChars,
        right_fetched == kReconversionContextChars);
    if (!span) {
        SecureEraseString(context);
        return S_FALSE;
    }

    ComPtr<ITfRange> word_range;
    hr = source_range->Clone(word_range.GetAddressOf());
    if (FAILED(hr)) {
        SecureEraseString(context);
        return hr;
    }
    word_range->Collapse(ec, TF_ANCHOR_START);
    const LONG start_delta = static_cast<LONG>(span->start) - static_cast<LONG>(selection_start);
    const LONG end_delta = static_cast<LONG>(span->end) - static_cast<LONG>(selection_start);
    LONG shifted = 0;
    hr = word_range->ShiftStart(ec, start_delta, &shifted, nullptr);
    if (FAILED(hr) || shifted != start_delta) {
        SecureEraseString(context);
        return FAILED(hr) ? hr : S_FALSE;
    }
    hr = word_range->ShiftEnd(ec, end_delta, &shifted, nullptr);
    if (FAILED(hr) || shifted != end_delta) {
        SecureEraseString(context);
        return FAILED(hr) ? hr : S_FALSE;
    }

    target->range = std::move(word_range);
    target->word.assign(context.substr(span->start, span->end - span->start));
    target->span = *span;
    SecureEraseString(context);
    return S_OK;
}

HRESULT RestoreReconversionSelection(
    TfEditCookie ec,
    ITfContext* context,
    ITfRange* replacement_range,
    const core::rules::ReconversionSpan& span,
    size_t replacement_length,
    bool is_typed_key = false) {
    if (!context || !replacement_range) return E_INVALIDARG;
    ComPtr<ITfRange> restore_range;
    HRESULT hr = replacement_range->Clone(restore_range.GetAddressOf());
    if (FAILED(hr)) return hr;

    size_t relative_start = replacement_length;
    size_t relative_end = replacement_length;
    if (span.selection_start == span.selection_end) {
        const size_t original_offset = span.selection_start - span.start;
        const size_t original_length = span.end - span.start;
        if (original_offset < original_length) {
            const size_t new_offset = original_offset + (is_typed_key ? 1 : 0);
            relative_start = (std::min)(new_offset, replacement_length);
            relative_end = relative_start;
        } else {
            relative_start = replacement_length;
            relative_end = replacement_length;
        }
    } else if (!is_typed_key) {
        relative_start = (std::min)(span.selection_start - span.start, replacement_length);
        relative_end = (std::min)(span.selection_end - span.start, replacement_length);
    }

    restore_range->Collapse(ec, TF_ANCHOR_START);
    LONG shifted = 0;
    hr = restore_range->ShiftStart(ec, static_cast<LONG>(relative_start), &shifted, nullptr);
    if (FAILED(hr) || shifted != static_cast<LONG>(relative_start)) return FAILED(hr) ? hr : E_FAIL;
    hr = restore_range->ShiftEnd(ec, static_cast<LONG>(relative_end - relative_start), &shifted, nullptr);
    if (FAILED(hr) || shifted != static_cast<LONG>(relative_end - relative_start)) return FAILED(hr) ? hr : E_FAIL;

    TF_SELECTION selection;
    selection.range = restore_range.Get();
    selection.style.ase = TF_AE_NONE;
    selection.style.fInterimChar = FALSE;
    return context->SetSelection(ec, 1, &selection);
}

HRESULT RestoreReconversionSelectionAt(
    TfEditCookie ec,
    ITfContext* context,
    ITfRange* replacement_range,
    size_t relative_start,
    size_t relative_end) {
    if (!context || !replacement_range) return E_INVALIDARG;
    if (relative_start > relative_end) return E_INVALIDARG;
    if (relative_end >
        static_cast<size_t>((std::numeric_limits<LONG>::max)())) {
        return E_INVALIDARG;
    }

    ComPtr<ITfRange> restore_range;
    HRESULT hr = replacement_range->Clone(restore_range.GetAddressOf());
    if (FAILED(hr)) return hr;

    hr = restore_range->Collapse(ec, TF_ANCHOR_START);
    if (FAILED(hr)) return hr;
    LONG shifted = 0;
    const LONG start_delta = static_cast<LONG>(relative_start);
    hr = restore_range->ShiftStart(ec, start_delta, &shifted, nullptr);
    if (FAILED(hr) || shifted != start_delta) {
        return FAILED(hr) ? hr : E_FAIL;
    }
    const LONG end_delta =
        static_cast<LONG>(relative_end - relative_start);
    hr = restore_range->ShiftEnd(ec, end_delta, &shifted, nullptr);
    if (FAILED(hr) || shifted != end_delta) {
        return FAILED(hr) ? hr : E_FAIL;
    }

    TF_SELECTION selection;
    selection.range = restore_range.Get();
    selection.style.ase = TF_AE_NONE;
    selection.style.fInterimChar = FALSE;
    return context->SetSelection(ec, 1, &selection);
}

bool IsReconvertableWord(std::wstring_view word, core::InputMethod method) {
    if (word.empty()) return false;
    std::wstring raw = core::rules::ReconstructRawKeys(word, method);
    core::Engine engine(method);
    engine.SetEnglishProtectionLevel(core::EnglishProtectionLevel::Off);
    engine.SetSmartContextProtection(false);
    for (wchar_t ch : raw) engine.ProcessKey(ch);
    std::wstring display = engine.GetDisplayString();
    engine.SecureClear();
    std::wstring lower;
    lower.reserve(word.length());
    for (wchar_t ch : word) lower.push_back(core::rules::ToLower(ch));
    const bool valid = display == word &&
        (core::speller::IsInDictionary(lower) || core::rules::IsValidVietnamese(word, true));
    SecureEraseString(raw);
    SecureEraseString(display);
    SecureEraseString(lower);
    return valid;
}

class ReconversionCandidateString final : public ITfCandidateString {
public:
    explicit ReconversionCandidateString(std::wstring text) : text_(std::move(text)) {
        ClassFactory::IncrementActiveObjects();
    }
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (riid == IID_IUnknown || riid == IID_ITfCandidateString) {
            *ppv = static_cast<ITfCandidateString*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return AddComRef(ref_count_); }
    STDMETHODIMP_(ULONG) Release() override {
        const ULONG count = ReleaseComRef(ref_count_);
        if (count == 0) delete this;
        return count;
    }
    STDMETHODIMP GetString(BSTR* value) override {
        if (!value) return E_INVALIDARG;
        *value = SysAllocStringLen(text_.data(), static_cast<UINT>(text_.length()));
        return *value ? S_OK : E_OUTOFMEMORY;
    }
    STDMETHODIMP GetIndex(ULONG* index) override {
        if (!index) return E_INVALIDARG;
        *index = 0;
        return S_OK;
    }
private:
    ~ReconversionCandidateString() noexcept {
        SecureEraseString(text_);
        ClassFactory::DecrementActiveObjects();
    }
    std::atomic<ULONG> ref_count_{1};
    std::wstring text_;
};

class ReconversionCandidateEnumerator final : public IEnumTfCandidates {
public:
    ReconversionCandidateEnumerator(std::wstring text, bool returned = false)
        : text_(std::move(text)), returned_(returned) {
        ClassFactory::IncrementActiveObjects();
    }
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (riid == IID_IUnknown || riid == IID_IEnumTfCandidates) {
            *ppv = static_cast<IEnumTfCandidates*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return AddComRef(ref_count_); }
    STDMETHODIMP_(ULONG) Release() override {
        const ULONG count = ReleaseComRef(ref_count_);
        if (count == 0) delete this;
        return count;
    }
    STDMETHODIMP Clone(IEnumTfCandidates** result) override {
        if (!result) return E_INVALIDARG;
        *result = new (std::nothrow) ReconversionCandidateEnumerator(text_, returned_);
        return *result ? S_OK : E_OUTOFMEMORY;
    }
    STDMETHODIMP Next(ULONG count, ITfCandidateString** values, ULONG* fetched) override {
        if (!values || (count != 1 && !fetched)) return E_INVALIDARG;
        if (fetched) *fetched = 0;
        if (count == 0 || returned_) return S_FALSE;
        values[0] = new (std::nothrow) ReconversionCandidateString(text_);
        if (!values[0]) return E_OUTOFMEMORY;
        returned_ = true;
        if (fetched) *fetched = 1;
        return count == 1 ? S_OK : S_FALSE;
    }
    STDMETHODIMP Reset() override {
        returned_ = false;
        return S_OK;
    }
    STDMETHODIMP Skip(ULONG count) override {
        if (count == 0) return S_OK;
        const bool available = !returned_;
        returned_ = true;
        return available && count == 1 ? S_OK : S_FALSE;
    }
private:
    ~ReconversionCandidateEnumerator() noexcept {
        SecureEraseString(text_);
        ClassFactory::DecrementActiveObjects();
    }
    std::atomic<ULONG> ref_count_{1};
    std::wstring text_;
    bool returned_ = false;
};

class ReconversionCandidateList final : public ITfCandidateList {
public:
    explicit ReconversionCandidateList(std::wstring text) : text_(std::move(text)) {
        ClassFactory::IncrementActiveObjects();
    }
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (riid == IID_IUnknown || riid == IID_ITfCandidateList) {
            *ppv = static_cast<ITfCandidateList*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return AddComRef(ref_count_); }
    STDMETHODIMP_(ULONG) Release() override {
        const ULONG count = ReleaseComRef(ref_count_);
        if (count == 0) delete this;
        return count;
    }
    STDMETHODIMP EnumCandidates(IEnumTfCandidates** result) override {
        if (!result) return E_INVALIDARG;
        *result = new (std::nothrow) ReconversionCandidateEnumerator(text_);
        return *result ? S_OK : E_OUTOFMEMORY;
    }
    STDMETHODIMP GetCandidate(ULONG index, ITfCandidateString** result) override {
        if (!result) return E_INVALIDARG;
        *result = nullptr;
        if (index != 0) return E_INVALIDARG;
        *result = new (std::nothrow) ReconversionCandidateString(text_);
        return *result ? S_OK : E_OUTOFMEMORY;
    }
    STDMETHODIMP GetCandidateNum(ULONG* count) override {
        if (!count) return E_INVALIDARG;
        *count = 1;
        return S_OK;
    }
    STDMETHODIMP SetResult(ULONG index, TfCandidateResult) override {
        return index == 0 ? S_OK : E_INVALIDARG;
    }
private:
    ~ReconversionCandidateList() noexcept {
        SecureEraseString(text_);
        ClassFactory::DecrementActiveObjects();
    }
    std::atomic<ULONG> ref_count_{1};
    std::wstring text_;
};

enum class EditAction {
    ProcessChar,
    DirectProcessChar,
    DirectBackspace,
    DirectCommit,
    Backspace,
    Commit,
    ReconvertTest,
    Reconvert,
    QueryReconversionRange,
    ReadReconversionText,
    StartReconversion,
    CheckPassword,
    DetectBrowserTextInputMode,
    DetectTextInputScope,
    DetectEnterReplayScope,
    CaptureShorthandSelection,
    SelectionIsNonEmpty,
    ReadExcelFormulaPrefix,
    RestoreRaw,
    RestoreRawBackspace,
    SmartUndoCorrection,
    ResumeTelegramCommittedWord,
    CancelTelegramNativeSelection,
    CommitEscRaw,
    DirectRevertRaw,
    BrowserUrlReconvertTest,
    BrowserUrlReconvertApply,
};

class EditSession : public ITfEditSession {
public:
    EditSession(
        VietnameseIME* ime,
        ITfContext* pic,
        EditAction action,
        wchar_t ch = 0,
        ITfRange* requested_range = nullptr,
        WORD replay_vk = 0,
        wchar_t host_owned_commit_delimiter = L'\0') noexcept
        : ime_(ime),
          pic_(pic),
          action_(action),
          ch_(ch),
          requested_range_(requested_range),
          ref_count_(1),
          replay_vk_(replay_vk),
          host_owned_commit_delimiter_(host_owned_commit_delimiter) {
        if (ime_) ime_->AddRef();
        if (pic_) pic_->AddRef();
    }

    EditSession(VietnameseIME* ime, ITfContext* pic, EditAction action, const std::wstring& str) noexcept
        : ime_(ime), pic_(pic), action_(action), ch_(0), requested_range_(nullptr), ref_count_(1), str_(str), replay_vk_(0) {
        if (ime_) ime_->AddRef();
        if (pic_) pic_->AddRef();
    }

    virtual ~EditSession() noexcept {
        SecureEraseString(result_text_);
        SecureEraseString(str_);
        ch_ = 0;
        host_owned_commit_delimiter_ = 0;
        if (ime_) ime_->Release();
        if (pic_) pic_->Release();
    }

    bool is_convertible() const noexcept { return is_convertible_; }
    bool action_succeeded() const noexcept { return action_succeeded_; }
    bool action_executed() const noexcept { return action_executed_; }
    BrowserTextInputMode browser_text_input_mode() const noexcept {
        return browser_text_input_mode_;
    }
    const std::wstring& get_result_text() const noexcept { return result_text_; }
    const std::wstring& get_source_text() const noexcept { return str_; }
    ITfRange* detach_result_range() noexcept { return result_range_.Detach(); }

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (riid == IID_IUnknown || riid == IID_ITfEditSession) {
            *ppv = static_cast<ITfEditSession*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    STDMETHODIMP_(ULONG) AddRef() override { return AddComRef(ref_count_); }
    STDMETHODIMP_(ULONG) Release() override {
        const ULONG count = ReleaseComRef(ref_count_);
        if (count == 0) delete this;
        return count;
    }

    STDMETHODIMP DoEditSession(TfEditCookie ec) override {
        logger::Log(logger::Level::Info, L"EditSession::DoEditSession started");
        if (!ime_ || !pic_) {
            logger::Log(logger::Level::Error, L"EditSession: ime_ or pic_ is null");
            return E_FAIL;
        }

        struct SelectionUpdateGuard {
            VietnameseIME* ime;
            SelectionUpdateGuard(VietnameseIME* i) : ime(i) {
                if (ime) ime->is_updating_selection_ = true;
            }
            ~SelectionUpdateGuard() {
                if (ime) ime->is_updating_selection_ = false;
            }
        } guard(ime_);

        if (action_ == EditAction::CheckPassword) {
            logger::Log(logger::Level::Info, L"EditSession: executing CheckPassword...");
            action_executed_ = true;
            action_succeeded_ = true;
            ime_->SetPasswordField(false);
            ComPtr<ITfReadOnlyProperty> prop;
            if (SUCCEEDED(pic_->GetAppProperty(GUID_PROP_INPUTSCOPE_LOCAL, prop.GetAddressOf())) && prop) {
                ComPtr<ITfRange> range;
                TF_SELECTION sel;
                ULONG fetched = 0;
                if (SUCCEEDED(pic_->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &sel, &fetched)) && fetched > 0) {
                    range.Attach(sel.range);
                } else {
                    ComPtr<ITfRange> start_range;
                    if (SUCCEEDED(pic_->GetStart(ec, start_range.GetAddressOf()))) {
                        range = start_range;
                    }
                }

                if (range) {
                    VARIANT var;
                    VariantInit(&var);
                    if (SUCCEEDED(prop->GetValue(ec, range.Get(), &var))) {
                        if (var.vt == VT_UNKNOWN && var.punkVal != nullptr) {
                            ComPtr<ITfInputScope> input_scope;
                            if (SUCCEEDED(var.punkVal->QueryInterface(IID_ITfInputScope_LOCAL, reinterpret_cast<void**>(input_scope.GetAddressOf())))) {
                                InputScope* scopes = nullptr;
                                UINT count = 0;
                                if (SUCCEEDED(input_scope->GetInputScopes(&scopes, &count)) && scopes) {
                                    for (UINT i = 0; i < count; ++i) {
                                        logger::LogFormat(logger::Level::Info, L"Found InputScope: %u", static_cast<unsigned int>(scopes[i]));
                                        if (IsPasswordInputScope(scopes[i])) {
                                            logger::Log(logger::Level::Info, L"Password field detected via InputScope");
                                            ime_->SetPasswordField(true);
                                            break;
                                        }
                                    }
                                    ::CoTaskMemFree(scopes);
                                }
                            }
                        }
                        VariantClear(&var);
                    }
                }
            }
            if (ime_->IsPasswordField() && ime_->HasActiveComposition()) {
                logger::Log(logger::Level::Info, L"CheckPassword: ending existing composition in secure context");
                ime_->EndComposition(ec);
            }
            return S_OK;
        }

        if (action_ == EditAction::DetectBrowserTextInputMode) {
            browser_text_input_mode_ =
                BrowserTextInputMode::NativeComposition;
            if (!ime_->IsBrowserProcess() ||
                ime_->IsSecureInputContext()) {
                return S_OK;
            }

            ComPtr<ITfReadOnlyProperty> prop;
            if (FAILED(pic_->GetAppProperty(
                    GUID_PROP_INPUTSCOPE_LOCAL,
                    prop.GetAddressOf())) || !prop) {
                return S_OK;
            }

            ComPtr<ITfRange> scope_range;
            TF_SELECTION scope_sel{};
            ULONG scope_fetched = 0;
            if (SUCCEEDED(pic_->GetSelection(
                    ec, TF_DEFAULT_SELECTION, 1,
                    &scope_sel, &scope_fetched)) &&
                scope_fetched > 0) {
                scope_range.Attach(scope_sel.range);
            } else {
                pic_->GetStart(ec, scope_range.GetAddressOf());
            }
            if (!scope_range) {
                return S_OK;
            }

            VARIANT var;
            VariantInit(&var);
            const HRESULT value_hr = prop->GetValue(
                ec, scope_range.Get(), &var);
            if (SUCCEEDED(value_hr) && var.vt == VT_UNKNOWN &&
                var.punkVal != nullptr) {
                ComPtr<ITfInputScope> input_scope;
                if (SUCCEEDED(var.punkVal->QueryInterface(
                        IID_ITfInputScope_LOCAL,
                        reinterpret_cast<void**>(
                            input_scope.GetAddressOf()))) &&
                    input_scope) {
                    InputScope* scopes = nullptr;
                    UINT count = 0;
                    if (SUCCEEDED(input_scope->GetInputScopes(
                            &scopes, &count)) && scopes) {
                        browser_text_input_mode_ =
                            SelectBrowserTextInputMode(
                                true, false,
                                std::span<const InputScope>(
                                    scopes, count));
                        action_succeeded_ = true;
                        ::CoTaskMemFree(scopes);
                    }
                }
            }
            VariantClear(&var);
            logger::LogFormat(
                logger::Level::Debug,
                L"DetectBrowserTextInputMode: success=%d, mode=%d",
                action_succeeded_ ? 1 : 0,
                static_cast<int>(browser_text_input_mode_));
            return S_OK;
        }

        if (action_ == EditAction::DetectTextInputScope || action_ == EditAction::DetectEnterReplayScope) {
            action_succeeded_ = true;
            is_convertible_ = false;

            ComPtr<ITfReadOnlyProperty> prop;
            if (SUCCEEDED(pic_->GetAppProperty(GUID_PROP_INPUTSCOPE_LOCAL, prop.GetAddressOf())) && prop) {
                ComPtr<ITfRange> scope_range;
                TF_SELECTION scope_sel{};
                ULONG scope_fetched = 0;
                if (SUCCEEDED(pic_->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &scope_sel, &scope_fetched)) && scope_fetched > 0) {
                    scope_range.Attach(scope_sel.range);
                } else {
                    ComPtr<ITfRange> start_range;
                    if (SUCCEEDED(pic_->GetStart(ec, start_range.GetAddressOf()))) {
                        scope_range = start_range;
                    }
                }

                if (scope_range) {
                    VARIANT var;
                    VariantInit(&var);
                    if (SUCCEEDED(prop->GetValue(ec, scope_range.Get(), &var))) {
                        if (var.vt == VT_UNKNOWN && var.punkVal != nullptr) {
                            ComPtr<ITfInputScope> input_scope;
                            if (SUCCEEDED(var.punkVal->QueryInterface(IID_ITfInputScope_LOCAL, reinterpret_cast<void**>(input_scope.GetAddressOf())))) {
                                InputScope* scopes = nullptr;
                                UINT count = 0;
                                if (SUCCEEDED(input_scope->GetInputScopes(&scopes, &count)) && scopes) {
                                    bool saw_text_scope = false;
                                    bool saw_password_scope = false;
                                    bool saw_enter_replay_scope = false;
                                    for (UINT i = 0; i < count; ++i) {
                                        saw_text_scope = true;
                                        if (IsPasswordInputScope(scopes[i])) {
                                            saw_password_scope = true;
                                            break;
                                        }
                                        if (IsEnterReplayInputScope(scopes[i])) {
                                            saw_enter_replay_scope = true;
                                        }
                                    }
                                    is_convertible_ = !saw_password_scope &&
                                        (action_ == EditAction::DetectTextInputScope ? saw_text_scope : saw_enter_replay_scope);
                                    ::CoTaskMemFree(scopes);
                                }
                            }
                        }
                        VariantClear(&var);
                    }
                }
            }

            logger::LogFormat(logger::Level::Debug,
                              L"%s: convertible = %s",
                              action_ == EditAction::DetectTextInputScope ? L"DetectTextInputScope" : L"DetectEnterReplayScope",
                              is_convertible_ ? L"TRUE" : L"FALSE");
            return S_OK;
        }

        if (ime_->IsSecureInputContext()) {
            logger::Log(logger::Level::Info, L"EditSession: secure context detected, clearing state and skipping action");
            if (ime_->HasActiveComposition()) {
                ime_->EndComposition(ec);
            }
            ime_->ClearSensitiveState(false);
            return S_OK;
        }

        if (action_ == EditAction::RestoreRaw ||
            action_ == EditAction::RestoreRawBackspace) {
            action_succeeded_ = ime_->TryRestoreLastCommittedRaw(
                ec, pic_, action_ == EditAction::RestoreRawBackspace);
            return S_OK;
        }

        if (action_ == EditAction::SmartUndoCorrection) {
            action_succeeded_ =
                ime_->TrySmartUndoLastCommittedCorrection(ec, pic_);
            return S_OK;
        }

        if (action_ == EditAction::ResumeTelegramCommittedWord) {
            action_succeeded_ = ime_->ResumeTelegramCommittedWord(ec, pic_);
            return S_OK;
        }

        if (action_ == EditAction::CancelTelegramNativeSelection) {
            action_succeeded_ = ime_->CollapseTelegramNativeSelection(ec, pic_);
            return S_OK;
        }
        
        ComPtr<ITfRange> range;
        TF_SELECTION sel{};
        ULONG fetched = 0;
        HRESULT hr = S_OK;
        if (requested_range_) {
            hr = requested_range_->Clone(range.GetAddressOf());
            if (FAILED(hr) || !range) return FAILED(hr) ? hr : E_FAIL;
        } else {
            hr = pic_->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &sel, &fetched);
            logger::LogFormat(logger::Level::Info, L"GetSelection returned hr = 0x%08X, fetched = %u", hr, fetched);
            if (FAILED(hr) || fetched == 0) {
                logger::Log(logger::Level::Error, L"GetSelection failed or returned fetched = 0");
                return E_FAIL;
            }
            range.Attach(sel.range);
        }

        if (action_ == EditAction::SelectionIsNonEmpty) {
            BOOL selection_empty = TRUE;
            HRESULT hrEmpty = range->IsEmpty(ec, &selection_empty);
            if (FAILED(hrEmpty)) {
                return hrEmpty;
            }
            is_convertible_ = !selection_empty;
            action_succeeded_ = true;
            return S_OK;
        }

        if (action_ == EditAction::CaptureShorthandSelection) {
            action_succeeded_ = ime_->CaptureTsfShorthandSelection(
                ec, range.Get(), ch_);
            return S_OK;
        }

        auto commit_fallback_text = [&](const std::wstring& text) -> HRESULT {
            if (text.empty()) {
                return S_OK;
            }

            ComPtr<ITfRange> fallback_range;
            HRESULT hrFallback = range->Clone(fallback_range.GetAddressOf());
            if (FAILED(hrFallback) || !fallback_range) {
                return FAILED(hrFallback) ? hrFallback : E_FAIL;
            }

            hrFallback = fallback_range->SetText(ec, 0, text.c_str(), static_cast<LONG>(text.length()));
            if (FAILED(hrFallback)) {
                return hrFallback;
            }

            fallback_range->Collapse(ec, TF_ANCHOR_END);
            TF_SELECTION fallback_sel;
            fallback_sel.range = fallback_range.Get();
            fallback_sel.style.ase = TF_AE_NONE;
            fallback_sel.style.fInterimChar = FALSE;
            hrFallback = pic_->SetSelection(ec, 1, &fallback_sel);
            ime_->ClearSensitiveState(false);
            return hrFallback;
        };

        if (action_ == EditAction::BrowserUrlReconvertTest ||
            action_ == EditAction::BrowserUrlReconvertApply) {
            const bool apply =
                action_ == EditAction::BrowserUrlReconvertApply;
            if (ime_->HasActiveComposition() ||
                !ime_->IsBrowserUrlNativeModeActiveForContext(pic_)) {
                return S_OK;
            }

            ResolvedBrowserUrlToken target;
            const HRESULT resolve_hr = ResolveBrowserUrlTokenBeforeCaret(
                ec, range.Get(), &target);
            action_executed_ = SUCCEEDED(resolve_hr);
            if (resolve_hr != S_OK) {
                logger::LogFormat(
                    logger::Level::Debug,
                    L"Browser URL typed reconversion resolve: apply=%d, hr=0x%08X",
                    apply ? 1 : 0, resolve_hr);
                return S_OK;
            }

            const core::InputMethod method = apply
                ? ime_->browser_url_pending_method_
                : ime_->GetEngine().GetInputMethod();
            const core::CorrectionLevel correction_level = apply
                ? ime_->browser_url_pending_correction_level_
                : ime_->GetEngine().GetCorrectionLevel();
            const core::EnglishProtectionLevel english_level = apply
                ? ime_->browser_url_pending_english_level_
                : ime_->GetEngine().GetEnglishProtectionLevel();
            const bool smart_context_protection = apply
                ? ime_->browser_url_pending_smart_context_protection_
                : ime_->GetEngine().GetSmartContextProtection();
            auto candidate =
                core::BuildBrowserUrlTypedReconversionCandidate(
                    target.token, ch_, method, correction_level,
                    english_level, smart_context_protection);

            if (candidate && !apply) {
                str_ = target.token;
                result_text_ = *candidate;
                is_convertible_ = true;
                action_succeeded_ = true;
            } else if (candidate && apply) {
                const bool exact_pending =
                    ime_->browser_url_pending_context_ &&
                    IsSameComObject(
                        pic_, ime_->browser_url_pending_context_.Get()) &&
                    ch_ == ime_->browser_url_pending_key_ &&
                    method == ime_->GetEngine().GetInputMethod() &&
                    target.token == ime_->browser_url_pending_token_ &&
                    *candidate == ime_->browser_url_pending_replacement_;
                if (exact_pending) {
                    const HRESULT text_hr = target.range->SetText(
                        ec, 0, candidate->c_str(),
                        static_cast<LONG>(candidate->length()));
                    const auto place_caret_at_range_end = [&]() {
                        ComPtr<ITfRange> caret_range;
                        HRESULT caret_hr = target.range->Clone(
                            caret_range.GetAddressOf());
                        if (SUCCEEDED(caret_hr) && caret_range) {
                            caret_hr = caret_range->Collapse(
                                ec, TF_ANCHOR_END);
                        }
                        if (SUCCEEDED(caret_hr) && caret_range) {
                            TF_SELECTION caret_selection{};
                            caret_selection.range = caret_range.Get();
                            caret_selection.style.ase = TF_AE_NONE;
                            caret_selection.style.fInterimChar = FALSE;
                            caret_hr = pic_->SetSelection(
                                ec, 1, &caret_selection);
                        }
                        return caret_hr;
                    };

                    HRESULT selection_hr = E_FAIL;
                    if (SUCCEEDED(text_hr)) {
                        selection_hr = place_caret_at_range_end();
                    }
                    HRESULT rollback_text_hr = S_FALSE;
                    HRESULT rollback_selection_hr = S_FALSE;
                    bool rollback_succeeded = false;
                    if (SUCCEEDED(text_hr) && FAILED(selection_hr)) {
                        rollback_text_hr = target.range->SetText(
                            ec, 0, target.token.c_str(),
                            static_cast<LONG>(target.token.length()));
                        if (SUCCEEDED(rollback_text_hr)) {
                            rollback_selection_hr =
                                place_caret_at_range_end();
                        }
                        rollback_succeeded =
                            SUCCEEDED(rollback_text_hr) &&
                            SUCCEEDED(rollback_selection_hr);
                    }
                    action_succeeded_ =
                        SUCCEEDED(text_hr) &&
                        (SUCCEEDED(selection_hr) || !rollback_succeeded);
                    is_convertible_ = action_succeeded_;
                    logger::LogFormat(
                        action_succeeded_ ? logger::Level::Debug
                                          : logger::Level::Warning,
                        L"Browser URL typed reconversion apply: text_hr=0x%08X, selection_hr=0x%08X, rollback_text_hr=0x%08X, rollback_selection_hr=0x%08X, rollback_ok=%d, source_len=%zu, replacement_len=%zu",
                        text_hr, selection_hr, rollback_text_hr,
                        rollback_selection_hr,
                        rollback_succeeded ? 1 : 0,
                        target.token.length(), candidate->length());
                }
            }

            logger::LogFormat(
                logger::Level::Debug,
                L"Browser URL typed reconversion: apply=%d, source_len=%zu, candidate_len=%zu, accepted=%d",
                apply ? 1 : 0, target.token.length(),
                candidate ? candidate->length() : 0,
                is_convertible_ ? 1 : 0);
            if (candidate) {
                SecureEraseString(*candidate);
            }
            SecureEraseString(target.token);
            return S_OK;
        }
        if (action_ == EditAction::DirectProcessChar) {
            logger::Log(logger::Level::Info, L"EditAction::DirectProcessChar");
            if (ime_->HasDirectInlineState()) {
                bool inline_state_valid = false;
                if (ime_->direct_inline_display_length_ > 0) {
                    BOOL selection_empty = TRUE;
                    if (SUCCEEDED(range->IsEmpty(ec, &selection_empty)) && selection_empty) {
                        ComPtr<ITfRange> check_range;
                        if (SUCCEEDED(range->Clone(check_range.GetAddressOf())) && check_range) {
                            if (SUCCEEDED(check_range->Collapse(ec, TF_ANCHOR_START))) {
                                LONG shifted = 0;
                                LONG to_shift = -static_cast<LONG>(ime_->direct_inline_display_length_);
                                if (SUCCEEDED(check_range->ShiftStart(ec, to_shift, &shifted, nullptr)) && (shifted == to_shift || shifted == -to_shift)) {
                                    std::wstring text_buf(ime_->direct_inline_display_length_, L'\0');
                                    ULONG fetched_chars = 0;
                                    if (SUCCEEDED(check_range->GetText(ec, 0, &text_buf[0], static_cast<ULONG>(text_buf.size()), &fetched_chars)) && fetched_chars == ime_->direct_inline_display_length_) {
                                        std::wstring expected_display = ime_->GetEngine().GetDisplayString();
                                        if (text_buf == expected_display) {
                                            inline_state_valid = true;
                                        } else if (ime_->IsWordTsfInlineApp() &&
                                                   ime_->GetEngine().UpdateCasingFromHost(text_buf)) {
                                            inline_state_valid = true;
                                            logger::LogFormat(
                                                logger::Level::Info,
                                                L"Inline validation: accepted host casing update (len: %zu)",
                                                text_buf.length());
                                        } else {
                                            logger::LogFormat(logger::Level::Info, L"Inline validation: text mismatch (len: %zu vs %zu)", text_buf.length(), expected_display.length());
                                        }
                                        SecureEraseString(expected_display);
                                    } else {
                                        logger::LogFormat(logger::Level::Info, L"Inline validation: GetText failed or fetched incorrect length %u", fetched_chars);
                                    }
                                    SecureEraseString(text_buf);
                                } else {
                                    logger::LogFormat(logger::Level::Info, L"Inline validation: ShiftStart failed to shift expected characters (shifted %d vs %d)", shifted, to_shift);
                                }
                            }
                        }
                    } else {
                        logger::Log(logger::Level::Info, L"Inline validation: selection is non-empty");
                    }
                } else {
                    inline_state_valid = true;
                }

                if (!inline_state_valid) {
                    logger::Log(logger::Level::Info, L"Invalid inline state detected in DirectProcessChar, resetting inline state");
                    ime_->ResetDirectInlineState();
                }
            }

            if (!ime_->HasDirectInlineState()) {
                (void)ime_->CaptureTsfShorthandSelection(
                    ec, range.Get(), ch_);
                IMEConfig config = LoadConfigFromRegistry();
                if (config.enable_auto_capitalize &&
                    (ShouldAutoCapitalizeAtRange(ec, range.Get()) || ShouldAutoCapitalizeAtFocusedControl())) {
                    ch_ = core::rules::ToUpper(ch_);
                    logger::Log(logger::Level::Info, L"Auto-capitalized first direct inline key");
                }
                ime_->GetEngine().Clear();
            }

            std::wstring old_disp = ime_->GetEngine().GetDisplayString();
            ime_->GetEngine().ProcessKey(ch_);
            std::wstring disp = ime_->GetEngine().GetDisplayString();
            logger::LogFormat(logger::Level::Info, L"Direct inline display length: %zu", disp.length());
            bool text_applied = false;
            HRESULT hrDirect = ime_->ReplaceDirectInlineText(
                ec, pic_, range.Get(), disp, old_disp, ch_, &text_applied);
            SecureEraseString(disp);
            SecureEraseString(old_disp);
            action_succeeded_ = ShouldConsumeDirectInlineMutation(
                SUCCEEDED(hrDirect), text_applied);
            if (FAILED(hrDirect) && !text_applied) return hrDirect;
            if (FAILED(hrDirect)) {
                logger::LogFormat(
                    logger::Level::Warning,
                    L"Direct inline caret update failed after text mutation; key remains consumed: hr=0x%08X",
                    hrDirect);
            }
        }
        else if (action_ == EditAction::DirectBackspace) {
            logger::Log(logger::Level::Info, L"EditAction::DirectBackspace");
            if (ime_->HasDirectInlineState()) {
                bool inline_state_valid = false;
                if (ime_->direct_inline_display_length_ > 0) {
                    BOOL selection_empty = TRUE;
                    if (SUCCEEDED(range->IsEmpty(ec, &selection_empty)) && selection_empty) {
                        ComPtr<ITfRange> check_range;
                        if (SUCCEEDED(range->Clone(check_range.GetAddressOf())) && check_range) {
                            if (SUCCEEDED(check_range->Collapse(ec, TF_ANCHOR_START))) {
                                LONG shifted = 0;
                                LONG to_shift = -static_cast<LONG>(ime_->direct_inline_display_length_);
                                if (SUCCEEDED(check_range->ShiftStart(ec, to_shift, &shifted, nullptr)) && (shifted == to_shift || shifted == -to_shift)) {
                                    std::wstring text_buf(ime_->direct_inline_display_length_, L'\0');
                                    ULONG fetched_chars = 0;
                                    if (SUCCEEDED(check_range->GetText(ec, 0, &text_buf[0], static_cast<ULONG>(text_buf.size()), &fetched_chars)) && fetched_chars == ime_->direct_inline_display_length_) {
                                        std::wstring expected_display = ime_->GetEngine().GetDisplayString();
                                        if (text_buf == expected_display) {
                                            inline_state_valid = true;
                                        } else if (ime_->IsWordTsfInlineApp() &&
                                                   ime_->GetEngine().UpdateCasingFromHost(text_buf)) {
                                            inline_state_valid = true;
                                            logger::LogFormat(
                                                logger::Level::Info,
                                                L"Inline validation: accepted host casing update (len: %zu) (backspace)",
                                                text_buf.length());
                                        } else {
                                            logger::LogFormat(logger::Level::Info, L"Inline validation: text mismatch (len: %zu vs %zu) (backspace)", text_buf.length(), expected_display.length());
                                        }
                                        SecureEraseString(expected_display);
                                    } else {
                                        logger::LogFormat(logger::Level::Info, L"Inline validation: GetText failed or fetched incorrect length %u (backspace)", fetched_chars);
                                    }
                                    SecureEraseString(text_buf);
                                } else {
                                    logger::LogFormat(logger::Level::Info, L"Inline validation: ShiftStart failed to shift expected characters (shifted %d vs %d) (backspace)", shifted, to_shift);
                                }
                            }
                        }
                    } else {
                        logger::Log(logger::Level::Info, L"Inline validation: selection is non-empty (backspace)");
                    }
                } else {
                    inline_state_valid = true;
                }

                if (!inline_state_valid) {
                    logger::Log(logger::Level::Info, L"Invalid inline state detected in DirectBackspace, resetting inline state");
                    ime_->ResetDirectInlineState();
                    action_succeeded_ = false;
                    return S_OK;
                }
            }

            if (ime_->HasDirectInlineState()) {
                ime_->GetEngine().BackspaceDisplayChar();
                std::wstring raw = ime_->GetEngine().GetRawString();
                std::wstring disp = ime_->GetEngine().GetDisplayString();
                logger::LogFormat(logger::Level::Info, L"Direct backspace: raw_empty = %s, display_length = %zu", raw.empty() ? L"TRUE" : L"FALSE", disp.length());
                bool text_applied = false;
                HRESULT hrDirect = ime_->ReplaceDirectInlineText(
                    ec, pic_, range.Get(), disp, L"", 0, &text_applied);
                logger::LogFormat(logger::Level::Info, L"Direct backspace replace returned hr = 0x%08X", hrDirect);
                action_succeeded_ = ShouldConsumeDirectInlineMutation(
                    SUCCEEDED(hrDirect), text_applied);
                if (raw.empty()) {
                    ime_->ResetDirectInlineState();
                }
                SecureEraseString(raw);
                SecureEraseString(disp);
                if (FAILED(hrDirect) && !text_applied) return hrDirect;
                if (FAILED(hrDirect)) {
                    logger::LogFormat(
                        logger::Level::Warning,
                        L"Direct inline backspace caret update failed after text mutation; key remains consumed: hr=0x%08X",
                        hrDirect);
                }
            }
        }
        else if (action_ == EditAction::DirectCommit) {
            logger::LogFormat(logger::Level::Info, L"EditAction::DirectCommit: has_delimiter = %s", ch_ != 0 ? L"TRUE" : L"FALSE");
            const wchar_t transform_delimiter =
                core::ResolveCommitTransformDelimiter(
                    ch_, host_owned_commit_delimiter_);
            std::wstring raw = ime_->GetEngine().GetRawString();
            std::wstring pre_speller =
                ime_->GetEngine().GetPreCorrectionDisplayString();
            core::EngineDisplayResult engine_display =
                ime_->GetEngine().GetDisplayResult();
            std::wstring committed_display = engine_display.text;
            CommitUndoEntry::TransformKind transform_kind =
                engine_display.HasSpellerCorrection()
                    ? CommitUndoEntry::TransformKind::SpellerCorrection
                    : CommitUndoEntry::TransformKind::None;

            std::wstring previous_token;
            if (ime_->enable_fuzzy_input_ && transform_delimiter == L' ' &&
                !ime_->IsExcelApp() && !ime_->IsInkscapeApp() &&
                !ime_->IsFakeBackspaceApp()) {
                previous_token = ReadImmediatePreviousTokenFromTsf(
                    ec, range.Get(), committed_display);
            }
            const bool allow_previous_token_rewrite =
                !previous_token.empty() && !ime_->IsTelegramProcess() &&
                !ime_->IsInkscapeApp() &&
                ime_->GetFocusedProcessName() != L"anydesk.exe";
            auto transform = ime_->BuildDirectCommitTransformDecision(
                raw, committed_display, pre_speller, transform_delimiter,
                previous_token, allow_previous_token_rewrite,
                host_owned_commit_delimiter_ == 0);
            core::CommitTransformDecision& decision = transform.decision;
            const bool rewrite_requested =
                decision.RequiresRewrite() ||
                transform.caret_offset.has_value();
            bool rewrite_succeeded = false;
            ComPtr<ITfRange> applied_range;
            std::wstring original_text_for_undo;
            if (rewrite_requested) {
                bool text_applied = false;
                HRESULT hrTransform = E_FAIL;
                if (decision.rewrite_scope ==
                    core::CommitRewriteScope::PreviousAndCurrent) {
                    if (TryGetVerifiedTsfTextImmediatelyBeforeCaret(
                            ec, range.Get(), decision.expected_source,
                            applied_range)) {
                        SelectionUpdateScope selection_scope(
                            ime_->is_updating_selection_);
                        hrTransform = applied_range->SetText(
                            ec, 0, decision.text.c_str(),
                            static_cast<LONG>(decision.text.length()));
                        text_applied = SUCCEEDED(hrTransform);
                        if (text_applied) {
                            ComPtr<ITfRange> caret_after;
                            HRESULT caret_hr = applied_range->Clone(
                                caret_after.GetAddressOf());
                            if (SUCCEEDED(caret_hr) && caret_after) {
                                caret_hr = caret_after->Collapse(
                                    ec, TF_ANCHOR_END);
                            }
                            if (SUCCEEDED(caret_hr) && caret_after) {
                                TF_SELECTION selection_after{};
                                selection_after.range = caret_after.Get();
                                selection_after.style.ase = TF_AE_NONE;
                                selection_after.style.fInterimChar = FALSE;
                                caret_hr = pic_->SetSelection(
                                    ec, 1, &selection_after);
                            }
                            if (FAILED(caret_hr)) {
                                hrTransform = caret_hr;
                            }
                        }
                    }
                } else if (VerifyTsfTextImmediatelyBeforeCaret(
                               ec, range.Get(),
                               decision.expected_source)) {
                    hrTransform = ime_->ReplaceDirectInlineText(
                        ec, pic_, range.Get(), decision.text, L"", 0,
                        &text_applied,
                        transform.caret_offset
                            ? applied_range.GetAddressOf()
                            : nullptr);
                }
                logger::LogFormat(
                    logger::Level::Info,
                    L"Direct commit transform: hr=0x%08X, kind=%d, scope=%d, display_len=%zu",
                    hrTransform, static_cast<int>(decision.transform_kind),
                    static_cast<int>(decision.rewrite_scope),
                    decision.text.length());
                if (SUCCEEDED(hrTransform) || text_applied) {
                    VietnameseIME::ShorthandCaretTransactionResult prepared =
                        VietnameseIME::ShorthandCaretTransactionResult::Applied;
                    if (transform.caret_offset && SUCCEEDED(hrTransform)) {
                        prepared = ime_->PrepareShorthandCaretTransaction(
                            ec, pic_, applied_range.Get(),
                            committed_display, *transform.caret_offset);
                    }
                    rewrite_succeeded = prepared !=
                        VietnameseIME::ShorthandCaretTransactionResult::RolledBack;
                    if (rewrite_succeeded) {
                        committed_display = decision.text;
                        transform_kind = decision.transform_kind;
                        original_text_for_undo = decision.undo_text;
                    }
                }
            }
            if (rewrite_requested && !rewrite_succeeded) {
                ime_->ClearLastCommitUndo();
            } else if (transform_delimiter == L' ' &&
                       !ime_->HasPendingShorthandCaretTransaction()) {
                ime_->CaptureCommitUndoDirectInlineTsf(
                    ec, pic_, committed_display, transform_kind,
                    original_text_for_undo,
                    original_text_for_undo.empty()
                        ? nullptr
                        : applied_range.Get());
            }
            SecureEraseString(previous_token);
            SecureEraseString(pre_speller);
            SecureEraseString(original_text_for_undo);
            ime_->ResetDirectInlineState();
            if (ch_ != 0) {
                ComPtr<ITfRange> delimiter_range;
                TF_SELECTION delimiter_selection{};
                ULONG delimiter_fetched = 0;
                const HRESULT hrGetSelection = pic_->GetSelection(
                    ec, TF_DEFAULT_SELECTION, 1,
                    &delimiter_selection, &delimiter_fetched);
                if (SUCCEEDED(hrGetSelection) && delimiter_fetched > 0 &&
                    delimiter_selection.range) {
                    delimiter_range.Attach(delimiter_selection.range);
                }
                wchar_t delim[2] = { ch_, L'\0' };
                HRESULT hrText = delimiter_range
                    ? delimiter_range->SetText(ec, 0, delim, 1)
                    : E_FAIL;
                logger::LogFormat(logger::Level::Info, L"Direct commit SetText returned hr = 0x%08X", hrText);

                if (delimiter_range) {
                    delimiter_range->Collapse(ec, TF_ANCHOR_END);
                }
                delimiter_selection.range = delimiter_range.Get();
                delimiter_selection.style.ase = TF_AE_NONE;
                delimiter_selection.style.fInterimChar = FALSE;
                HRESULT hrSetSel = delimiter_range
                    ? pic_->SetSelection(ec, 1, &delimiter_selection)
                    : E_FAIL;
                logger::LogFormat(logger::Level::Info, L"Direct commit SetSelection returned hr = 0x%08X", hrSetSel);
                action_succeeded_ = SUCCEEDED(hrText) && SUCCEEDED(hrSetSel);
                if (ime_->last_commit_undo_ &&
                    ime_->last_commit_undo_->is_tsf &&
                    IsSameComObject(
                        pic_, ime_->last_commit_undo_->expected_context.Get())) {
                    ime_->last_commit_undo_->committed_with_ascii_space =
                        action_succeeded_ && ch_ == L' ';
                }
            } else {
                action_succeeded_ = true;
                if (ime_->last_commit_undo_ &&
                    ime_->last_commit_undo_->is_tsf &&
                    IsSameComObject(
                        pic_, ime_->last_commit_undo_->expected_context.Get())) {
                    ime_->last_commit_undo_->committed_with_ascii_space =
                        host_owned_commit_delimiter_ == L' ';
                }
            }
            if (ime_->HasPendingShorthandCaretTransaction()) {
                const auto result =
                    ime_->FinalizeShorthandCaretTransaction(ec, pic_);
                if (result != VietnameseIME::
                        ShorthandCaretTransactionResult::Applied) {
                    ime_->ClearLastCommitUndo();
                }
            }
            SecureEraseString(decision.text);
            SecureEraseString(decision.expected_source);
            SecureEraseString(decision.undo_text);
            SecureEraseString(committed_display);
            SecureEraseString(engine_display.text);
            SecureEraseString(raw);
        }
        
        else if (action_ == EditAction::ProcessChar) {
            logger::Log(logger::Level::Info, L"EditAction::ProcessChar");
            if (ime_->HasActiveComposition()) {
                bool is_selection_at_composition_end = false;
                ComPtr<ITfRange> comp_range;
                if (SUCCEEDED(ime_->active_composition_->GetRange(comp_range.GetAddressOf())) && comp_range) {
                    BOOL selection_empty = TRUE;
                    if (SUCCEEDED(range->IsEmpty(ec, &selection_empty)) && selection_empty) {
                        LONG comparison = 0;
                        if (SUCCEEDED(range->CompareStart(ec, comp_range.Get(), TF_ANCHOR_END, &comparison)) && comparison == 0) {
                            is_selection_at_composition_end = true;
                        }
                    }
                }
                
                if (!is_selection_at_composition_end && !ime_->IsTelegramProcess()) {
                    logger::Log(logger::Level::Info, L"ProcessChar: Selection is not at composition end, committing active composition first");
                    ime_->EndComposition(ec);
                }
            }

            if (!ime_->HasActiveComposition()) {
                logger::Log(logger::Level::Info, L"No active composition, starting new one");
                ime_->ResetDirectInlineState(true);
                (void)ime_->CaptureTsfShorthandSelection(
                    ec, range.Get(), ch_);
                IMEConfig config = LoadConfigFromRegistry();
                if (config.enable_auto_capitalize &&
                    (ShouldAutoCapitalizeAtRange(ec, range.Get()) || ShouldAutoCapitalizeAtFocusedControl())) {
                    ch_ = core::rules::ToUpper(ch_);
                    logger::Log(logger::Level::Info, L"Auto-capitalized first composition key");
                }

                ime_->GetEngine().Clear();
                ime_->GetEngine().ProcessKey(ch_);
                std::wstring disp = ime_->GetEngine().GetDisplayString();
                logger::LogFormat(logger::Level::Info, L"Engine display length: %zu", disp.length());

                HRESULT hrComp = ime_->StartComposition(ec, pic_, range.Get());
                logger::LogFormat(logger::Level::Info, L"StartComposition returned hr = 0x%08X", hrComp);
                if (SUCCEEDED(hrComp)) {
                    HRESULT hrUpdate = ime_->UpdateCompositionText(ec, pic_, range.Get(), disp);
                    logger::LogFormat(logger::Level::Info, L"UpdateCompositionText returned hr = 0x%08X", hrUpdate);
                    if (SUCCEEDED(hrUpdate)) {
                        action_succeeded_ = true;
                    } else {
                        if (ime_->HasActiveComposition()) {
                            ime_->EndComposition(ec);
                        }
                        HRESULT hrFallback = commit_fallback_text(disp);
                        logger::LogFormat(logger::Level::Warning, L"ProcessChar update fallback returned hr = 0x%08X", hrFallback);
                        action_succeeded_ = SUCCEEDED(hrFallback);
                        if (FAILED(hrFallback)) {
                            SecureEraseString(disp);
                            return hrFallback;
                        }
                    }
                } else {
                    BOOL selection_empty = TRUE;
                    HRESULT hrEmpty = range->IsEmpty(ec, &selection_empty);
                    if (SUCCEEDED(hrEmpty) && !selection_empty) {
                        range->SetText(ec, 0, L"", 0);
                        range->Collapse(ec, TF_ANCHOR_START);
                    }
                    HRESULT hrFallback = commit_fallback_text(disp);
                    logger::LogFormat(logger::Level::Warning, L"ProcessChar start fallback returned hr = 0x%08X", hrFallback);
                    action_succeeded_ = SUCCEEDED(hrFallback);
                    if (FAILED(hrFallback)) {
                        SecureEraseString(disp);
                        return hrFallback;
                    }
                }
                SecureEraseString(disp);
            } else {
                logger::Log(logger::Level::Info, L"Active composition exists, updating");
                ime_->GetEngine().ProcessKey(ch_);
                std::wstring disp = ime_->GetEngine().GetDisplayString();
                logger::LogFormat(logger::Level::Info, L"Engine display length: %zu", disp.length());
                HRESULT hrUpdate = ime_->UpdateCompositionText(ec, pic_, range.Get(), disp);
                logger::LogFormat(logger::Level::Info, L"UpdateCompositionText returned hr = 0x%08X", hrUpdate);
                action_succeeded_ = SUCCEEDED(hrUpdate);
                if (FAILED(hrUpdate)) {
                    logger::Log(logger::Level::Warning, L"ProcessChar active update failed; keeping existing composition state");
                    SecureEraseString(disp);
                    return hrUpdate;
                }
                SecureEraseString(disp);
            }
        }
        else if (action_ == EditAction::Backspace) {
            logger::Log(logger::Level::Info, L"EditAction::Backspace");
            if (ime_->HasActiveComposition()) {
                bool is_selection_at_composition_end = false;
                ComPtr<ITfRange> comp_range;
                if (SUCCEEDED(ime_->active_composition_->GetRange(comp_range.GetAddressOf())) && comp_range) {
                    BOOL selection_empty = TRUE;
                    if (SUCCEEDED(range->IsEmpty(ec, &selection_empty)) && selection_empty) {
                        LONG comparison = 0;
                        if (SUCCEEDED(range->CompareStart(ec, comp_range.Get(), TF_ANCHOR_END, &comparison)) && comparison == 0) {
                            is_selection_at_composition_end = true;
                        }
                    }
                }
                
                if (!is_selection_at_composition_end && !ime_->IsTelegramProcess()) {
                    logger::Log(logger::Level::Info, L"Backspace: Selection is not at composition end, committing active composition");
                    ime_->EndComposition(ec);
                    
                    BOOL selection_empty = TRUE;
                    if (SUCCEEDED(range->IsEmpty(ec, &selection_empty)) && !selection_empty) {
                        HRESULT hrReplace = range->SetText(ec, 0, L"", 0);
                        logger::LogFormat(logger::Level::Info, L"Backspace cleared selected text, hr = 0x%08X", hrReplace);
                        action_succeeded_ = SUCCEEDED(hrReplace);
                    } else {
                        // If selection was empty but just moved, we should let the host handle the Backspace key event
                        action_succeeded_ = false;
                    }
                    return S_OK;
                }

                ime_->GetEngine().BackspaceDisplayChar();
                std::wstring disp = ime_->GetEngine().GetDisplayString();
                std::wstring raw = ime_->GetEngine().GetRawString();
                logger::LogFormat(logger::Level::Info, L"Backspace: raw_empty = %s, display_length = %zu", raw.empty() ? L"TRUE" : L"FALSE", disp.length());
                HRESULT hrUpdate = S_OK;
                if (raw.empty()) {
                    hrUpdate = ime_->UpdateCompositionText(ec, pic_, range.Get(), L"");
                    ime_->EndComposition(ec);
                } else {
                    hrUpdate = ime_->UpdateCompositionText(ec, pic_, range.Get(), disp);
                }
                action_succeeded_ = SUCCEEDED(hrUpdate);
                SecureEraseString(disp);
                SecureEraseString(raw);
            }
        }
        else if (action_ == EditAction::Commit) {
            logger::LogFormat(logger::Level::Info, L"EditAction::Commit: has_delimiter = %s", ch_ != 0 ? L"TRUE" : L"FALSE");
            ime_->shorthand_host_delimiter_consumed_ = false;
            if (ime_->HasActiveComposition()) {
                if (ch_ == L'\xffff') {
                    logger::Log(logger::Level::Info, L"EditAction::Commit: received sentinel L'\\xffff', clearing composition text first to abort");
                    ComPtr<ITfRange> comp_range;
                    if (SUCCEEDED(ime_->active_composition_->GetRange(comp_range.GetAddressOf())) && comp_range) {
                        comp_range->SetText(ec, 0, L"", 0);
                    }
                } else {
                    const wchar_t transform_delimiter =
                        core::ResolveCommitTransformDelimiter(
                            ch_, host_owned_commit_delimiter_);
                    VietnameseIME::AppliedCompositionTransform transform =
                        ime_->ApplyCompositionCommitTransforms(
                            ec, pic_, transform_delimiter,
                            replay_vk_ == 0);
                    if (ime_->HasPendingShorthandCaretTransaction()) {
                        // Smart Undo assumes the caret remains after the word.
                        // A CURSOR snippet intentionally violates that contract.
                        ime_->ClearLastCommitUndo();
                    } else {
                        ime_->CaptureCommitUndo(
                            ec, pic_, transform.transform_kind,
                            transform.original_text,
                            transform.display_text,
                            transform.committed_range.Get());
                        if (ime_->last_commit_undo_ &&
                            ime_->last_commit_undo_->is_tsf &&
                            IsSameComObject(
                                pic_, ime_->last_commit_undo_->expected_context.Get())) {
                            ime_->last_commit_undo_->committed_with_ascii_space =
                                host_owned_commit_delimiter_ == L' ';
                        }
                    }
                }
                const HRESULT end_hr = ime_->EndComposition(
                    ec, ch_ == L'\xffff');
                logger::LogFormat(logger::Level::Info, L"EndComposition returned hr = 0x%08X", end_hr);
            }
            wchar_t inserted_delimiter = ch_;
            const bool owns_host_delimiter =
                inserted_delimiter == 0 &&
                host_owned_commit_delimiter_ != 0 &&
                ime_->HasPendingShorthandCaretTransaction();
            if (owns_host_delimiter) {
                inserted_delimiter = host_owned_commit_delimiter_;
            }
            if (inserted_delimiter != 0 && inserted_delimiter != L'\xffff') {
                ComPtr<ITfRange> current_range;
                TF_SELECTION current_sel;
                ULONG current_fetched = 0;
                HRESULT hrSel = pic_->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &current_sel, &current_fetched);
                logger::LogFormat(logger::Level::Info, L"Commit GetSelection returned hr = 0x%08X, fetched = %u", hrSel, current_fetched);
                if (SUCCEEDED(hrSel) && current_fetched > 0) {
                    current_range.Attach(current_sel.range);
                    wchar_t delim[2] = { inserted_delimiter, L'\0' };
                    HRESULT hrText = current_range->SetText(ec, 0, delim, 1);
                    logger::LogFormat(logger::Level::Info, L"Commit SetText returned hr = 0x%08X", hrText);
                    if (ime_->last_commit_undo_ && ime_->last_commit_undo_->is_tsf &&
                        IsSameComObject(pic_, ime_->last_commit_undo_->expected_context.Get())) {
                        ime_->last_commit_undo_->committed_with_ascii_space =
                            SUCCEEDED(hrText) && inserted_delimiter == L' ';
                        logger::LogFormat(
                            logger::Level::Info,
                            L"Commit boundary metadata: set_text_hr=0x%08X, ascii_space=%d",
                            hrText,
                            ime_->last_commit_undo_->committed_with_ascii_space ? 1 : 0);
                    }
                    
                    current_range->Collapse(ec, TF_ANCHOR_END);
                    current_sel.range = current_range.Get();
                    current_sel.style.ase = TF_AE_NONE;
                    current_sel.style.fInterimChar = FALSE;
                    HRESULT hrSetSel = pic_->SetSelection(ec, 1, &current_sel);
                    logger::LogFormat(logger::Level::Info, L"Commit SetSelection returned hr = 0x%08X", hrSetSel);
                    if (owns_host_delimiter && SUCCEEDED(hrText)) {
                        // OnKeyDown will eat the physical host-owned key so the
                        // boundary is inserted exactly once at the range end.
                        ime_->shorthand_host_delimiter_consumed_ = true;
                    }
                }
            }
            if (ime_->HasPendingShorthandCaretTransaction()) {
                const auto result =
                    ime_->FinalizeShorthandCaretTransaction(ec, pic_);
                if (result != VietnameseIME::
                        ShorthandCaretTransactionResult::Applied) {
                    ime_->ClearLastCommitUndo();
                }
            }
            if (replay_vk_ != 0) {
                logger::LogFormat(logger::Level::Info, L"EditSession: replaying key 0x%04X after commit", replay_vk_);
                ime_->SendSyntheticNativeKey(replay_vk_);
            }
        }
        else if (action_ == EditAction::ReconvertTest || action_ == EditAction::Reconvert) {
            ResolvedReconversionTarget target;
            hr = ResolveReconversionTarget(ec, range.Get(), &target);
            if (hr == S_OK) {
                const core::InputMethod method = ime_->GetEngine().GetInputMethod();
                if (!core::ShouldAttemptTypedReconversion(target.span, ch_, method)) {
                    logger::LogFormat(logger::Level::Debug,
                                      L"Reconvert key skipped by intent: selection_empty=%s target_length=%zu",
                                      target.span.selection_start == target.span.selection_end ? L"TRUE" : L"FALSE",
                                      target.word.length());
                    SecureEraseString(target.word);
                    return S_OK;
                }
                std::optional<core::ReconversionCandidate> candidate = core::BuildReconversionCandidateWithSelection(
                    target.word,
                    target.span.selection_start - target.span.start,
                    target.span.selection_end - target.span.start,
                    ch_,
                    method);
                logger::LogFormat(logger::Level::Debug, L"Reconvert key target_length=%zu candidate_length=%zu valid=%s",
                                  target.word.length(),
                                  candidate ? candidate->replacement.length() : 0,
                                  candidate ? L"TRUE" : L"FALSE");
                if (candidate) {
                    const std::wstring& new_word = candidate->replacement;
                    result_text_ = new_word;
                    if (action_ == EditAction::ReconvertTest) {
                        is_convertible_ = true;
                    } else {
                        if (ime_->HasActiveComposition()) {
                            logger::Log(
                                logger::Level::Warning,
                                L"Reconvert apply skipped because a composition appeared after the test phase");
                            SecureEraseString(candidate->replacement);
                            SecureEraseString(target.word);
                            return S_OK;
                        }

                        std::wstring raw_keys = core::rules::ReconstructRawKeys(new_word, method);
                        ime_->GetEngine().Clear();
                        for (wchar_t key : raw_keys) {
                            ime_->GetEngine().ProcessKey(key);
                        }

                        const size_t original_selection_start =
                            target.span.selection_start - target.span.start;
                        const size_t original_selection_end =
                            target.span.selection_end - target.span.start;
                        const HRESULT hrComp =
                            ime_->StartComposition(ec, pic_, target.range.Get());
                        const bool started_composition =
                            SUCCEEDED(hrComp) && ime_->HasActiveComposition();

                        HRESULT hrSet = E_FAIL;
                        if (started_composition) {
                            hrSet = ime_->UpdateCompositionText(ec, pic_, target.range.Get(), new_word);
                        } else {
                            // Preserve the existing direct replacement fallback
                            // for hosts that reject TSF compositions.
                            hrSet = target.range->SetText(ec, 0, new_word.c_str(), static_cast<LONG>(new_word.length()));
                        }

                        const HRESULT hrSelection = SUCCEEDED(hrSet)
                            ? RestoreReconversionSelectionAt(
                                  ec, pic_, target.range.Get(),
                                  candidate->selection_start,
                                  candidate->selection_end)
                            : hrSet;
                        is_convertible_ =
                            SUCCEEDED(hrSet) && SUCCEEDED(hrSelection);

                        if (!is_convertible_) {
                            // SetText and SetSelection form one logical edit.
                            // Restore the original word/caret before allowing
                            // the caller to fall back to ordinary key input.
                            const HRESULT hrRollbackText = target.range->SetText(
                                ec, 0, target.word.c_str(),
                                static_cast<LONG>(target.word.length()));
                            HRESULT hrEnd = S_OK;
                            if (started_composition && ime_->HasActiveComposition()) {
                                hrEnd = ime_->EndComposition(ec, false);
                            }
                            const HRESULT hrRollbackSelection =
                                SUCCEEDED(hrRollbackText) && SUCCEEDED(hrEnd)
                                ? RestoreReconversionSelectionAt(
                                      ec, pic_, target.range.Get(),
                                      original_selection_start,
                                      original_selection_end)
                                : E_FAIL;
                            const bool rolled_back =
                                SUCCEEDED(hrRollbackText) &&
                                SUCCEEDED(hrEnd) &&
                                SUCCEEDED(hrRollbackSelection);
                            logger::LogFormat(
                                logger::Level::Warning,
                                L"Reconvert apply failed: start=0x%08X set=0x%08X selection=0x%08X rollback_text=0x%08X rollback_end=0x%08X rollback_selection=0x%08X rolled_back=%d",
                                hrComp, hrSet, hrSelection, hrRollbackText,
                                hrEnd, hrRollbackSelection,
                                rolled_back ? 1 : 0);

                            ime_->GetEngine().Clear();
                            // If rollback itself is incomplete, consume the
                            // key rather than risk applying it twice to text
                            // whose host state is now uncertain.
                            is_convertible_ = !rolled_back;
                        }
                        SecureEraseString(raw_keys);
                    }
                }
                if (candidate) {
                    SecureEraseString(candidate->replacement);
                }
                SecureEraseString(target.word);
            }
        }
        else if (action_ == EditAction::QueryReconversionRange ||
                 action_ == EditAction::ReadReconversionText ||
                 action_ == EditAction::StartReconversion) {
            ResolvedReconversionTarget target;
            hr = ResolveReconversionTarget(ec, range.Get(), &target);
            if (hr == S_OK && IsReconvertableWord(target.word, ime_->GetEngine().GetInputMethod())) {
                if (action_ == EditAction::QueryReconversionRange) {
                    result_range_ = target.range;
                    is_convertible_ = true;
                } else if (action_ == EditAction::ReadReconversionText) {
                    result_text_ = target.word;
                    is_convertible_ = true;
                } else if (!ime_->HasActiveComposition()) {
                    core::rules::ReconversionSpan restore_span = target.span;
                    TF_SELECTION current_selection{};
                    ULONG current_fetched = 0;
                    if (SUCCEEDED(pic_->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &current_selection, &current_fetched)) &&
                        current_fetched > 0) {
                        ComPtr<ITfRange> current_range;
                        current_range.Attach(current_selection.range);
                        ResolvedReconversionTarget current_target;
                        if (ResolveReconversionTarget(ec, current_range.Get(), &current_target) == S_OK) {
                            BOOL equal_start = FALSE;
                            BOOL equal_end = FALSE;
                            if (SUCCEEDED(target.range->IsEqualStart(ec, current_target.range.Get(), TF_ANCHOR_START, &equal_start)) &&
                                SUCCEEDED(target.range->IsEqualEnd(ec, current_target.range.Get(), TF_ANCHOR_END, &equal_end)) &&
                                equal_start && equal_end) {
                                restore_span.selection_start = target.span.start +
                                    (current_target.span.selection_start - current_target.span.start);
                                restore_span.selection_end = target.span.start +
                                    (current_target.span.selection_end - current_target.span.start);
                            }
                            SecureEraseString(current_target.word);
                        }
                    }
                    std::wstring raw = core::rules::ReconstructRawKeys(target.word, ime_->GetEngine().GetInputMethod());
                    ime_->GetEngine().Clear();
                    for (wchar_t key : raw) ime_->GetEngine().ProcessKey(key);
                    std::wstring display = ime_->GetEngine().GetDisplayString();
                    if (display == target.word) {
                        HRESULT hrComp = ime_->StartComposition(ec, pic_, target.range.Get());
                        HRESULT hrUpdate = SUCCEEDED(hrComp)
                            ? ime_->UpdateCompositionText(ec, pic_, target.range.Get(), display)
                            : hrComp;
                        HRESULT hrSelection = SUCCEEDED(hrUpdate)
                            ? RestoreReconversionSelection(ec, pic_, target.range.Get(), restore_span, display.length())
                            : hrUpdate;
                        if (SUCCEEDED(hrComp) && FAILED(hrUpdate) && ime_->HasActiveComposition()) {
                            ime_->EndComposition(ec);
                        }
                        is_convertible_ = SUCCEEDED(hrComp) && SUCCEEDED(hrUpdate) && SUCCEEDED(hrSelection);
                    } else {
                        ime_->GetEngine().Clear();
                    }
                    SecureEraseString(raw);
                    SecureEraseString(display);
                }
                SecureEraseString(target.word);
            }
        }
        else if (action_ == EditAction::ReadExcelFormulaPrefix) {
            action_succeeded_ = true;
            is_convertible_ = false;

            ComPtr<ITfRange> check_range;
            if (SUCCEEDED(range->Clone(check_range.GetAddressOf())) && check_range) {
                if (SUCCEEDED(check_range->Collapse(ec, TF_ANCHOR_START))) {
                    LONG shifted = 0;
                    if (SUCCEEDED(check_range->ShiftStart(ec, -1024, &shifted, nullptr))) {
                        LONG num_chars = -shifted;
                        if (num_chars > 0) {
                            std::wstring text_buf(static_cast<size_t>(num_chars), L'\0');
                            ULONG fetched_chars = 0;
                            if (SUCCEEDED(check_range->GetText(ec, 0, &text_buf[0], static_cast<ULONG>(text_buf.size()), &fetched_chars))) {
                                if (fetched_chars < text_buf.size()) {
                                    text_buf.resize(fetched_chars);
                                }
                                result_text_ = std::move(text_buf);
                                is_convertible_ = true;
                            }
                        } else if (num_chars == 0) {
                            result_text_.clear();
                            is_convertible_ = true;
                        }
                    }
                }
            }
        }
        else if (action_ == EditAction::CommitEscRaw) {
            if (ime_->HasActiveComposition()) {
                ComPtr<ITfRange> commit_range;
                if (SUCCEEDED(ime_->active_composition_->GetRange(commit_range.GetAddressOf())) && commit_range) {
                    ime_->UpdateCompositionText(ec, pic_, commit_range.Get(), str_);
                }
                ime_->EndComposition(ec);
            }
            action_succeeded_ = true;
        }
        else if (action_ == EditAction::DirectRevertRaw) {
            logger::Log(logger::Level::Info, L"EditAction::DirectRevertRaw");
            if (ime_->HasDirectInlineState()) {
                TF_SELECTION direct_selection{};
                ULONG direct_fetched = 0;
                if (SUCCEEDED(pic_->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &direct_selection, &direct_fetched)) && direct_fetched > 0 && direct_selection.range) {
                    HRESULT hrDirect = ime_->ReplaceDirectInlineText(ec, pic_, direct_selection.range, str_);
                    ime_->ResetDirectInlineState();
                    action_succeeded_ = SUCCEEDED(hrDirect);
                    direct_selection.range->Release();
                } else {
                    logger::Log(logger::Level::Warning, L"DirectRevertRaw: GetSelection failed");
                    action_succeeded_ = false;
                }
            } else {
                action_succeeded_ = true;
            }
        }
        
        return S_OK;
    }

private:
    VietnameseIME* ime_;
    ITfContext* pic_;
    EditAction action_;
    wchar_t ch_;
    ComPtr<ITfRange> requested_range_;
    std::atomic<ULONG> ref_count_;
    bool is_convertible_ = false;
    bool action_succeeded_ = false;
    bool action_executed_ = false;
    BrowserTextInputMode browser_text_input_mode_ =
        BrowserTextInputMode::NativeComposition;
    std::wstring result_text_;
    ComPtr<ITfRange> result_range_;
    std::wstring str_;
    WORD replay_vk_ = 0;
    wchar_t host_owned_commit_delimiter_ = L'\0';
};


// Creator function used by ClassFactory
HRESULT CreateInstance_VietnameseIME(IUnknown* outer, REFIID riid, void** ppv) {
    if (outer) return CLASS_E_NOAGGREGATION;
    
    VietnameseIME* ime = new (std::nothrow) VietnameseIME();
    if (!ime) return E_OUTOFMEMORY;
    
    HRESULT hr = ime->QueryInterface(riid, ppv);
    ime->Release(); // Balance the constructor ref count (starts at 1)
    return hr;
}

VietnameseIME::VietnameseIME() noexcept
    : registry_thread_(nullptr),
      registry_shutdown_event_(nullptr),
      registry_watch_event_(nullptr),
      config_changed_(false) {
    ClassFactory::IncrementActiveObjects();
    browser_url_pending_token_.reserve(
        core::kMaxRawKeysPerComposition + 1);
    browser_url_pending_replacement_.reserve(
        core::kMaxRawKeysPerComposition + 1);
    wchar_t path[MAX_PATH] = {0};
    if (::GetModuleFileNameW(nullptr, path, MAX_PATH) != 0) {
        host_process_name_ = NormalizeProcessName(path);
    }
}

VietnameseIME::~VietnameseIME() noexcept {
    ClearShorthandCaretTransaction();
    ClearPendingShorthandSelection();
    ClearBrowserUrlPendingReconversion();
    ClearLastCommitUndo();
    ClearTelegramRawReplay();
    ClassFactory::DecrementActiveObjects();
}

// IUnknown Implementation
STDMETHODIMP VietnameseIME::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;

    if (riid == IID_IUnknown || riid == IID_ITfTextInputProcessor) {
        *ppv = static_cast<ITfTextInputProcessor*>(this);
    } else if (riid == IID_ITfTextInputProcessorEx) {
        *ppv = static_cast<ITfTextInputProcessorEx*>(this);
    } else if (riid == IID_ITfKeyEventSink) {
        *ppv = static_cast<ITfKeyEventSink*>(this);
    } else if (riid == IID_ITfThreadMgrEventSink) {
        *ppv = static_cast<ITfThreadMgrEventSink*>(this);
    } else if (riid == IID_ITfDisplayAttributeProvider) {
        *ppv = static_cast<ITfDisplayAttributeProvider*>(this);
    } else if (riid == IID_ITfCompositionSink) {
        *ppv = static_cast<ITfCompositionSink*>(this);
    } else if (riid == IID_ITfFunctionProvider) {
        *ppv = static_cast<ITfFunctionProvider*>(this);
    } else if (riid == IID_ITfFnReconversion || riid == IID_ITfFunction) {
        *ppv = static_cast<ITfFnReconversion*>(this);
    } else if (riid == IID_ITfMouseSink) {
        *ppv = static_cast<ITfMouseSink*>(this);
    } else if (riid == IID_ITfTextEditSink) {
        *ppv = static_cast<ITfTextEditSink*>(this);
    } else {
        return E_NOINTERFACE;
    }

    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) VietnameseIME::AddRef() {
    return AddComRef(ref_count_);
}

STDMETHODIMP_(ULONG) VietnameseIME::Release() {
    const ULONG count = ReleaseComRef(ref_count_);
    if (count == 0) {
        delete this;
    }
    return count;
}

// ITfTextInputProcessor Implementation
STDMETHODIMP VietnameseIME::Activate(ITfThreadMgr* ptm, TfClientId tid) {
    return ActivateEx(ptm, tid, 0);
}

STDMETHODIMP VietnameseIME::Deactivate() {
    logger::Log(logger::Level::Info, L"VietnameseIME::Deactivate called.");
    hotkey_toggle_state_.Reset();
    const bool activation_ready = activation_ready_for_auto_exclude_;
    activation_ready_for_auto_exclude_ = false;
    current_app_explicitly_disabled_ = false;
    ResetBrowserInputScopeCheck();
    ResetDirectInlineState();
    ClearShorthandCaretTransaction();
    ClearLastCommitUndo();
    ClearTelegramRawReplay();
    if (!is_active_) return S_OK;
    
    HWND fg_hwnd = ::GetForegroundWindow();
    DWORD fg_pid = 0;
    const DWORD fg_tid = fg_hwnd
        ? ::GetWindowThreadProcessId(fg_hwnd, &fg_pid)
        : 0;
    const std::wstring process_name = host_process_name_.empty()
        ? GetFocusedProcessName()
        : host_process_name_;
    if (ShouldLearnAutomaticOffOnDeactivate(
            enable_app_input_profiles_,
            enable_auto_app_input_profiles_,
            activation_ready,
            IsValidAppProfileProcessName(process_name),
            fg_pid == ::GetCurrentProcessId(),
            fg_tid == ::GetCurrentThreadId())) {
        IMEConfig deactivate_config = LoadConfigFromRegistry();
        if (LearnAutomaticOffOnDeactivate(
                deactivate_config, process_name)) {
            if (SaveConfigToRegistry(deactivate_config)) {
                logger::Log(
                    logger::Level::Info,
                    L"Deactivate learned an Automatic Off profile");
            } else {
                logger::Log(
                    logger::Level::Warning,
                    L"Deactivate could not persist an Automatic Off profile");
            }
        }
    }
    
    // Shut down registry watcher thread
    if (registry_thread_) {
        if (registry_shutdown_event_) {
            SetEvent(registry_shutdown_event_);
        }
        // The worker owns an IME reference until it exits. Wait for that
        // handoff before closing its events; timing out and freeing this object
        // would leave the worker with a dangling pointer.
        WaitForSingleObject(registry_thread_, INFINITE);
        CloseHandle(registry_thread_);
        registry_thread_ = nullptr;
    }
    if (registry_shutdown_event_) {
        CloseHandle(registry_shutdown_event_);
        registry_shutdown_event_ = nullptr;
    }
    if (registry_watch_event_) {
        CloseHandle(registry_watch_event_);
        registry_watch_event_ = nullptr;
    }
    
    if (mouse_cookie_ != 0 && active_composition_) {
        ComPtr<ITfRange> comp_range;
        if (SUCCEEDED(active_composition_->GetRange(comp_range.GetAddressOf())) && comp_range) {
            ComPtr<ITfContext> context;
            if (SUCCEEDED(comp_range->GetContext(context.GetAddressOf())) && context) {
                ComPtr<ITfMouseTracker> mouse_tracker;
                if (SUCCEEDED(context->QueryInterface(IID_ITfMouseTracker, reinterpret_cast<void**>(mouse_tracker.GetAddressOf())))) {
                    mouse_tracker->UnadviseMouseSink(mouse_cookie_);
                }
            }
        }
        mouse_cookie_ = 0;
    }
    
    ClearSensitiveState(true);
    display_attribute_atom_ = 0;

    for (HWND hwnd : subclassed_hwnds_) {
        if (::IsWindow(hwnd)) {
            ::RemoveWindowSubclass(hwnd, InkscapeSubclassProc, 0x1991);
            logger::LogFormat(logger::Level::Info, L"Removed subclass from Inkscape window 0x%p", hwnd);
        }
    }
    subclassed_hwnds_.clear();

    if (active_subclassed_hwnd_ != nullptr) {
        if (::IsWindow(active_subclassed_hwnd_)) {
            ::RemoveWindowSubclass(active_subclassed_hwnd_, MouseHookSubclassProc, 0x2026);
            logger::LogFormat(logger::Level::Info, L"Deactivate: Removed MouseHookSubclassProc subclass from HWND 0x%p", active_subclassed_hwnd_);
        }
        active_subclassed_hwnd_ = nullptr;
    }
    if (active_subclassed_root_hwnd_ != nullptr) {
        if (::IsWindow(active_subclassed_root_hwnd_)) {
            ::RemoveWindowSubclass(active_subclassed_root_hwnd_, MouseHookSubclassProc, 0x2027);
            logger::LogFormat(logger::Level::Info, L"Deactivate: Removed MouseHookSubclassProc subclass from Root HWND 0x%p", active_subclassed_root_hwnd_);
        }
        active_subclassed_root_hwnd_ = nullptr;
    }

    RemoveThreadCompositionHooks();

    UnadviseSelectionSink();

    UninitThreadMgrEventSink();
    UninitKeySink();
    
    ComPtr<ITfSourceSingle> source_single;
    if (SUCCEEDED(thread_mgr_.As(source_single))) {
        source_single->UnadviseSingleSink(client_id_, IID_ITfFunctionProvider);
    }

    thread_mgr_.Reset();
    client_id_ = 0;
    is_active_ = false;
    
    logger::Log(logger::Level::Info, L"VietnameseIME::Deactivate succeeded.");
    return S_OK;
}

// ITfTextInputProcessorEx Implementation
STDMETHODIMP VietnameseIME::ActivateEx(ITfThreadMgr* ptm, TfClientId tid, [[maybe_unused]] DWORD dwFlags) {
    logger::LogFormat(logger::Level::Info, L"VietnameseIME::ActivateEx called. tid = %d, dwFlags = %u", tid, dwFlags);
    if (is_active_) return S_OK;
    if (!ptm) return E_INVALIDARG;
    activation_ready_for_auto_exclude_ = false;

    thread_mgr_ = ComPtr<ITfThreadMgr>(ptm);
    client_id_ = tid;
    is_active_ = true;

    HRESULT hr = InitKeySink();
    if (FAILED(hr)) {
        logger::LogFormat(logger::Level::Error, L"InitKeySink failed. hr = 0x%08X", hr);
        Deactivate();
        return hr;
    }

    hr = InitThreadMgrEventSink();
    if (FAILED(hr)) {
        logger::LogFormat(logger::Level::Error, L"InitThreadMgrEventSink failed. hr = 0x%08X", hr);
        Deactivate();
        return hr;
    }

    ComPtr<ITfSourceSingle> source_single;
    if (SUCCEEDED(thread_mgr_.As(source_single))) {
        source_single->AdviseSingleSink(client_id_, IID_ITfFunctionProvider, static_cast<ITfFunctionProvider*>(this));
    }

    // Register Display Attribute GUID to get an atom
    ComPtr<ITfCategoryMgr> category_mgr;
    if (SUCCEEDED(CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER, IID_ITfCategoryMgr, reinterpret_cast<void**>(category_mgr.GetAddressOf())))) {
        category_mgr->RegisterGUID(GUID_VietnameseDisplayAttribute, &display_attribute_atom_);
    }

    IMEConfig initial_config = LoadConfigFromRegistry();
    HWND fg_hwnd = ::GetForegroundWindow();
    DWORD fg_pid = 0;
    const DWORD fg_tid = fg_hwnd
        ? ::GetWindowThreadProcessId(fg_hwnd, &fg_pid)
        : 0;
    const std::wstring process_name = host_process_name_.empty()
        ? GetFocusedProcessName()
        : host_process_name_;
    if (fg_pid == ::GetCurrentProcessId() &&
        fg_tid == ::GetCurrentThreadId() &&
        RestoreAutomaticAppInputProfileOnActivate(
            initial_config, process_name)) {
        if (SaveConfigToRegistry(initial_config)) {
            logger::Log(
                logger::Level::Info,
                L"Activate restored an Automatic Off profile");
        } else {
            logger::Log(
                logger::Level::Warning,
                L"Activate could not persist a restored Automatic Off profile");
        }
    }

    // Load initial config
    ReloadConfig();

    // Set up registry monitoring (skip on Secure Desktop)
    if (!logger::IsSecureDesktop()) {
        registry_shutdown_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        registry_watch_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (registry_shutdown_event_ && registry_watch_event_) {
            // Hold the IME alive for the raw Win32 worker thread. The worker
            // releases this reference on every exit path.
            AddRef();
            registry_thread_ = CreateThread(nullptr, 0, RegistryWatchThreadProc, this, 0, nullptr);
            if (!registry_thread_) {
                Release();
                logger::Log(logger::Level::Error, L"ActivateEx: Failed to create Registry watch thread");
            }
        } else {
            logger::Log(logger::Level::Error, L"ActivateEx: Failed to create Registry watch events");
        }
    }

    logger::Log(logger::Level::Info, L"VietnameseIME::ActivateEx succeeded.");
    activation_ready_for_auto_exclude_ = true;
    EnsureInkscapeSubclassed();
    return S_OK;
}

// ITfKeyEventSink Implementation
STDMETHODIMP VietnameseIME::OnSetFocus(BOOL fForeground) {
    logger::LogFormat(logger::Level::Info, L"OnSetFocus called: fForeground = %s", fForeground ? L"TRUE" : L"FALSE");
    if (fForeground) {
        EnsureInkscapeSubclassed();
        if (IsBrowserProcess()) {
            ClearSensitiveState(false);
            is_password_field_ = false;
            MarkBrowserInputScopeCheckPending(nullptr);
        }
    }
    if (!fForeground && telegram_boundary_resume_state_.IsPending()) {
        ClearLastCommitUndo();
    }
    if (!fForeground && telegram_raw_replay_state_.IsPending()) {
        ClearTelegramRawReplay();
    }
    if (!fForeground) {
        ResetBrowserInputScopeCheck();
        if (IsExcelApp()) {
            ResetExcelFormulaSession(L"foreground_lost");
        }
    }
    if (!fForeground && thread_mgr_) {
        if (!IsBrowserProcess()) {
            ComPtr<ITfDocumentMgr> doc_mgr;
            if (SUCCEEDED(thread_mgr_->GetFocus(doc_mgr.GetAddressOf())) && doc_mgr) {
                ComPtr<ITfContext> context;
                if (SUCCEEDED(doc_mgr->GetTop(context.GetAddressOf())) && context) {
                    CommitCompositionSync(context.Get());
                }
            }
        }
        ClearSensitiveState(false);
    }
    return S_OK;
}

STDMETHODIMP VietnameseIME::OnTestKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) {
    if (!pfEaten) return E_INVALIDARG;
    ClearBrowserUrlPendingReconversion();
    browser_input_scope_test_gate_attempted_ = false;

    const ULONG_PTR extra_info = static_cast<ULONG_PTR>(::GetMessageExtraInfo());
    const bool lost_marker_selection_key =
        telegram_synthetic_selection_suppression_.ShouldPassThrough(
            telegram_boundary_resume_state_.phase,
            GetTickCount64(), wParam);
    const bool lost_marker_raw_replay_key =
        !IsTelegramRawReplayMarker(extra_info) &&
        telegram_raw_replay_state_.phase ==
            TelegramRawReplayPhase::Dispatching &&
        IsTelegramRawReplayVirtualKey(
            wParam, telegram_raw_replay_plan_);
    if (IsTelegramNativeTransactionMarker(extra_info) ||
        lost_marker_selection_key ||
        extra_info == static_cast<ULONG_PTR>(0xDEADC0DEu)) {
        if (lost_marker_selection_key &&
            !IsTelegramNativeTransactionMarker(extra_info)) {
            logger::Log(logger::Level::Info,
                        L"Telegram synthetic selection key passed through without marker");
        }
        *pfEaten = FALSE;
        return S_OK;
    }

    CheckAndReloadConfig();
    const bool hotkey_claimed =
        ShouldClaimHotkeyTestEvent(wParam, true);
    if (!hotkey_claimed &&
        hotkey_mode_ <= static_cast<DWORD>(HotkeyMode::AltZ)) {
        hotkey_toggle_state_.ObservePassThroughEvent(
            static_cast<HotkeyMode>(hotkey_mode_),
            ClassifyHotkeyKey(wParam), true);
    }
    if ((lParam & (1 << 28)) != 0 && !hotkey_claimed) {
        *pfEaten = FALSE;
        return S_OK;
    }

    if (telegram_raw_replay_state_.IsPending() &&
        !IsTelegramRawReplayMarker(extra_info) &&
        !lost_marker_raw_replay_key) {
        logger::Log(
            logger::Level::Info,
            L"Telegram raw replay canceled by an intervening real key");
        ClearTelegramRawReplay();
    }

    if (last_commit_undo_ && ShouldCaptureSmartUndo(*last_commit_undo_) &&
        ShouldInvalidateCommitUndoOnTestKeyDown(
            wParam, IsModifierKey(wParam),
            telegram_boundary_resume_state_.IsPending(),
            IsTelegramRawReplayMarker(extra_info) ||
                lost_marker_raw_replay_key)) {
        logger::Log(logger::Level::Info,
                    L"Smart Undo invalidated by an intervening key");
        ClearLastCommitUndo();
    }

    if (last_commit_undo_ && last_commit_undo_->is_tsf &&
        IsTelegramProcess() &&
        ShouldInvalidateCommitUndoOnTestKeyDown(
            wParam, IsModifierKey(wParam),
            telegram_boundary_resume_state_.IsPending(),
            IsTelegramRawReplayMarker(extra_info) ||
                lost_marker_raw_replay_key)) {
        logger::Log(logger::Level::Info,
                    L"Telegram commit undo invalidated by an intervening key");
        ClearLastCommitUndo();
    }

    if (telegram_boundary_resume_state_.IsPending()) {
        const bool canceled_safely =
            CancelTelegramNativeSelectionForRealKey(pic);
        telegram_swallow_real_keydown_ = !canceled_safely;
        logger::Log(logger::Level::Warning,
                    canceled_safely
                        ? L"Telegram boundary resume canceled safely by a real key"
                        : L"Telegram boundary resume cancellation failed; real key consumed");
        *pfEaten = canceled_safely ? FALSE : TRUE;
        return S_OK;
    }

    if (hotkey_claimed) {
        *pfEaten = TRUE;
        return S_OK;
    }

    if (HasTextShortcutModifier()) {
        if (active_composition_) {
            logger::Log(logger::Level::Info, L"OnTestKeyDown: Shortcut modifier detected, committing active composition");
            CommitCompositionSync(pic);
        }
        *pfEaten = FALSE;
        return S_OK;
    }

    EnsureInkscapeSubclassed();

    const bool browser_text_key = IsBrowserProcess() &&
        IsValidCompositionKey(wParam, engine_.GetInputMethod());
    if (ShouldRequestBrowserInputScopeCheck(
            IsBrowserProcess(), browser_text_key,
            browser_input_scope_check_pending_,
            IsBrowserInputScopeContextCurrent(pic), false)) {
        browser_input_scope_test_gate_attempted_ = true;
        if (!EnsureBrowserInputScopeCheckedForTextKey(pic)) {
            *pfEaten = FALSE;
            return S_OK;
        }
    }

    if (HandleBrowserUrlTestKeyDown(
            pic, wParam, lParam, pfEaten)) {
        return S_OK;
    }

    const bool no_modifier = !HasTextShortcutModifier() && !IsKeyDown(VK_SHIFT);
    if (wParam == VK_BACK && !HasActiveComposition() && last_commit_undo_ && no_modifier &&
        !(IsExcelApp() && excel_formula_state_ != core::ExcelFormulaSessionState::Idle)) {
        const HWND focus_hwnd = GetBestFocusWindow();
        const bool focus_matches = focus_hwnd != nullptr && focus_hwnd == last_commit_undo_->hwnd;
        const bool same_entry_context = !last_commit_undo_->is_tsf ||
            IsSameComObject(pic, last_commit_undo_->expected_context.Get());
        const bool telegram_tsf = last_commit_undo_->is_tsf && IsTelegramProcess();
        const bool same_tsf_context = telegram_tsf &&
                                      IsSameComObject(pic, last_commit_undo_->expected_context.Get());
        const bool safe_context = typing_mode_ == 0 &&
                                   !IsSecureInputContext() &&
                                   !IsCurrentAppBlocked(pic) &&
                                   !IsBuiltInNativeBypassProcess(GetFocusedProcessName());
        const bool smart_undo_host_supported = last_commit_undo_->is_tsf ||
            ClassNameEquals(last_commit_undo_->hwnd, L"Edit") ||
            ClassNameEquals(last_commit_undo_->hwnd, L"Scintilla");
        if (ShouldRouteSmartUndoBackspace(
                *last_commit_undo_, enable_smart_undo_, GetTickCount64(),
                false, no_modifier, focus_matches, same_entry_context,
                true, IsSecureInputContext(),
                safe_context && smart_undo_host_supported)) {
            *pfEaten = TRUE;
            return S_OK;
        }
        if (ShouldRouteTelegramNativeBoundaryBackspace(
                *last_commit_undo_, GetTickCount64(), false, no_modifier,
                telegram_tsf, same_tsf_context, safe_context,
                static_cast<bool>(last_commit_undo_->committed_text_range))) {
            *pfEaten = TRUE;
            return S_OK;
        }

        const bool host_supported = safe_context &&
            !last_commit_undo_->is_tsf &&
            IsNotepadPlusPlusDirectInlineFocused();
        if (ShouldRouteCommitUndoBackspace(*last_commit_undo_, GetTickCount64(), false, no_modifier,
                                           focus_matches, host_supported,
                                           CommitUndoFocusMode::ExactWindow, false)) {
            *pfEaten = TRUE;
            return S_OK;
        }
    }

    if (wParam == VK_ESCAPE) {
        if (IsConsoleProcess()) {
            std::wstring raw_keys = engine_.GetRawString();
            std::wstring display_text = engine_.GetDisplayString();
            size_t inline_len = direct_inline_display_length_;
            bool has_comp = (active_composition_.Get() != nullptr);
            
            if (has_comp || inline_len > 0) {
                bool needs_revert = (raw_keys != display_text);
                bool is_vim_or_ssh = false;
                HWND hwnd = GetBestFocusWindow();
                if (hwnd) {
                    wchar_t title[512] = {0};
                    if (::GetWindowTextW(hwnd, title, 511) > 0) {
                        std::wstring title_str(title);
                        for (wchar_t& c : title_str) {
                            if (c >= L'A' && c <= L'Z') {
                                c = c - L'A' + L'a';
                            }
                        }
                        if (title_str.find(L"vim") != std::wstring::npos ||
                            title_str.find(L"vi") != std::wstring::npos ||
                            title_str.find(L"ssh") != std::wstring::npos ||
                            title_str.find(L"nano") != std::wstring::npos ||
                            title_str.find(L"tmux") != std::wstring::npos ||
                            title_str.find(L"emacs") != std::wstring::npos ||
                            title_str.find(L"wsl") != std::wstring::npos ||
                            title_str.find(L"bash") != std::wstring::npos) {
                            is_vim_or_ssh = true;
                        }
                    }
                }
                
                if (has_comp) {
                    *pfEaten = TRUE;
                    logger::Log(logger::Level::Info, L"OnTestKeyDown: Esc in console, has_comp=true -> eating");
                    return S_OK;
                } else {
                    if (needs_revert) {
                        *pfEaten = TRUE;
                        logger::Log(logger::Level::Info, L"OnTestKeyDown: Esc in console, needs_revert=true -> eating");
                        return S_OK;
                    } else {
                        if (is_vim_or_ssh) {
                            *pfEaten = TRUE;
                            logger::Log(logger::Level::Info, L"OnTestKeyDown: Esc in console (Vim/SSH), needs_revert=false -> eating");
                            return S_OK;
                        } else {
                            *pfEaten = FALSE;
                            logger::Log(logger::Level::Info, L"OnTestKeyDown: Esc in console, needs_revert=false -> passing through");
                            return S_OK;
                        }
                    }
                }
            } else {
                *pfEaten = FALSE;
                logger::Log(logger::Level::Info, L"OnTestKeyDown: Esc in console, no active inline state -> passing through");
                return S_OK;
            }
        } else {
            if (!IsDirectCommitApp() && (active_composition_.Get() != nullptr || HasDirectInlineState() || last_commit_undo_)) {
                *pfEaten = TRUE;
                logger::Log(logger::Level::Info, L"OnTestKeyDown: Esc in GUI app, active composition, inline state or last_commit_undo -> eating");
                return S_OK;
            }
        }
    }

    if (IsInkscapeApp()) {
        if (wParam == VK_SPACE || wParam == VK_RETURN) {
            if (active_composition_) {
                last_inkscape_commit_vk_ = wParam;
                last_inkscape_commit_time_ = ::GetTickCount64();
            }
        }
    }

    if (!HasActiveComposition() && !HasDirectInlineState() &&
        enable_shorthand_ && !IsExcelApp() &&
        !HasTextShortcutModifier() &&
        IsValidCompositionKey(wParam, engine_.GetInputMethod())) {
        const wchar_t first_char = TranslateKey(wParam, lParam);
        RefreshShorthandRulesIfChanged();
        const bool starts_selection_shorthand =
            first_char != 0 &&
            HasSelectionShorthandStartingWith(first_char);
        const ShorthandSelectionCapturePlan capture_plan =
            PlanShorthandSelectionCapture(
                starts_selection_shorthand,
                pending_shorthand_selection_.has_value());
        if (capture_plan == ShorthandSelectionCapturePlan::Clear) {
            ClearPendingShorthandSelection();
        } else if (capture_plan ==
                   ShorthandSelectionCapturePlan::Capture) {
            ComPtr<EditSession> selection_session;
            selection_session.Attach(new (std::nothrow) EditSession(
                this, pic, EditAction::CaptureShorthandSelection,
                first_char));
            HRESULT session_hr = E_FAIL;
            const HRESULT request_hr = selection_session
                ? pic->RequestEditSession(
                      client_id_, selection_session.Get(),
                      TF_ES_SYNC | TF_ES_READ, &session_hr)
                : E_OUTOFMEMORY;
            bool captured =
                SUCCEEDED(request_hr) && SUCCEEDED(session_hr) &&
                selection_session &&
                selection_session->action_succeeded();
            if (!captured) {
                captured = CaptureFocusedWin32ShorthandSelection(
                    pic, first_char);
            }
            if (!captured) {
                ClearPendingShorthandSelection();
                logger::LogFormat(
                    logger::Level::Warning,
                    L"Shorthand selection pre-capture failed: request=0x%08X, session=0x%08X",
                    request_hr, session_hr);
            }
        }
    }

    if (IsExcelApp()) {
        logger::LogFormat(logger::Level::Info, L"OnTestKeyDown (Excel): vk=0x%02X, state=%d, has_comp=%s",
                          static_cast<unsigned int>(wParam), static_cast<int>(excel_formula_state_),
                          HasActiveComposition() ? L"TRUE" : L"FALSE");
    }

    PrepareExcelFormulaSession(pic, wParam, lParam);
    KeyDecision decision = MakeKeyDecision(pic, wParam, lParam);
    if (decision.action == KeyAction::Reconvert) {
        decision.eat = TryReconversion(pic, decision.ch, false);
        if (!decision.eat) {
            if (decision.fallback_to_direct_process_char) {
                decision.eat = true;
                decision.action = KeyAction::DirectProcessChar;
            } else if (decision.fallback_to_process_char) {
                decision.eat = true;
                decision.action = KeyAction::ProcessChar;
            }
        }
    } else if (decision.action == KeyAction::ExplorerEditReconvert) {
        decision.eat = TryExplorerEditReconversion(decision.ch, false);
        if (!decision.eat && decision.fallback_to_direct_process_char) {
            decision.eat = true;
            decision.action = KeyAction::DirectProcessChar;
        }
    }

    if (!decision.eat && decision.clear_sensitive_before_host && !decision.commit_existing_before_host) {
        ClearSensitiveState(false);
    }

    // For native keys such as Enter/Tab with an active composition, return TRUE
    // here so TSF will call OnKeyDown and let us finalize the composition first.
    // OnKeyDown can still return pfEaten=FALSE so the host receives the key.
    *pfEaten = (decision.eat || decision.commit_existing_before_host) ? TRUE : FALSE;

    if (!*pfEaten && IsExcelApp()) {
        ObserveExcelNativeChar(pic, wParam, lParam, L"test_key_observation");
    }

    logger::LogFormat(logger::Level::Debug, L"OnTestKeyDown: action = %d, eaten = %s",
                      static_cast<int>(decision.action), *pfEaten ? L"TRUE" : L"FALSE");

    return S_OK;
}

STDMETHODIMP VietnameseIME::OnKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) {
    if (!pfEaten) return E_INVALIDARG;

    const ULONG_PTR extra_info = static_cast<ULONG_PTR>(::GetMessageExtraInfo());
    const bool lost_marker_selection_key =
        telegram_synthetic_selection_suppression_.ShouldPassThrough(
            telegram_boundary_resume_state_.phase,
            GetTickCount64(), wParam);
    const bool lost_marker_raw_replay_key =
        !IsTelegramRawReplayMarker(extra_info) &&
        telegram_raw_replay_state_.phase ==
            TelegramRawReplayPhase::Dispatching &&
        IsTelegramRawReplayVirtualKey(
            wParam, telegram_raw_replay_plan_);
    if (IsTelegramNativeTransactionMarker(extra_info) ||
        lost_marker_selection_key ||
        extra_info == static_cast<ULONG_PTR>(0xDEADC0DEu)) {
        if (lost_marker_selection_key &&
            !IsTelegramNativeTransactionMarker(extra_info)) {
            logger::Log(logger::Level::Info,
                        L"Telegram synthetic selection keydown passed through without marker");
        }
        *pfEaten = FALSE;
        return S_OK;
    }

    CheckAndReloadConfig();
    const bool hotkey_claimed =
        ShouldClaimHotkeyTestEvent(wParam, true);
    if ((lParam & (1 << 28)) != 0 && !hotkey_claimed) {
        *pfEaten = FALSE;
        return S_OK;
    }

    if (telegram_raw_replay_state_.IsPending() &&
        !IsTelegramRawReplayMarker(extra_info) &&
        !lost_marker_raw_replay_key) {
        logger::Log(
            logger::Level::Info,
            L"Telegram raw replay canceled by an OnKeyDown race");
        ClearTelegramRawReplay();
    }

    if (telegram_swallow_real_keydown_) {
        telegram_swallow_real_keydown_ = false;
        logger::Log(logger::Level::Warning,
                    L"Telegram unsafe cancellation keydown consumed once");
        *pfEaten = TRUE;
        return S_OK;
    }

    if (telegram_boundary_resume_state_.IsPending()) {
        const bool canceled_safely =
            CancelTelegramNativeSelectionForRealKey(pic);
        logger::Log(logger::Level::Warning,
                    canceled_safely
                        ? L"Telegram boundary resume canceled safely by OnKeyDown race"
                        : L"Telegram boundary resume OnKeyDown cancellation failed; key consumed");
        *pfEaten = canceled_safely ? FALSE : TRUE;
        return S_OK;
    }

    BOOL hotkeyEaten = FALSE;
    if (DispatchHotkeyEvent(wParam, lParam, true, &hotkeyEaten)) {
        *pfEaten = hotkeyEaten;
        return S_OK;
    }

    if (HasTextShortcutModifier()) {
        if (active_composition_) {
            logger::Log(logger::Level::Info, L"OnKeyDown: Shortcut modifier detected, committing active composition");
            CommitCompositionSync(pic);
        }
        *pfEaten = FALSE;
        return S_OK;
    }

    is_updating_selection_ = false;

    if (wParam == VK_ESCAPE) {
        if (IsConsoleProcess()) {
            logger::Log(logger::Level::Info, L"OnKeyDown: Esc detected in console process");
            
            std::wstring raw_keys = engine_.GetRawString();
            std::wstring display_text = engine_.GetDisplayString();
            size_t inline_len = direct_inline_display_length_;
            bool has_comp = (active_composition_.Get() != nullptr);
            
            engine_.Clear();
            direct_inline_display_length_ = 0;
            ClearLastCommitUndo();
            
            if (has_comp || inline_len > 0) {
                // Determine if we need to revert by comparing raw and display
                bool needs_revert = (raw_keys != display_text);
                
                // Check if this is a Vim or SSH session to determine if ESC is needed
                bool is_vim_or_ssh = false;
                HWND hwnd = GetBestFocusWindow();
                if (hwnd) {
                    wchar_t title[512] = {0};
                    if (::GetWindowTextW(hwnd, title, 511) > 0) {
                        std::wstring title_str(title);
                        for (wchar_t& c : title_str) {
                            if (c >= L'A' && c <= L'Z') {
                                c = c - L'A' + L'a';
                            }
                        }
                        if (title_str.find(L"vim") != std::wstring::npos ||
                            title_str.find(L"vi") != std::wstring::npos ||
                            title_str.find(L"ssh") != std::wstring::npos ||
                            title_str.find(L"nano") != std::wstring::npos ||
                            title_str.find(L"tmux") != std::wstring::npos ||
                            title_str.find(L"emacs") != std::wstring::npos ||
                            title_str.find(L"wsl") != std::wstring::npos ||
                            title_str.find(L"bash") != std::wstring::npos) {
                            is_vim_or_ssh = true;
                        }
                    }
                }
                
                if (has_comp) {
                    if (needs_revert) {
                        ComPtr<EditSession> session;
                        session.Attach(new (std::nothrow) EditSession(this, pic, EditAction::Commit, L'\xffff'));
                        if (session) {
                            HRESULT hr = 0;
                            pic->RequestEditSession(client_id_, session.Get(), TF_ES_SYNC | TF_ES_READWRITE, &hr);
                        }
                        for (wchar_t wch : raw_keys) {
                            SendSyntheticUnicodeChar(wch);
                        }
                    } else {
                        CommitCompositionSync(pic);
                    }
                    
                    if (is_vim_or_ssh) {
                        SendSyntheticNativeKey(VK_ESCAPE);
                    }
                    *pfEaten = TRUE;
                } else {
                    if (needs_revert) {
                        for (size_t i = 0; i < inline_len; ++i) {
                            SendSyntheticNativeKey(VK_BACK);
                        }
                        for (wchar_t wch : raw_keys) {
                            SendSyntheticUnicodeChar(wch);
                        }
                        if (is_vim_or_ssh) {
                            SendSyntheticNativeKey(VK_ESCAPE);
                        }
                        *pfEaten = TRUE;
                    } else {
                        if (is_vim_or_ssh) {
                            SendSyntheticNativeKey(VK_ESCAPE);
                            *pfEaten = TRUE;
                        } else {
                            *pfEaten = FALSE;
                        }
                    }
                }
            } else {
                *pfEaten = FALSE;
            }
            return S_OK;
        }

        if (active_composition_) {
            logger::Log(logger::Level::Info, L"OnKeyDown: Esc detected with active composition, committing raw keys");
            std::wstring raw_keys = engine_.GetRawString();
            engine_.SecureClear();
            
            ComPtr<EditSession> session;
            session.Attach(new (std::nothrow) EditSession(this, pic, EditAction::CommitEscRaw, raw_keys));
            if (session) {
                HRESULT hr = 0;
                pic->RequestEditSession(client_id_, session.Get(), TF_ES_SYNC | TF_ES_READWRITE, &hr);
            }
            
            *pfEaten = TRUE;
            return S_OK;
        } else if (HasDirectInlineState()) {
            if (IsDirectCommitApp()) {
                *pfEaten = FALSE;
            } else {
                logger::Log(logger::Level::Info, L"OnKeyDown: Esc detected with direct inline state, committing raw keys");
                TryProcessDirectCommitEsc(pic);
                *pfEaten = TRUE;
                return S_OK;
            }
        } else if (last_commit_undo_) {
            if (!last_commit_undo_->original_text.empty()) {
                // A two-token fuzzy transform has literal source text rather
                // than replayable Telex/VNI keys. Esc must not feed that span
                // through the legacy raw-key reconstruction path.
                ClearLastCommitUndo();
                *pfEaten = FALSE;
            } else if (IsDirectCommitApp()) {
                ClearLastCommitUndo();
                *pfEaten = FALSE;
            } else {
                logger::Log(logger::Level::Info, L"OnKeyDown: Esc detected, attempting to restore raw keys");
                bool restored = false;
                if (last_commit_undo_->is_tsf) {
                    ComPtr<EditSession> session;
                    session.Attach(new (std::nothrow) EditSession(this, pic, EditAction::RestoreRaw));
                    if (session) {
                        HRESULT hr = 0;
                        HRESULT hrReq = pic->RequestEditSession(client_id_, session.Get(), TF_ES_SYNC | TF_ES_READWRITE, &hr);
                        if (SUCCEEDED(hrReq) && SUCCEEDED(hr) && session->action_succeeded()) {
                            restored = true;
                        }
                    }
                } else {
                    restored = TryRestoreLastCommittedRawDirectInline(last_commit_undo_->hwnd, false);
                }

                if (restored) {
                    *pfEaten = TRUE;
                    return S_OK;
                } else {
                    ClearLastCommitUndo();
                }
            }
        }
    } else {
        const bool no_modifier = !HasTextShortcutModifier() && !IsKeyDown(VK_SHIFT);
        if (wParam == VK_BACK && !active_composition_ && last_commit_undo_ && no_modifier &&
            !(IsExcelApp() && excel_formula_state_ != core::ExcelFormulaSessionState::Idle)) {
            const HWND focus_hwnd = GetBestFocusWindow();
            const bool focus_matches = focus_hwnd != nullptr &&
                focus_hwnd == last_commit_undo_->hwnd;
            const bool same_entry_context = !last_commit_undo_->is_tsf ||
                IsSameComObject(pic, last_commit_undo_->expected_context.Get());
            const bool telegram_tsf = last_commit_undo_->is_tsf && IsTelegramProcess();
            const bool same_tsf_context = telegram_tsf &&
                IsSameComObject(pic, last_commit_undo_->expected_context.Get());
            const bool safe_context = typing_mode_ == 0 &&
                !IsSecureInputContext() &&
                !IsCurrentAppBlocked(pic) &&
                !IsBuiltInNativeBypassProcess(GetFocusedProcessName());
            const bool smart_undo_host_supported =
                last_commit_undo_->is_tsf ||
                ClassNameEquals(last_commit_undo_->hwnd, L"Edit") ||
                ClassNameEquals(last_commit_undo_->hwnd, L"Scintilla");
            if (ShouldRouteSmartUndoBackspace(
                    *last_commit_undo_, enable_smart_undo_,
                    GetTickCount64(), false, no_modifier, focus_matches,
                    same_entry_context, true, IsSecureInputContext(),
                    safe_context && smart_undo_host_supported)) {
                bool restored = false;
                if (last_commit_undo_->is_tsf) {
                    ComPtr<EditSession> session;
                    session.Attach(new (std::nothrow) EditSession(
                        this, pic, EditAction::SmartUndoCorrection));
                    if (session) {
                        HRESULT hr = 0;
                        const HRESULT hr_req = pic->RequestEditSession(
                            client_id_, session.Get(),
                            TF_ES_SYNC | TF_ES_READWRITE, &hr);
                        restored = SUCCEEDED(hr_req) && SUCCEEDED(hr) &&
                            session->action_succeeded();
                    }
                } else {
                    restored =
                        TrySmartUndoLastCommittedCorrectionDirectInline(
                            last_commit_undo_->hwnd);
                }

                if (!restored) {
                    ClearLastCommitUndo();
                }
                *pfEaten = restored ? TRUE : FALSE;
                return S_OK;
            }
            if (ShouldRouteTelegramNativeBoundaryBackspace(
                    *last_commit_undo_, GetTickCount64(), false, no_modifier,
                    telegram_tsf, same_tsf_context, safe_context,
                    static_cast<bool>(last_commit_undo_->committed_text_range))) {
                const ULONGLONG transaction_tick = GetTickCount64();
                const bool transaction_started =
                    telegram_boundary_resume_state_.Begin(transaction_tick);
                if (transaction_started) {
                    telegram_boundary_resume_context_ =
                        ComPtr<ITfContext>(pic);
                    telegram_synthetic_selection_suppression_.Begin(
                        transaction_tick);
                }
                const UINT sent_count = transaction_started
                    ? SendTelegramBoundarySelectionSequence()
                    : 0;
                const auto send_decision =
                    DecideTelegramNativeSelectionSend(sent_count);
                const bool caps_lock_on =
                    (::GetKeyState(VK_CAPITAL) & 0x0001) != 0;
                auto replay_plan = send_decision.selection_complete
                    ? BuildTelegramRawReplayPlan(
                          last_commit_undo_->raw_keys, caps_lock_on,
                          core::kMaxRawKeysPerComposition)
                    : std::nullopt;
                const bool scheduled = transaction_started && replay_plan &&
                    ScheduleTelegramRawReplay(
                        pic, std::move(*replay_plan), caps_lock_on);
                logger::LogFormat(
                    scheduled ? logger::Level::Info : logger::Level::Warning,
                    L"Telegram native boundary replay scheduled: state_started=%d, sent_count=%u, consume=%d, selection_complete=%d, scheduled=%d, raw_len=%zu, display_len=%zu",
                    transaction_started ? 1 : 0,
                    sent_count,
                    send_decision.consume_physical_backspace ? 1 : 0,
                    send_decision.selection_complete ? 1 : 0,
                    scheduled ? 1 : 0,
                    last_commit_undo_->raw_keys.length(),
                    last_commit_undo_->display_text.length());
                if (send_decision.consume_physical_backspace) {
                    if (!scheduled) {
                        if (send_decision.selection_complete) {
                            SendTelegramSelectionCollapseRight();
                        }
                        engine_.SecureClear();
                        ClearLastCommitUndo();
                    }
                    *pfEaten = TRUE;
                    return S_OK;
                }
                engine_.SecureClear();
                ClearLastCommitUndo();
                *pfEaten = FALSE;
                return S_OK;
            }
        }

        if (wParam == VK_BACK && !active_composition_ && last_commit_undo_ &&
            !(last_commit_undo_->is_tsf && IsTelegramProcess())) {
            if (IsCommitUndoRestoreWindowValid(
                    GetTickCount64(), last_commit_undo_->committed_tick)) {
                bool restored = false;
                if (last_commit_undo_->is_tsf) {
                    ComPtr<EditSession> session;
                    session.Attach(new (std::nothrow) EditSession(this, pic, EditAction::RestoreRawBackspace));
                    if (session) {
                        HRESULT hr = 0;
                        HRESULT hrReq = pic->RequestEditSession(client_id_, session.Get(), TF_ES_SYNC | TF_ES_READWRITE, &hr);
                        if (SUCCEEDED(hrReq) && SUCCEEDED(hr) && session->action_succeeded()) {
                            restored = true;
                        }
                    }
                } else {
                    restored = TryRestoreLastCommittedRawDirectInline(last_commit_undo_->hwnd, true);
                }

                if (restored) {
                    *pfEaten = TRUE;
                    return S_OK;
                }
            }
        }

        if (!IsModifierKey(wParam)) {
            ClearLastCommitUndo();
        }
    }

    EnsureInkscapeSubclassed();

    const bool browser_text_key = IsBrowserProcess() &&
        IsValidCompositionKey(wParam, engine_.GetInputMethod());
    const bool browser_scope_check_already_attempted =
        std::exchange(browser_input_scope_test_gate_attempted_, false);
    const bool browser_scope_context_current =
        IsBrowserInputScopeContextCurrent(pic);
    if (browser_scope_check_already_attempted &&
        browser_input_scope_check_pending_ && browser_text_key &&
        browser_scope_context_current) {
        *pfEaten = FALSE;
        return S_OK;
    }
    if (ShouldRequestBrowserInputScopeCheck(
            IsBrowserProcess(), browser_text_key,
            browser_input_scope_check_pending_,
            browser_scope_context_current,
            browser_scope_check_already_attempted)) {
        if (!EnsureBrowserInputScopeCheckedForTextKey(pic)) {
            *pfEaten = FALSE;
            return S_OK;
        }
    }

    if (HandleBrowserUrlKeyDown(
            pic, wParam, lParam, pfEaten)) {
        return S_OK;
    }

    if (composition_commit_pending_ && active_composition_) {
        logger::Log(logger::Level::Info, L"OnKeyDown: Pending selection commit detected, committing composition synchronously first");
        pending_commit_caret_policy_ = CommitCaretPolicy::PreserveHostSelection;
        CommitCompositionSync(pic);
        composition_commit_pending_ = false;
    }

    if (IsInkscapeApp()) {
        if (wParam == VK_SPACE || wParam == VK_RETURN) {
            if (active_composition_) {
                last_inkscape_commit_vk_ = wParam;
                last_inkscape_commit_time_ = ::GetTickCount64();
            }
        }
    }

    if (IsExcelApp()) {
        logger::LogFormat(logger::Level::Info, L"OnKeyDown (Excel): vk=0x%02X, state=%d, has_comp=%s",
                          static_cast<unsigned int>(wParam), static_cast<int>(excel_formula_state_),
                          HasActiveComposition() ? L"TRUE" : L"FALSE");
    }

    PrepareExcelFormulaSession(pic, wParam, lParam);
    KeyDecision decision = MakeKeyDecision(pic, wParam, lParam);
    if (decision.action == KeyAction::Reconvert) {
        if (TryReconversion(pic, decision.ch, true)) {
            *pfEaten = TRUE;
            return S_OK;
        }
        if (decision.fallback_to_direct_process_char) {
            decision.eat = true;
            decision.action = KeyAction::DirectProcessChar;
        } else if (decision.fallback_to_process_char) {
            decision.eat = true;
            decision.action = KeyAction::ProcessChar;
        } else {
            decision.eat = false;
            decision.action = KeyAction::PassThrough;
        }
    } else if (decision.action == KeyAction::ExplorerEditReconvert) {
        if (TryExplorerEditReconversion(decision.ch, true)) {
            *pfEaten = TRUE;
            return S_OK;
        }
        if (decision.fallback_to_direct_process_char) {
            decision.eat = true;
            decision.action = KeyAction::DirectProcessChar;
        } else {
            decision.eat = false;
            decision.action = KeyAction::PassThrough;
        }
    }

    if (decision.commit_existing_before_host && active_composition_) {
        const bool host_owned_native_space =
            decision.host_owned_commit_delimiter == L' ';
        shorthand_host_delimiter_consumed_ = false;
        if (decision.replay_native_after_commit) {
            pending_commit_caret_policy_ = CommitCaretPolicy::MoveToCompositionEnd;
            CommitCompositionSync(
                pic, decision.replay_vk,
                decision.host_owned_commit_delimiter);
        } else {
            CommitCompositionSync(
                pic, 0, decision.host_owned_commit_delimiter);
        }
        if (shorthand_host_delimiter_consumed_) {
            decision.eat = true;
        }
        if (host_owned_native_space && !active_composition_ &&
            last_commit_undo_ && last_commit_undo_->is_tsf &&
            IsSameComObject(
                pic, last_commit_undo_->expected_context.Get())) {
            // The host inserts Space immediately after OnKeyDown returns. Mark
            // it optimistically; Smart Undo still verifies the actual span.
            last_commit_undo_->committed_with_ascii_space = true;
        }
    }

    if (decision.clear_sensitive_before_host) {
        ClearSensitiveState(false);
    }

    *pfEaten = decision.eat ? TRUE : FALSE;

    if (decision.eat) {
        logger::LogFormat(logger::Level::Info, L"OnKeyDown (EATEN): action = %d", static_cast<int>(decision.action));
        
        if (decision.action == KeyAction::InkscapePostKey) {
            if (!ProcessInkscapeNonCompositionKey(wParam, lParam)) {
                *pfEaten = FALSE;
            }
        } else if (decision.action == KeyAction::Backspace) {
            ComPtr<EditSession> session;
            session.Attach(new (std::nothrow) EditSession(this, pic, EditAction::Backspace));
            if (session) {
                HRESULT hr = 0;
                HRESULT hrReq = pic->RequestEditSession(client_id_, session.Get(), TF_ES_SYNC | TF_ES_READWRITE, &hr);
                logger::LogFormat(logger::Level::Info, L"RequestEditSession (Backspace) returned hrReq = 0x%08X, hr = 0x%08X", hrReq, hr);
                if (FAILED(hrReq) || FAILED(hr) || !session->action_succeeded()) {
                    *pfEaten = FALSE;
                }
            }
        } else if (decision.action == KeyAction::CommitSpace) {
            ComPtr<ITfEditSession> session;
            session.Attach(new (std::nothrow) EditSession(this, pic, EditAction::Commit, L' '));
            if (session) {
                HRESULT hr = 0;
                HRESULT hrReq = pic->RequestEditSession(client_id_, session.Get(), TF_ES_SYNC | TF_ES_READWRITE, &hr);
                logger::LogFormat(logger::Level::Info, L"RequestEditSession (Commit Space) returned hrReq = 0x%08X, hr = 0x%08X", hrReq, hr);
            }
        } else if (decision.action == KeyAction::CommitChar) {
            ComPtr<ITfEditSession> session;
            session.Attach(new (std::nothrow) EditSession(this, pic, EditAction::Commit, decision.ch));
            if (session) {
                HRESULT hr = 0;
                HRESULT hrReq = pic->RequestEditSession(client_id_, session.Get(), TF_ES_SYNC | TF_ES_READWRITE, &hr);
                logger::LogFormat(logger::Level::Info, L"RequestEditSession (Commit Char) returned hrReq = 0x%08X, hr = 0x%08X", hrReq, hr);
            }
        } else if (decision.action == KeyAction::DirectBackspace) {
            if (IsNotepadPlusPlusDirectInlineFocused()) {
                if (!ProcessNotepadPlusPlusDirectBackspace()) {
                    ResetDirectInlineState();
                    *pfEaten = FALSE;
                }
            } else if (IsDirectCommitApp()) {
                if (!ProcessExplorerEditBackspace()) {
                    ResetDirectInlineState();
                    *pfEaten = FALSE;
                }
            } else if (IsFakeBackspaceApp()) {
                if (!ProcessFakeBackspaceEditBackspace()) {
                    ResetDirectInlineState();
                    *pfEaten = FALSE;
                }
            } else {
                ComPtr<EditSession> session;
                session.Attach(new (std::nothrow) EditSession(this, pic, EditAction::DirectBackspace));
                if (session) {
                    const EditSessionDispatchResult dispatch =
                        RequestEditSessionWithWordAsyncFallback(
                            pic, client_id_, session.Get(),
                            TF_ES_SYNC | TF_ES_READWRITE,
                            IsWordTsfInlineApp());
                    logger::LogFormat(
                        logger::Level::Info,
                        L"RequestEditSession (Direct Backspace): sync_req=0x%08X, sync_hr=0x%08X, async_retry=%d, async_req=0x%08X, async_hr=0x%08X, deferred=%d",
                        dispatch.sync_request_hr, dispatch.sync_session_hr,
                        dispatch.retried_async ? 1 : 0,
                        dispatch.async_request_hr, dispatch.async_session_hr,
                        dispatch.deferred ? 1 : 0);
                    if (!dispatch.Accepted() ||
                        (!dispatch.deferred &&
                         !session->action_succeeded())) {
                        ResetDirectInlineState();
                        *pfEaten = FALSE;
                    }
                }
            }
        } else if (decision.action == KeyAction::DirectCommitSpace) {
            if (IsNotepadPlusPlusDirectInlineFocused()) {
                if (!ProcessNotepadPlusPlusDirectCommitChar(L' ')) {
                    *pfEaten = FALSE;
                }
            } else {
                ComPtr<EditSession> session;
                session.Attach(new (std::nothrow) EditSession(
                    this, pic, EditAction::DirectCommit, decision.ch,
                    nullptr, 0,
                    decision.host_owned_commit_delimiter));
                if (session) {
                    const EditSessionDispatchResult dispatch =
                        RequestEditSessionWithWordAsyncFallback(
                            pic, client_id_, session.Get(),
                            TF_ES_SYNC | TF_ES_READWRITE,
                            IsWordTsfInlineApp());
                    logger::LogFormat(
                        logger::Level::Info,
                        L"RequestEditSession (Direct Commit Space): sync_req=0x%08X, sync_hr=0x%08X, async_retry=%d, async_req=0x%08X, async_hr=0x%08X, deferred=%d",
                        dispatch.sync_request_hr, dispatch.sync_session_hr,
                        dispatch.retried_async ? 1 : 0,
                        dispatch.async_request_hr, dispatch.async_session_hr,
                        dispatch.deferred ? 1 : 0);
                    if (!dispatch.Accepted() ||
                        (!dispatch.deferred &&
                         !session->action_succeeded())) {
                        ResetDirectInlineState();
                        *pfEaten = FALSE;
                    }
                }
            }
        } else if (decision.action == KeyAction::DirectCommitChar) {
            if (IsNotepadPlusPlusDirectInlineFocused()) {
                if (!ProcessNotepadPlusPlusDirectCommitChar(decision.ch)) {
                    *pfEaten = FALSE;
                }
            } else {
                ComPtr<ITfEditSession> session;
                session.Attach(new (std::nothrow) EditSession(this, pic, EditAction::DirectCommit, decision.ch));
                if (session) {
                    HRESULT hr = 0;
                    HRESULT hrReq = pic->RequestEditSession(client_id_, session.Get(), TF_ES_SYNC | TF_ES_READWRITE, &hr);
                    logger::LogFormat(logger::Level::Info, L"RequestEditSession (Direct Commit Char) returned hrReq = 0x%08X, hr = 0x%08X", hrReq, hr);
                }
            }
        } else if (decision.action == KeyAction::DirectProcessChar) {
            logger::Log(logger::Level::Info, L"DirectProcessChar requested");
            if (decision.ch != 0) {
                if (IsNotepadPlusPlusDirectInlineFocused()) {
                    if (!ProcessNotepadPlusPlusDirectChar(decision.ch)) {
                        ResetDirectInlineState();
                        *pfEaten = FALSE;
                    }
                } else if (IsDirectCommitApp()) {
                    if (!ProcessExplorerEditChar(decision.ch)) {
                        ResetDirectInlineState();
                        *pfEaten = FALSE;
                    }
                } else if (IsFakeBackspaceApp()) {
                    if (!ProcessFakeBackspaceEditChar(decision.ch)) {
                        ResetDirectInlineState();
                        *pfEaten = FALSE;
                    }
                } else {
                    ComPtr<EditSession> session;
                    session.Attach(new (std::nothrow) EditSession(this, pic, EditAction::DirectProcessChar, decision.ch));
                    if (session) {
                        const EditSessionDispatchResult dispatch =
                            RequestEditSessionWithWordAsyncFallback(
                                pic, client_id_, session.Get(),
                                TF_ES_SYNC | TF_ES_READWRITE,
                                IsWordTsfInlineApp());
                        logger::LogFormat(
                            logger::Level::Info,
                            L"RequestEditSession (Direct Process Char): sync_req=0x%08X, sync_hr=0x%08X, async_retry=%d, async_req=0x%08X, async_hr=0x%08X, deferred=%d",
                            dispatch.sync_request_hr,
                            dispatch.sync_session_hr,
                            dispatch.retried_async ? 1 : 0,
                            dispatch.async_request_hr,
                            dispatch.async_session_hr,
                            dispatch.deferred ? 1 : 0);
                        if (!dispatch.Accepted() ||
                            (!dispatch.deferred &&
                             !session->action_succeeded())) {
                            ResetDirectInlineState();
                            *pfEaten = FALSE;
                        }
                    } else {
                        ResetDirectInlineState();
                        *pfEaten = FALSE;
                    }
                }
            }
        } else if (decision.action == KeyAction::ProcessChar) {
            logger::Log(logger::Level::Info, L"ProcessChar requested");
            if (decision.ch != 0) {
                ComPtr<EditSession> session;
                session.Attach(new (std::nothrow) EditSession(this, pic, EditAction::ProcessChar, decision.ch));
                if (session) {
                    HRESULT hr = 0;
                    HRESULT hrReq = pic->RequestEditSession(client_id_, session.Get(), TF_ES_SYNC | TF_ES_READWRITE, &hr);
                    logger::LogFormat(logger::Level::Info, L"RequestEditSession (ProcessChar) returned hrReq = 0x%08X, hr = 0x%08X", hrReq, hr);
                    if (FAILED(hrReq) || FAILED(hr) || !session->action_succeeded()) {
                        logger::Log(logger::Level::Warning, L"ProcessChar edit-session failed; returning key to host to avoid swallowing input");
                        ClearSensitiveState(false);
                        *pfEaten = FALSE;
                    }
                }
            } else {
                logger::Log(logger::Level::Warning, L"Char is 0, skipping EditSession request");
            }
        }
    } else {
        logger::LogFormat(logger::Level::Debug, L"OnKeyDown (PASSED): commit_existing = %s",
                          decision.commit_existing_before_host ? L"TRUE" : L"FALSE");
    }

    if (decision.pass_to_host_after_action) {
        *pfEaten = FALSE;
    }

    return S_OK;
}

STDMETHODIMP VietnameseIME::OnTestKeyUp([[maybe_unused]] ITfContext* pic, [[maybe_unused]] WPARAM wParam, [[maybe_unused]] LPARAM lParam, BOOL* pfEaten) {
    if (!pfEaten) return E_INVALIDARG;
    CheckAndReloadConfig();
    const ULONG_PTR extra_info = static_cast<ULONG_PTR>(::GetMessageExtraInfo());
    const bool lost_marker_selection_key =
        telegram_synthetic_selection_suppression_.ShouldPassThrough(
            telegram_boundary_resume_state_.phase,
            GetTickCount64(), wParam);
    if (IsTelegramNativeTransactionMarker(extra_info) ||
        lost_marker_selection_key) {
        if (lost_marker_selection_key &&
            !IsTelegramNativeTransactionMarker(extra_info)) {
            logger::Log(logger::Level::Info,
                        L"Telegram synthetic selection keyup passed through without marker");
        }
        *pfEaten = FALSE;
        return S_OK;
    }
    if (extra_info == static_cast<ULONG_PTR>(0xDEADC0DEu) ||
        (lParam & (1 << 28)) != 0) {
        *pfEaten = FALSE;
        return S_OK;
    }
    *pfEaten = ShouldClaimHotkeyTestEvent(wParam, false) ? TRUE : FALSE;
    return S_OK;
}

STDMETHODIMP VietnameseIME::OnKeyUp([[maybe_unused]] ITfContext* pic, [[maybe_unused]] WPARAM wParam, [[maybe_unused]] LPARAM lParam, BOOL* pfEaten) {
    if (!pfEaten) return E_INVALIDARG;
    CheckAndReloadConfig();
    const ULONG_PTR extra_info = static_cast<ULONG_PTR>(::GetMessageExtraInfo());
    const bool lost_marker_selection_key =
        telegram_synthetic_selection_suppression_.ShouldPassThrough(
            telegram_boundary_resume_state_.phase,
            GetTickCount64(), wParam);
    if (IsTelegramNativeTransactionMarker(extra_info) ||
        lost_marker_selection_key) {
        if (lost_marker_selection_key &&
            !IsTelegramNativeTransactionMarker(extra_info)) {
            logger::Log(logger::Level::Info,
                        L"Telegram synthetic selection keyup dispatch passed through without marker");
        }
        *pfEaten = FALSE;
        return S_OK;
    }
    if (extra_info == static_cast<ULONG_PTR>(0xDEADC0DEu) ||
        (lParam & (1 << 28)) != 0) {
        *pfEaten = FALSE;
        return S_OK;
    }
    BOOL hotkeyEaten = FALSE;
    DispatchHotkeyEvent(wParam, lParam, false, &hotkeyEaten);
    *pfEaten = hotkeyEaten;
    return S_OK;
}

STDMETHODIMP VietnameseIME::OnPreservedKey([[maybe_unused]] ITfContext* pic, [[maybe_unused]] REFGUID rguid, BOOL* pfEaten) {
    if (!pfEaten) return E_INVALIDARG;
    *pfEaten = FALSE;
    return S_OK;
}

bool VietnameseIME::IsModifierKey(WPARAM wParam) const noexcept {
    return (wParam == VK_SHIFT || wParam == VK_CONTROL || wParam == VK_MENU ||
            wParam == VK_LWIN || wParam == VK_RWIN || wParam == VK_CAPITAL ||
            wParam == VK_NUMLOCK || wParam == VK_SCROLL ||
            wParam == VK_LSHIFT || wParam == VK_RSHIFT ||
            wParam == VK_LCONTROL || wParam == VK_RCONTROL ||
            wParam == VK_LMENU || wParam == VK_RMENU);
}

VietnameseIME::KeyDecision VietnameseIME::MakeKeyDecision(ITfContext* pic, WPARAM wParam, LPARAM lParam) {
    KeyDecision decision;
    decision.is_modifier = IsModifierKey(wParam);
    if (decision.is_modifier) {
        return decision;
    }

    const bool has_composition = HasActiveComposition();

    if (typing_mode_ == 1) {
        if (has_composition) {
            decision.commit_existing_before_host = true;
        }
        decision.clear_sensitive_before_host = true;
        decision.action = KeyAction::PassThrough;
        return decision;
    }

    if (IsSecureInputContext()) {
        if (has_composition) {
            decision.commit_existing_before_host = true;
        }
        decision.clear_sensitive_before_host = true;
        return decision;
    }

    if (IsBuiltInNativeBypassProcess(GetFocusedProcessName())) {
        if (has_composition) {
            decision.commit_existing_before_host = true;
        }
        decision.clear_sensitive_before_host = true;
        return decision;
    }

    if (IsCurrentAppBlocked(pic)) {
        if (has_composition) {
            decision.commit_existing_before_host = true;
        }
        decision.clear_sensitive_before_host = true;
        return decision;
    }

    if (IsExcelApp()) {
        const core::ExcelFormulaSessionState state = GetExcelFormulaSessionState(pic);
        if (state == core::ExcelFormulaSessionState::PendingFormulaStart) {
            if (IsValidCompositionKey(wParam, engine_.GetInputMethod())) {
                TryAdoptPendingExcelFormulaContext(pic);
            }
            if (has_composition) {
                decision.commit_existing_before_host = true;
            }
            return decision;
        }
        if (state == core::ExcelFormulaSessionState::FormulaSyntax) {
            if (has_composition) {
                decision.commit_existing_before_host = true;
            }
            return decision;
        }
    }

    const bool is_fake_backspace = IsFakeBackspaceApp();
    if (is_fake_backspace) {
        if (IsInkscapeApp()) {
            const bool has_inline = HasDirectInlineState();
            if (has_inline && wParam == VK_BACK) {
                decision.eat = true;
                decision.action = KeyAction::DirectBackspace;
                return decision;
            }
            if (has_inline && IsCaretNavigationKey(wParam)) {
                decision.clear_sensitive_before_host = true;
                return decision;
            }
            if (!HasTextShortcutModifier()) {
                if (IsValidCompositionKey(wParam, engine_.GetInputMethod()) ||
                    IsSmartContextContinuationKey(wParam, lParam)) {
                    decision.ch = TranslateKey(wParam, lParam);
                    if (decision.ch != 0) {
                        decision.eat = true;
                        decision.action = KeyAction::DirectProcessChar;
                        return decision;
                    }
                }
                if (has_inline) {
                    decision.eat = true;
                    decision.action = KeyAction::InkscapePostKey;
                    return decision;
                }
            }
            return decision;
        }

        const bool has_inline = HasDirectInlineState();
        if (has_composition) {
            decision.commit_existing_before_host = true;
            decision.clear_sensitive_before_host = true;
            return decision;
        }
        if (has_inline && wParam == VK_BACK) {
            decision.eat = true;
            decision.action = KeyAction::DirectBackspace;
            return decision;
        }
        if (has_inline && IsCaretNavigationKey(wParam)) {
            decision.clear_sensitive_before_host = true;
            return decision;
        }
        if (!HasTextShortcutModifier()) {
            if (IsValidCompositionKey(wParam, engine_.GetInputMethod()) ||
                IsSmartContextContinuationKey(wParam, lParam)) {
                decision.ch = TranslateKey(wParam, lParam);
                if (decision.ch != 0) {
                    decision.eat = true;
                    decision.action = KeyAction::DirectProcessChar;
                    return decision;
                }
            }
            if (has_inline) {
                decision.clear_sensitive_before_host = true;
                return decision;
            }
        } else {
            if (has_inline) {
                decision.clear_sensitive_before_host = true;
                return decision;
            }
        }
        return decision;
    }

    // Enter and Tab should keep native host behavior. If a composition exists,
    // finalize it on OnKeyDown before returning the key to the host.
    if (has_composition && (wParam == VK_RETURN || wParam == VK_TAB)) {
        if (GetNativeKeyReplayKind(pic, wParam) == NativeKeyReplayKind::ReplayNativeKey) {
            decision.eat = true;
            decision.commit_existing_before_host = true;
            decision.replay_native_after_commit = true;
            decision.replay_vk = static_cast<WORD>(wParam);
            return decision;
        }
        decision.commit_existing_before_host = true;
        return decision;
    }

    if (has_composition && IsCaretNavigationKey(wParam)) {
        decision.eat = true;
        decision.commit_existing_before_host = true;
        decision.replay_native_after_commit = true;
        decision.replay_vk = static_cast<WORD>(wParam);
        return decision;
    }

    const ExplorerFocusKind explorer_focus = GetExplorerFocusKind(pic);
    if (explorer_focus == ExplorerFocusKind::NativeSurface) {
        if (has_composition) {
            decision.commit_existing_before_host = true;
        }
        if (HasDirectInlineState()) {
            decision.clear_sensitive_before_host = true;
        }
        return decision;
    }

    const bool word_inline = IsWordTsfInlineApp();
    const bool has_word_inline = word_inline && IsWordTsfInlineActive();
    if (word_inline) {
        if (!HasTextShortcutModifier() && wParam == VK_SPACE) {
            const auto plan = core::DecideHostOwnedSpaceCommit(
                true, false, true, has_composition, has_word_inline);
            if (plan.target ==
                core::HostOwnedSpaceCommitTarget::Composition) {
                decision.commit_existing_before_host = true;
                decision.host_owned_commit_delimiter =
                    plan.host_owned_commit_delimiter;
                return decision;
            }
            if (plan.target ==
                core::HostOwnedSpaceCommitTarget::DirectInline) {
                decision.eat = true;
                decision.pass_to_host_after_action = plan.pass_key_to_host;
                decision.action = KeyAction::DirectCommitSpace;
                decision.ch = plan.ime_insertion_character;
                decision.host_owned_commit_delimiter =
                    plan.host_owned_commit_delimiter;
                return decision;
            }
        }

        if (has_composition) {
            const bool has_shortcut_modifier = HasTextShortcutModifier();
            const bool valid_text_key =
                !has_shortcut_modifier &&
                (IsValidCompositionKey(wParam, engine_.GetInputMethod()) ||
                 IsSmartContextContinuationKey(wParam, lParam));
            const WordReconversionContinuation continuation =
                DecideWordReconversionContinuation(
                    word_reconversion_composition_active_,
                    has_shortcut_modifier, wParam == VK_BACK,
                    valid_text_key);
            if (continuation ==
                WordReconversionContinuation::Backspace) {
                decision.eat = true;
                decision.action = KeyAction::Backspace;
                return decision;
            }
            if (continuation ==
                WordReconversionContinuation::ProcessChar) {
                decision.ch = TranslateKey(wParam, lParam);
                if (decision.ch != 0) {
                    decision.eat = true;
                    decision.action = KeyAction::ProcessChar;
                    return decision;
                }
            }
            decision.commit_existing_before_host = true;
            decision.clear_sensitive_before_host = true;
            return decision;
        }

        if (has_word_inline && wParam == VK_BACK) {
            decision.eat = true;
            decision.action = KeyAction::DirectBackspace;
            return decision;
        }

        if (has_word_inline && IsHorizontalCaretNavigationKey(wParam)) {
            decision.clear_sensitive_before_host = true;
            return decision;
        }

        if (!HasTextShortcutModifier()) {
            const bool valid_text_key =
                IsValidCompositionKey(wParam, engine_.GetInputMethod()) ||
                IsSmartContextContinuationKey(wParam, lParam);
            if (valid_text_key) {
                decision.ch = TranslateKey(wParam, lParam);
                if (decision.ch != 0) {
                    if (!has_word_inline) {
                        if (pending_shorthand_selection_) {
                            decision.eat = true;
                            decision.action = KeyAction::DirectProcessChar;
                        } else {
                            decision.action = KeyAction::Reconvert;
                            decision.fallback_to_direct_process_char = true;
                        }
                        return decision;
                    }
                    decision.eat = true;
                    decision.action = KeyAction::DirectProcessChar;
                    return decision;
                }
            }

            if (has_word_inline) {
                decision.clear_sensitive_before_host = true;
                return decision;
            }
        }

        return decision;
    }

    const bool notepad_plus_plus_direct = IsNotepadPlusPlusDirectInlineFocused();
    if (notepad_plus_plus_direct) {
        if (has_composition) {
            decision.commit_existing_before_host = true;
            decision.clear_sensitive_before_host = true;
            return decision;
        }

        const wchar_t translated = TranslateKey(wParam, lParam);
        if (HasDirectInlineState() &&
            ShouldCommitNotepadPlusPlusDirectInlineBoundary(GetFocusedProcessName(), GetClassNameOrEmpty(GetBestFocusWindow()), translated)) {
            decision.eat = true;
            decision.action = KeyAction::DirectCommitSpace;
            return decision;
        }

        if (HasDirectInlineState() && wParam == VK_BACK) {
            decision.eat = true;
            decision.action = KeyAction::DirectBackspace;
            return decision;
        }

        if (HasDirectInlineState() && IsHorizontalCaretNavigationKey(wParam)) {
            decision.clear_sensitive_before_host = true;
            return decision;
        }

        if (!HasTextShortcutModifier() &&
            (IsValidCompositionKey(wParam, engine_.GetInputMethod()) ||
             IsSmartContextContinuationKey(wParam, lParam))) {
            decision.ch = TranslateKey(wParam, lParam);
            if (decision.ch != 0) {
                decision.eat = true;
                decision.action = KeyAction::DirectProcessChar;
                return decision;
            }
        }

        if (HasDirectInlineState()) {
            decision.clear_sensitive_before_host = true;
        }
        return decision;
    }

    const bool direct_commit = IsDirectCommitApp();
    const bool has_direct_inline = direct_commit && HasDirectInlineState();
    if (direct_commit) {
        if (has_composition) {
            decision.commit_existing_before_host = true;
            decision.clear_sensitive_before_host = true;
            return decision;
        }

        if (has_direct_inline && wParam == VK_BACK) {
            decision.eat = true;
            decision.action = KeyAction::DirectBackspace;
            return decision;
        }

        if (has_direct_inline && IsHorizontalCaretNavigationKey(wParam)) {
            decision.clear_sensitive_before_host = true;
            return decision;
        }

        if (!HasTextShortcutModifier()) {
            if (IsValidCompositionKey(wParam, engine_.GetInputMethod()) ||
                IsSmartContextContinuationKey(wParam, lParam)) {
                decision.ch = TranslateKey(wParam, lParam);
                if (decision.ch != 0) {
                    if (!has_direct_inline) {
                        if (pending_shorthand_selection_) {
                            decision.eat = true;
                            decision.action = KeyAction::DirectProcessChar;
                        } else {
                            decision.action = KeyAction::ExplorerEditReconvert;
                            decision.fallback_to_direct_process_char = true;
                        }
                        return decision;
                    }
                    decision.eat = true;
                    decision.action = KeyAction::DirectProcessChar;
                    return decision;
                }
            }

            if (has_direct_inline) {
                decision.clear_sensitive_before_host = true;
                return decision;
            }
        }

        return decision;
    }

    if (has_composition && IsWebRichTextHostProcess()) {
        const bool valid_composition_key =
            IsValidCompositionKey(wParam, engine_.GetInputMethod());
        const bool smart_context_continuation =
            IsSmartContextContinuationKey(wParam, lParam);
        const wchar_t translated_boundary = TranslateKey(wParam, lParam);
        if (ShouldPassWebRichTextBoundaryToHost(
                true, true, wParam == VK_SPACE, wParam == VK_BACK,
                valid_composition_key, smart_context_continuation,
                translated_boundary)) {
            decision.commit_existing_before_host = true;
            if (wParam == VK_SPACE) {
                const auto plan = core::DecideHostOwnedSpaceCommit(
                    true, true, false, true, false);
                decision.host_owned_commit_delimiter =
                    plan.host_owned_commit_delimiter;
            }
            decision.action = KeyAction::PassThrough;
            return decision;
        }
    }

    const bool filtered = IsKeyFiltered(wParam, lParam);
    if (!filtered) {
        if (has_composition) {
            if (!HasTextShortcutModifier()) {
                decision.ch = TranslateKey(wParam, lParam);
                if (decision.ch != 0 && decision.ch >= L' ') {
                    decision.eat = true;
                    decision.action = KeyAction::CommitChar;
                    return decision;
                }
            }
            decision.commit_existing_before_host = true;
        }
        return decision;
    }

    if (has_composition) {
        decision.eat = true;
        if (wParam == VK_BACK) {
            decision.action = KeyAction::Backspace;
        } else if (wParam == VK_SPACE) {
            decision.action = KeyAction::CommitSpace;
        } else {
            decision.ch = TranslateKey(wParam, lParam);
            if (decision.ch != 0) {
                decision.action = KeyAction::ProcessChar;
            } else {
                decision.eat = false;
                decision.action = KeyAction::PassThrough;
                decision.commit_existing_before_host = true;
            }
        }
        return decision;
    }

    decision.ch = TranslateKey(wParam, lParam);
    if (decision.ch == 0) {
        return decision;
    }

    if (IsValidCompositionKey(wParam, engine_.GetInputMethod())) {
        if (pending_shorthand_selection_) {
            decision.eat = true;
            decision.action = KeyAction::ProcessChar;
        } else {
            decision.action = KeyAction::Reconvert;
            decision.fallback_to_process_char = true;
        }
        return decision;
    }

    decision.eat = true;
    decision.action = KeyAction::ProcessChar;
    return decision;
}

bool VietnameseIME::TryReconversion(ITfContext* pic, wchar_t ch, bool apply) {
    if (!pic || ch == 0 || IsSecureInputContext()) {
        return false;
    }

    ComPtr<EditSession> session;
    session.Attach(new (std::nothrow) EditSession(this, pic, apply ? EditAction::Reconvert : EditAction::ReconvertTest, ch));
    if (!session) {
        return false;
    }

    HRESULT hr = S_OK;
    HRESULT hrReq = pic->RequestEditSession(
        client_id_,
        session.Get(),
        apply ? (TF_ES_SYNC | TF_ES_READWRITE) : (TF_ES_SYNC | TF_ES_READ),
        &hr
    );

    logger::LogFormat(logger::Level::Info,
                      L"TryReconversion(apply=%s) returned hrReq = 0x%08X, hr = 0x%08X, convertible = %s",
                      apply ? L"TRUE" : L"FALSE",
                      hrReq,
                      hr,
                      session->is_convertible() ? L"TRUE" : L"FALSE");

    const bool converted =
        SUCCEEDED(hrReq) && SUCCEEDED(hr) && session->is_convertible();
    if (apply && converted && IsWordTsfInlineApp() &&
        HasActiveComposition()) {
        word_reconversion_composition_active_ = true;
    }
    return converted;
}

bool VietnameseIME::IsValidCompositionKey(WPARAM wParam, core::InputMethod method) const {
    if (wParam >= 0x41 && wParam <= 0x5A) {
        return true;
    }
    if (method == core::InputMethod::VNI) {
        if (wParam >= 0x30 && wParam <= 0x39) {
            if ((GetKeyState(VK_SHIFT) & 0x8000) == 0) {
                return true;
            }
        }
        if (wParam >= 0x60 && wParam <= 0x69) {
            return true;
        }
    }
    return false;
}

bool VietnameseIME::IsSmartContextContinuationKey(
    WPARAM wParam,
    LPARAM lParam) const noexcept {
    if (HasTextShortcutModifier() || !engine_.HasPendingRaw() ||
        !engine_.GetSmartContextProtection()) {
        return false;
    }

    const wchar_t ch = TranslateKey(wParam, lParam);
    return ch != 0 && engine_.ShouldContinueSmartContext(ch);
}

std::wstring VietnameseIME::GetFocusedProcessName() const {
    HWND hwnd = GetBestFocusWindow();
    if (!hwnd) {
        return L"";
    }

    DWORD process_id = 0;
    ::GetWindowThreadProcessId(hwnd, &process_id);
    if (process_id == 0) {
        return L"";
    }

    if (process_id == cached_process_id_) {
        return cached_process_name_;
    }

    std::wstring process_name;
    HANDLE hProcess = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id);
    if (hProcess) {
        std::wstring path(32768, L'\0');
        DWORD size = static_cast<DWORD>(path.size());
        if (::QueryFullProcessImageNameW(hProcess, 0, &path[0], &size) && size > 0) {
            path.resize(size);
            process_name = NormalizeProcessName(std::move(path));
        }
        ::CloseHandle(hProcess);
    }

    cached_process_id_ = process_id;
    cached_process_name_ = std::move(process_name);
    return cached_process_name_;
}

bool VietnameseIME::IsCurrentAppBlocked([[maybe_unused]] ITfContext* pic) const {
    return current_app_explicitly_disabled_;
}

bool VietnameseIME::IsDirectCommitApp() const {
    if (IsExplorerWin32EditFocused()) {
        return true;
    }
    bool is_commit = false;
    if (IsCustomDirectApp(&is_commit)) {
        if (is_commit) {
            HWND hwnd = GetBestFocusWindow();
            if (!hwnd) return false;
            std::wstring class_name = GetClassNameOrEmpty(hwnd);
            return _wcsicmp(class_name.c_str(), L"Edit") == 0;
        }
    }
    return false;
}

bool VietnameseIME::IsFakeBackspaceApp() const {
    return IsConsoleProcess() ||
           vn_ime::fake_backspace::IsFakeBackspaceTargetApp(
               host_process_name_, GetFocusedProcessName());
}


bool VietnameseIME::IsNativeEnterReplayApp() const {
    std::wstring process_name = GetFocusedProcessName();
    if (process_name.find(L"chrome") != std::wstring::npos ||
        process_name.find(L"edge") != std::wstring::npos ||
        process_name.find(L"firefox") != std::wstring::npos ||
        process_name.find(L"brave") != std::wstring::npos ||
        process_name.find(L"opera") != std::wstring::npos ||
        process_name.find(L"vivaldi") != std::wstring::npos) {
        return true;
    }
    return (process_name == L"telegram.exe" ||
            process_name == L"viber.exe" ||
            process_name == L"notepad++.exe");
}

bool IsShellNativeSurfaceWindow(HWND hwnd) {
    if (!hwnd) return false;
    std::wstring class_name = GetClassNameOrEmpty(hwnd);
    if (class_name.find(L"wcfTextViewHost") != std::wstring::npos) {
        return true;
    }
    if (class_name.find(L"HwndWrapper[devenv.exe") != std::wstring::npos) {
        return true;
    }
    return false;
}

const wchar_t* VisualStudioFocusKindName(int kind) {
    switch (static_cast<VietnameseIME::VisualStudioFocusKind>(kind)) {
        case VietnameseIME::VisualStudioFocusKind::NotVisualStudio: return L"NotVisualStudio";
        case VietnameseIME::VisualStudioFocusKind::ShellNativeSurface: return L"ShellNativeSurface";
        case VietnameseIME::VisualStudioFocusKind::TsfTextInput: return L"TsfTextInput";
        default: return L"Unknown";
    }
}

const wchar_t* NativeKeyReplayKindName(int kind) {
    switch (static_cast<VietnameseIME::NativeKeyReplayKind>(kind)) {
        case VietnameseIME::NativeKeyReplayKind::CommitOnly: return L"CommitOnly";
        case VietnameseIME::NativeKeyReplayKind::ReplayNativeKey: return L"ReplayNativeKey";
        default: return L"Unknown";
    }
}

bool VietnameseIME::IsTelegramProcess() const {
    return host_process_name_ == L"telegram.exe" || GetFocusedProcessName() == L"telegram.exe";
}

bool VietnameseIME::IsBrowserProcess() const {
    if (IsBrowserExecutableName(host_process_name_)) {
        return true;
    }
    return IsBrowserExecutableName(GetFocusedProcessName());
}

bool VietnameseIME::IsWebRichTextHostProcess() const {
    if (IsWebRichTextHostExecutableName(host_process_name_)) {
        return true;
    }
    return IsWebRichTextHostExecutableName(GetFocusedProcessName());
}

bool VietnameseIME::IsBrowserUrlNativeModeActiveForContext(
    ITfContext* pic) const noexcept {
    return browser_url_native_mode_active_ && pic &&
        browser_url_native_mode_context_ &&
        IsSameComObject(
            pic, browser_url_native_mode_context_.Get());
}

void VietnameseIME::ClearBrowserUrlPendingReconversion() noexcept {
    if (!browser_url_pending_token_.empty()) {
        SecureZeroMemory(
            browser_url_pending_token_.data(),
            browser_url_pending_token_.size() * sizeof(wchar_t));
        browser_url_pending_token_.clear();
    }
    if (!browser_url_pending_replacement_.empty()) {
        SecureZeroMemory(
            browser_url_pending_replacement_.data(),
            browser_url_pending_replacement_.size() * sizeof(wchar_t));
        browser_url_pending_replacement_.clear();
    }
    browser_url_pending_context_.Reset();
    browser_url_pending_key_ = 0;
    browser_url_pending_method_ = core::InputMethod::Telex;
    browser_url_pending_correction_level_ =
        core::CorrectionLevel::Normal;
    browser_url_pending_english_level_ =
        core::EnglishProtectionLevel::Balanced;
    browser_url_pending_smart_context_protection_ = true;
}

void VietnameseIME::ResetBrowserUrlNativeMode() noexcept {
    ClearBrowserUrlPendingReconversion();
    browser_url_native_mode_active_ = false;
    browser_url_native_mode_context_.Reset();
}

void VietnameseIME::MarkBrowserInputScopeCheckPending(
    ITfContext* pic) noexcept {
    browser_input_scope_check_pending_ = true;
    browser_input_scope_test_gate_attempted_ = false;
    browser_input_scope_context_.Reset();
    if (pic) {
        browser_input_scope_context_ = ComPtr<ITfContext>(pic);
    }
}

void VietnameseIME::ResetBrowserInputScopeCheck() noexcept {
    browser_input_scope_check_pending_ = false;
    browser_input_scope_test_gate_attempted_ = false;
    browser_input_scope_context_.Reset();
}

bool VietnameseIME::IsBrowserInputScopeContextCurrent(
    ITfContext* pic) const noexcept {
    return pic && browser_input_scope_context_ &&
        IsSameComObject(pic, browser_input_scope_context_.Get());
}

bool VietnameseIME::EnsureBrowserInputScopeCheckedForTextKey(
    ITfContext* pic) {
    const bool test_gate_attempted =
        browser_input_scope_test_gate_attempted_;
    const bool is_browser = IsBrowserProcess();
    if (!pic || !is_browser) {
        ClearSensitiveState(false);
        if (is_browser) {
            MarkBrowserInputScopeCheckPending(nullptr);
            browser_input_scope_test_gate_attempted_ =
                test_gate_attempted;
        } else {
            ResetBrowserInputScopeCheck();
        }
        return false;
    }

    if (!IsBrowserInputScopeContextCurrent(pic)) {
        ClearSensitiveState(false);
        MarkBrowserInputScopeCheckPending(pic);
        browser_input_scope_test_gate_attempted_ = test_gate_attempted;
    }
    if (!browser_input_scope_check_pending_) {
        return true;
    }

    ComPtr<EditSession> session;
    session.Attach(new (std::nothrow) EditSession(
        this, pic, EditAction::CheckPassword));
    HRESULT session_hr = E_OUTOFMEMORY;
    HRESULT request_hr = E_OUTOFMEMORY;
    if (session) {
        session_hr = E_FAIL;
        request_hr = pic->RequestEditSession(
            client_id_, session.Get(), TF_ES_SYNC | TF_ES_READ,
            &session_hr);
    }

    const BrowserInputScopeCheckDecision decision =
        DecideBrowserInputScopeCheck(
            browser_input_scope_check_pending_,
            SUCCEEDED(request_hr), SUCCEEDED(session_hr),
            session && session->action_succeeded());
    if (decision.clear_pending) {
        browser_input_scope_check_pending_ = false;
    }
    if (decision.clear_sensitive_state) {
        ClearSensitiveState(false);
        MarkBrowserInputScopeCheckPending(pic);
        browser_input_scope_test_gate_attempted_ = test_gate_attempted;
    }

    logger::LogFormat(
        decision.continue_key ? logger::Level::Debug
                              : logger::Level::Warning,
        L"Browser input-scope key gate: request_hr=0x%08X, session_hr=0x%08X, executed=%d, continue=%d, pending=%d",
        request_hr, session_hr,
        session && session->action_succeeded() ? 1 : 0,
        decision.continue_key ? 1 : 0,
        browser_input_scope_check_pending_ ? 1 : 0);
    return decision.continue_key;
}

std::optional<BrowserTextInputMode>
VietnameseIME::DetectBrowserTextInputMode(
    ITfContext* pic) {
    ResetBrowserUrlNativeMode();
    if (!pic || !IsBrowserProcess() || IsSecureInputContext()) {
        return std::nullopt;
    }

    ComPtr<EditSession> session;
    session.Attach(new (std::nothrow) EditSession(
        this, pic, EditAction::DetectBrowserTextInputMode));
    if (!session) {
        return std::nullopt;
    }

    HRESULT session_hr = E_FAIL;
    const HRESULT request_hr = pic->RequestEditSession(
        client_id_, session.Get(), TF_ES_SYNC | TF_ES_READ,
        &session_hr);
    const bool detected = SUCCEEDED(request_hr) &&
        SUCCEEDED(session_hr) && session->action_succeeded();
    if (!detected) {
        logger::LogFormat(
            logger::Level::Warning,
            L"DetectBrowserTextInputMode failed: request=0x%08X, session=0x%08X, executed=%d",
            request_hr, session_hr,
            session->action_executed() ? 1 : 0);
        return std::nullopt;
    }

    const BrowserTextInputMode mode = session->browser_text_input_mode();
    if (mode == BrowserTextInputMode::UrlNativeReconversion) {
        browser_url_native_mode_context_ = ComPtr<ITfContext>(pic);
        browser_url_native_mode_active_ = true;
    }
    logger::LogFormat(
        logger::Level::Debug,
        L"DetectBrowserTextInputMode request=0x%08X session=0x%08X mode=%d",
        request_hr, session_hr, static_cast<int>(mode));
    return mode;
}

bool VietnameseIME::TryBrowserUrlTypedReconversion(
    ITfContext* pic, wchar_t ch, bool apply) {
    if (!pic || ch == 0 || !IsBrowserProcess() ||
        IsSecureInputContext() || HasActiveComposition() ||
        !IsBrowserUrlNativeModeActiveForContext(pic)) {
        return false;
    }

    if (apply) {
        const bool pending_matches =
            browser_url_pending_context_ &&
            IsSameComObject(
                pic, browser_url_pending_context_.Get()) &&
            browser_url_pending_key_ == ch &&
            browser_url_pending_method_ == engine_.GetInputMethod() &&
            browser_url_pending_correction_level_ ==
                engine_.GetCorrectionLevel() &&
            browser_url_pending_english_level_ ==
                engine_.GetEnglishProtectionLevel() &&
            browser_url_pending_smart_context_protection_ ==
                engine_.GetSmartContextProtection() &&
            !browser_url_pending_token_.empty() &&
            !browser_url_pending_replacement_.empty();
        if (!pending_matches) {
            ClearBrowserUrlPendingReconversion();
            return false;
        }
    } else {
        ClearBrowserUrlPendingReconversion();
    }

    ComPtr<EditSession> session;
    session.Attach(new (std::nothrow) EditSession(
        this, pic,
        apply ? EditAction::BrowserUrlReconvertApply
              : EditAction::BrowserUrlReconvertTest,
        ch));
    HRESULT session_hr = E_OUTOFMEMORY;
    HRESULT request_hr = E_OUTOFMEMORY;
    if (session) {
        session_hr = E_FAIL;
        request_hr = pic->RequestEditSession(
            client_id_, session.Get(),
            apply ? (TF_ES_SYNC | TF_ES_READWRITE)
                  : (TF_ES_SYNC | TF_ES_READ),
            &session_hr);
    }

    const bool request_succeeded = SUCCEEDED(request_hr) &&
        SUCCEEDED(session_hr) && session && session->action_executed();
    const bool converted = request_succeeded &&
        session->is_convertible();
    if (!apply && converted) {
        browser_url_pending_context_ = ComPtr<ITfContext>(pic);
        browser_url_pending_token_.assign(session->get_source_text());
        browser_url_pending_replacement_.assign(
            session->get_result_text());
        browser_url_pending_key_ = ch;
        browser_url_pending_method_ = engine_.GetInputMethod();
        browser_url_pending_correction_level_ =
            engine_.GetCorrectionLevel();
        browser_url_pending_english_level_ =
            engine_.GetEnglishProtectionLevel();
        browser_url_pending_smart_context_protection_ =
            engine_.GetSmartContextProtection();
    }

    const size_t source_length = apply
        ? browser_url_pending_token_.length()
        : session ? session->get_source_text().length() : 0;
    const size_t replacement_length = apply
        ? browser_url_pending_replacement_.length()
        : session ? session->get_result_text().length() : 0;
    logger::LogFormat(
        converted ? logger::Level::Debug : logger::Level::Info,
        L"Browser URL typed reconversion request: apply=%d, request_hr=0x%08X, session_hr=0x%08X, executed=%d, converted=%d, source_len=%zu, replacement_len=%zu",
        apply ? 1 : 0, request_hr, session_hr,
        session && session->action_executed() ? 1 : 0,
        converted ? 1 : 0, source_length, replacement_length);

    if (apply) {
        ClearBrowserUrlPendingReconversion();
    } else if (!request_succeeded) {
        ClearSensitiveState(false);
    }
    return converted;
}

bool VietnameseIME::HandleBrowserUrlTestKeyDown(
    ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) {
    if (!pfEaten || !pic || !IsBrowserProcess() ||
        typing_mode_ != 0 || IsSecureInputContext() ||
        IsCurrentAppBlocked(pic) ||
        IsBuiltInNativeBypassProcess(GetFocusedProcessName()) ||
        HasActiveComposition()) {
        return false;
    }

    const bool valid_key =
        IsValidCompositionKey(wParam, engine_.GetInputMethod());
    std::optional<BrowserTextInputMode> mode;
    if (IsBrowserUrlNativeModeActiveForContext(pic)) {
        mode = BrowserTextInputMode::UrlNativeReconversion;
    } else if (valid_key) {
        mode = DetectBrowserTextInputMode(pic);
        if (!mode) {
            ClearSensitiveState(false);
            *pfEaten = FALSE;
            return true;
        }
    } else {
        return false;
    }

    if (*mode != BrowserTextInputMode::UrlNativeReconversion) {
        return false;
    }

    ClearBrowserUrlPendingReconversion();
    engine_.SecureClear();
    direct_inline_display_length_ = 0;
    scintilla_direct_inline_byte_length_ = 0;
    scintilla_direct_inline_start_ = 0;

    wchar_t ch = valid_key ? TranslateKey(wParam, lParam) : 0;
    const bool has_candidate = ch != 0 &&
        TryBrowserUrlTypedReconversion(pic, ch, false);
    const BrowserUrlKeyAction action = DecideBrowserUrlKeyAction(
        *mode, false, valid_key, has_candidate);
    *pfEaten = action == BrowserUrlKeyAction::ApplyTypedReconversion
        ? TRUE : FALSE;
    return true;
}

bool VietnameseIME::HandleBrowserUrlKeyDown(
    ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) {
    if (!pfEaten || !pic || !IsBrowserProcess() ||
        typing_mode_ != 0 || IsSecureInputContext() ||
        IsCurrentAppBlocked(pic) ||
        IsBuiltInNativeBypassProcess(GetFocusedProcessName()) ||
        HasActiveComposition()) {
        return false;
    }

    const bool valid_key =
        IsValidCompositionKey(wParam, engine_.GetInputMethod());
    std::optional<BrowserTextInputMode> mode;
    if (IsBrowserUrlNativeModeActiveForContext(pic)) {
        mode = BrowserTextInputMode::UrlNativeReconversion;
    } else if (valid_key) {
        mode = DetectBrowserTextInputMode(pic);
        if (!mode) {
            ClearSensitiveState(false);
            *pfEaten = FALSE;
            return true;
        }
    } else {
        return false;
    }

    if (*mode != BrowserTextInputMode::UrlNativeReconversion) {
        return false;
    }

    engine_.SecureClear();
    direct_inline_display_length_ = 0;
    scintilla_direct_inline_byte_length_ = 0;
    scintilla_direct_inline_start_ = 0;
    if (!valid_key) {
        ClearBrowserUrlPendingReconversion();
        *pfEaten = FALSE;
        return true;
    }

    const wchar_t ch = TranslateKey(wParam, lParam);
    if (ch == 0) {
        ClearBrowserUrlPendingReconversion();
        *pfEaten = FALSE;
        return true;
    }

    bool applied = false;
    const bool pending_matches = browser_url_pending_context_ &&
        IsSameComObject(pic, browser_url_pending_context_.Get()) &&
        browser_url_pending_key_ == ch;
    if (pending_matches) {
        applied = TryBrowserUrlTypedReconversion(pic, ch, true);
    } else {
        ClearBrowserUrlPendingReconversion();
        if (TryBrowserUrlTypedReconversion(pic, ch, false)) {
            applied = TryBrowserUrlTypedReconversion(pic, ch, true);
        }
    }

    if (!applied) {
        ClearSensitiveState(false);
    }
    *pfEaten = applied ? TRUE : FALSE;
    return true;
}

bool VietnameseIME::IsConsoleProcess() const {
    HWND hwnd = GetBestFocusWindow();
    if (hwnd) {
        std::wstring class_name = GetClassNameOrEmpty(hwnd);
        if (class_name == L"ConsoleWindowClass" ||
            class_name == L"MinTTY" ||
            class_name == L"CASCADIA_HOSTING_WINDOW_CLASS") {
            return true;
        }
    }
    std::wstring process_name = host_process_name_.empty() ? GetFocusedProcessName() : host_process_name_;
    return (process_name == L"powershell.exe" ||
            process_name == L"pwsh.exe" ||
            process_name == L"cmd.exe" ||
            process_name == L"conhost.exe" ||
            process_name == L"wt.exe" ||
            process_name == L"windowsterminal.exe" ||
            process_name == L"openconsole.exe" ||
            process_name == L"wsl.exe" ||
            process_name == L"wslhost.exe" ||
            process_name == L"bash.exe" ||
            process_name == L"ssh.exe" ||
            process_name == L"mintty.exe");
}

bool VietnameseIME::IsVisualStudioProcess() const {
    return host_process_name_ == L"devenv.exe" || GetFocusedProcessName() == L"devenv.exe";
}

bool VietnameseIME::IsExcelApp() const {
    return host_process_name_ == L"excel.exe" || GetFocusedProcessName() == L"excel.exe";
}

bool VietnameseIME::IsVisualStudioShellNativeSurfaceFocused(ITfContext* pic) const {
    if (!IsVisualStudioProcess()) {
        return false;
    }
    HWND focus = GetBestFocusWindow();
    if (IsShellNativeSurfaceWindow(focus)) {
        return true;
    }
    return IsShellNativeSurfaceWindow(GetContextViewWindow(pic));
}

VietnameseIME::VisualStudioFocusKind VietnameseIME::GetVisualStudioFocusKind(ITfContext* pic) {
    if (!IsVisualStudioProcess()) {
        return VisualStudioFocusKind::NotVisualStudio;
    }
    const bool native_surface = IsVisualStudioShellNativeSurfaceFocused(pic);
    const VisualStudioFocusKind kind = native_surface
        ? VisualStudioFocusKind::ShellNativeSurface
        : VisualStudioFocusKind::TsfTextInput;
    HWND focus = GetBestFocusWindow();
    HWND context_hwnd = GetContextViewWindow(pic);
    const std::wstring focus_class = GetClassNameOrEmpty(focus);
    const std::wstring context_class = GetClassNameOrEmpty(context_hwnd);
    logger::LogFormat(logger::Level::Debug,
                      L"VisualStudioFocusKind=%s native_surface=%s focus_class=%s context_class=%s",
                      VisualStudioFocusKindName(static_cast<int>(kind)),
                      native_surface ? L"TRUE" : L"FALSE",
                      focus_class.empty() ? L"<empty>" : focus_class.c_str(),
                      context_class.empty() ? L"<empty>" : context_class.c_str());
    return kind;
}

VietnameseIME::NativeKeyReplayKind VietnameseIME::GetNativeKeyReplayKind(ITfContext* pic, WPARAM wParam) {
    if (wParam != VK_RETURN && wParam != VK_TAB) {
        return NativeKeyReplayKind::CommitOnly;
    }
    HWND focus = GetBestFocusWindow();
    HWND context_hwnd = GetContextViewWindow(pic);
    const bool native_app = (wParam == VK_RETURN) && IsNativeEnterReplayApp();
    const bool focus_single_line_edit = IsSingleLineWin32EditWindow(focus);
    const bool context_single_line_edit = IsSingleLineWin32EditWindow(context_hwnd);
    const bool replay_scope = (!focus_single_line_edit && !context_single_line_edit)
        ? ContextHasNativeKeyReplayInputScope(pic)
        : false;

    NativeKeyReplayKind kind = NativeKeyReplayKind::CommitOnly;
    if (native_app || focus_single_line_edit || context_single_line_edit || replay_scope) {
        kind = NativeKeyReplayKind::ReplayNativeKey;
    }

    std::wstring focus_class = GetClassNameOrEmpty(focus);
    std::wstring context_class = GetClassNameOrEmpty(context_hwnd);
    logger::LogFormat(logger::Level::Debug,
                      L"NativeKeyReplayKind=%s key=0x%04X native_app=%s focus_single_line_edit=%s context_single_line_edit=%s replay_scope=%s focus_class=%s context_class=%s",
                      NativeKeyReplayKindName(static_cast<int>(kind)),
                      static_cast<unsigned int>(wParam),
                      native_app ? L"TRUE" : L"FALSE",
                      focus_single_line_edit ? L"TRUE" : L"FALSE",
                      context_single_line_edit ? L"TRUE" : L"FALSE",
                      replay_scope ? L"TRUE" : L"FALSE",
                      focus_class.empty() ? L"<empty>" : focus_class.c_str(),
                      context_class.empty() ? L"<empty>" : context_class.c_str());
    return kind;
}

bool VietnameseIME::ContextHasNativeKeyReplayInputScope(ITfContext* pic) {
    if (!pic) {
        return false;
    }
    ComPtr<EditSession> session;
    session.Attach(new (std::nothrow) EditSession(this, pic, EditAction::DetectEnterReplayScope));
    if (!session) {
        return false;
    }
    HRESULT hr = S_OK;
    HRESULT hrReq = pic->RequestEditSession(client_id_, session.Get(), TF_ES_SYNC | TF_ES_READ, &hr);
    const bool result = SUCCEEDED(hrReq) && SUCCEEDED(hr) && session->is_convertible();
    logger::LogFormat(logger::Level::Debug,
                      L"ContextHasNativeKeyReplayInputScope: hrReq = 0x%08X, hr = 0x%08X, result = %s",
                      hrReq, hr, result ? L"TRUE" : L"FALSE");
    return result;
}

std::optional<core::ExcelFormulaInputKind> VietnameseIME::GetExcelFormulaInputKind(ITfContext* pic) {
    if (!pic || !IsExcelApp()) {
        return std::nullopt;
    }

    ComPtr<EditSession> session;
    session.Attach(new (std::nothrow) EditSession(this, pic, EditAction::ReadExcelFormulaPrefix));
    if (!session) {
        return std::nullopt;
    }

    HRESULT hr = S_OK;
    HRESULT hrReq = pic->RequestEditSession(client_id_, session.Get(), TF_ES_SYNC | TF_ES_READ, &hr);
    if (SUCCEEDED(hrReq) && SUCCEEDED(hr) && session->is_convertible()) {
        const std::wstring& prefix = session->get_result_text();
        return core::ClassifyExcelFormulaPrefix(prefix, false);
    }

    return std::nullopt;
}

core::ExcelFormulaSessionState VietnameseIME::GetExcelFormulaSessionState(ITfContext* pic) const {
    if (!pic || !IsExcelApp()) {
        return core::ExcelFormulaSessionState::Idle;
    }
    return excel_formula_state_;
}

core::ExcelFormulaSessionState VietnameseIME::ComputeExcelFormulaStateFromBuffer() const noexcept {
    return excel_formula_state_;
}

void VietnameseIME::PrepareExcelFormulaSession(ITfContext* pic, WPARAM wParam, LPARAM lParam) {
    if (!pic || !IsExcelApp() || HasTextShortcutModifier()) {
        return;
    }

    const bool is_finish_key =
        wParam == VK_RETURN || wParam == VK_TAB || wParam == VK_ESCAPE ||
        wParam == VK_UP || wParam == VK_DOWN || wParam == VK_PRIOR ||
        wParam == VK_NEXT;
    if (is_finish_key) {
        ResetExcelFormulaSession(L"finish_key");
        return;
    }

    if (wParam == VK_DELETE) {
        ResetExcelFormulaSession(L"delete_key");
        return;
    }

    const wchar_t ch = TranslateKey(wParam, lParam);

    if (ch == L'=') {
        if (excel_formula_state_ == core::ExcelFormulaSessionState::Idle) {
            const bool local_start_eligible =
                excel_formula_start_eligible_;
            if (!core::ShouldStartExcelFormulaAtEntry(
                    local_start_eligible)) {
                excel_formula_start_eligible_ = false;
                logger::LogFormat(
                    logger::Level::Info,
                    L"PrepareExcelFormulaSession: kept '=' as cell text (local_start=%d)",
                    local_start_eligible ? 1 : 0);
                return;
            }
            excel_formula_state_ = core::ExcelFormulaSessionState::FormulaSyntax;
            excel_formula_chars_ = 1;
            excel_quote_chars_ = 0;
            last_quoted_chars_ = 0;
            excel_formula_chars_after_closed_quote_ = 0;
            excel_formula_start_eligible_ = false;
            excel_has_closed_quote_ = false;
            logger::Log(logger::Level::Info, L"PrepareExcelFormulaSession: started formula (=)");
            return;
        } else if (excel_formula_state_ == core::ExcelFormulaSessionState::FormulaSyntax) {
            ++excel_formula_chars_;
            if (excel_has_closed_quote_) {
                ++excel_formula_chars_after_closed_quote_;
            }
            return;
        }
    }

    if (excel_formula_state_ == core::ExcelFormulaSessionState::Idle) {
        if (ch != 0 && wParam != VK_BACK) {
            excel_formula_start_eligible_ = false;
        }
        return;
    }

    if (ch == L'"') {
        if (excel_formula_state_ == core::ExcelFormulaSessionState::FormulaSyntax) {
            ++excel_formula_chars_;
            excel_formula_state_ = core::ExcelFormulaSessionState::QuotedText;
            excel_quote_chars_ = 0;
            logger::LogFormat(
                logger::Level::Info,
                L"PrepareExcelFormulaSession: enter QuotedText (\"), formula_chars=%zu",
                excel_formula_chars_);
        } else if (excel_formula_state_ == core::ExcelFormulaSessionState::QuotedText) {
            ++excel_formula_chars_;
            last_quoted_chars_ = excel_quote_chars_;
            excel_has_closed_quote_ = true;
            excel_formula_chars_after_closed_quote_ = 0;
            excel_formula_state_ = core::ExcelFormulaSessionState::FormulaSyntax;
            excel_quote_chars_ = 0;
            logger::LogFormat(
                logger::Level::Info,
                L"PrepareExcelFormulaSession: exit QuotedText (\"), formula_chars=%zu, saved_quoted_chars=%zu",
                excel_formula_chars_, last_quoted_chars_);
        }
        return;
    }

    if (wParam == VK_BACK) {
        if (!HasActiveComposition()) {
            if (excel_formula_state_ == core::ExcelFormulaSessionState::FormulaSyntax) {
                if (core::ShouldReenterExcelQuotedTextOnBackspace(
                        excel_has_closed_quote_,
                        excel_formula_chars_after_closed_quote_)) {
                    if (excel_formula_chars_ > 0) {
                        --excel_formula_chars_;
                    }
                    excel_formula_state_ = core::ExcelFormulaSessionState::QuotedText;
                    excel_quote_chars_ = last_quoted_chars_;
                    last_quoted_chars_ = 0;
                    excel_has_closed_quote_ = false;
                    excel_formula_chars_after_closed_quote_ = 0;
                    logger::LogFormat(
                        logger::Level::Info,
                        L"PrepareExcelFormulaSession: VK_BACK re-entered QuotedText, quote_chars=%zu, formula_chars=%zu",
                        excel_quote_chars_, excel_formula_chars_);
                } else {
                    if (excel_has_closed_quote_ &&
                        excel_formula_chars_after_closed_quote_ > 0) {
                        --excel_formula_chars_after_closed_quote_;
                    }
                    if (excel_formula_chars_ > 1) {
                        --excel_formula_chars_;
                        logger::LogFormat(
                            logger::Level::Info,
                            L"PrepareExcelFormulaSession: VK_BACK in FormulaSyntax, remaining formula_chars=%zu",
                            excel_formula_chars_);
                    } else {
                        excel_formula_chars_ = 0;
                        excel_formula_state_ = core::ExcelFormulaSessionState::Idle;
                        excel_formula_start_eligible_ = true;
                        excel_has_closed_quote_ = false;
                        excel_formula_chars_after_closed_quote_ = 0;
                        logger::Log(
                            logger::Level::Info,
                            L"PrepareExcelFormulaSession: VK_BACK deleted '=', reset to Idle (Vietnamese mode)");
                    }
                }
            } else if (excel_formula_state_ == core::ExcelFormulaSessionState::QuotedText) {
                if (excel_quote_chars_ > 0) {
                    --excel_quote_chars_;
                    logger::LogFormat(
                        logger::Level::Info,
                        L"PrepareExcelFormulaSession: VK_BACK in QuotedText, remaining quote_chars=%zu",
                        excel_quote_chars_);
                } else {
                    if (excel_formula_chars_ > 0) {
                        --excel_formula_chars_;
                    }
                    excel_formula_state_ = core::ExcelFormulaSessionState::FormulaSyntax;
                    logger::LogFormat(
                        logger::Level::Info,
                        L"PrepareExcelFormulaSession: VK_BACK deleted opening quote (\") -> FormulaSyntax, formula_chars=%zu",
                        excel_formula_chars_);
                }
            }
        }
        return;
    }

    if (excel_formula_state_ == core::ExcelFormulaSessionState::FormulaSyntax) {
        if (ch != 0) {
            ++excel_formula_chars_;
            if (excel_has_closed_quote_) {
                ++excel_formula_chars_after_closed_quote_;
            }
        }
    } else if (excel_formula_state_ == core::ExcelFormulaSessionState::QuotedText) {
        if (ch != 0 && !IsValidCompositionKey(wParam, engine_.GetInputMethod())) {
            ++excel_quote_chars_;
        }
    }
}

bool VietnameseIME::TryAdoptPendingExcelFormulaContext(ITfContext* pic) {
    return false;
}

void VietnameseIME::ObserveExcelNativeChar(
    ITfContext* pic,
    WPARAM wParam,
    LPARAM lParam,
    const wchar_t* source) {
}

void VietnameseIME::ObserveExcelNativeChar(
    ITfContext* pic,
    wchar_t ch,
    const wchar_t* source) {
}

void VietnameseIME::SetExcelFormulaSessionState(ITfContext* pic, core::ExcelFormulaSessionState state, const wchar_t* source) {
    if (pic && IsExcelApp()) {
        logger::LogFormat(logger::Level::Info, L"SetExcelFormulaSessionState: old_state=%d, new_state=%d, source=%s",
                          static_cast<int>(excel_formula_state_), static_cast<int>(state), source);
    }
    excel_formula_state_ = state;
}

void VietnameseIME::ResetExcelFormulaSession(const wchar_t* reason) noexcept {
    if (excel_formula_state_ != core::ExcelFormulaSessionState::Idle ||
        excel_formula_chars_ > 0) {
        logger::LogFormat(
            logger::Level::Info,
            L"ResetExcelFormulaSession: old_state=%d, formula_chars=%zu -> Idle, reason=%s",
            static_cast<int>(excel_formula_state_), excel_formula_chars_, reason);
    }
    excel_formula_state_ = core::ExcelFormulaSessionState::Idle;
    excel_formula_chars_ = 0;
    excel_quote_chars_ = 0;
    last_quoted_chars_ = 0;
    excel_formula_chars_after_closed_quote_ = 0;
    excel_formula_start_eligible_ = true;
    excel_has_closed_quote_ = false;
}

bool VietnameseIME::IsWordTsfInlineApp() const {
    return host_process_name_ == L"winword.exe" || GetFocusedProcessName() == L"winword.exe" ||
           host_process_name_ == L"powerpnt.exe" || GetFocusedProcessName() == L"powerpnt.exe";
}

bool VietnameseIME::IsInkscapeApp() const {
    return host_process_name_ == L"inkscape.exe" || GetFocusedProcessName() == L"inkscape.exe";
}

bool VietnameseIME::IsInkscapeKeySuppressed(WPARAM wParam) const {
    if (wParam == last_inkscape_commit_vk_) {
        if (::GetTickCount64() - last_inkscape_commit_time_ < 100) {
            return true;
        }
    }
    return false;
}

bool VietnameseIME::ProcessInkscapeNonCompositionKey(WPARAM wParam, LPARAM lParam) {
    HWND hwnd = GetBestFocusWindow();
    if (!hwnd) {
        return false;
    }

    ResetDirectInlineState();

    wchar_t ch = TranslateKey(wParam, lParam);

    // For printable characters (including Space, letters, digits, punctuation),
    // we only post the WM_CHAR message. This avoids triggering global shortcuts
    // (such as Spacebar switching tools) in Inkscape.
    // For control keys (Enter, Tab, Escape, etc.), we post the full KEYDOWN/KEYUP sequence.
    if (ch >= 32 && ch != 127) {
        ::PostMessageW(hwnd, WM_CHAR, ch, lParam | (1 << 28));
    } else {
        LPARAM customLParam = lParam | (1 << 28);
        ::PostMessageW(hwnd, WM_KEYDOWN, wParam, customLParam);
        if (ch != 0) {
            ::PostMessageW(hwnd, WM_CHAR, ch, customLParam);
        }
        ::PostMessageW(hwnd, WM_KEYUP, wParam, customLParam | 0xC0000000);
    }
    return true;
}

bool VietnameseIME::IsWordTsfInlineActive() const {
    return direct_inline_display_length_ > 0 && IsWordTsfInlineApp();
}

bool VietnameseIME::IsExplorerProcess() const {
    return host_process_name_ == L"explorer.exe" || GetFocusedProcessName() == L"explorer.exe";
}

bool VietnameseIME::IsExplorerWin32EditFocused() const {
    HWND hwnd = GetBestFocusWindow();
    if (!hwnd) {
        return false;
    }

    std::wstring class_name = GetClassNameOrEmpty(hwnd);
    if (_wcsicmp(class_name.c_str(), L"Edit") != 0) {
        return false;
    }

    std::wstring process_name = host_process_name_.empty() ? GetFocusedProcessName() : host_process_name_;
    if (process_name == L"explorer.exe" || process_name == L"filezilla.exe" || process_name == L"antigravity.exe") {
        return true;
    }

    return WindowOrAncestorHasClass(hwnd, L"#32770");
}

bool VietnameseIME::IsExplorerNativeSurfaceFocused(ITfContext* pic) const {
    HWND focus = GetBestFocusWindow();
    if (IsExplorerNativeSurfaceWindow(focus)) {
        return true;
    }

    HWND context_hwnd = GetContextViewWindow(pic);
    return IsExplorerNativeSurfaceWindow(context_hwnd);
}

bool VietnameseIME::ExplorerFocusedThreadHasCaret() const {
    if (!IsExplorerProcess()) {
        return false;
    }

    GUITHREADINFO info{};
    if (!GetForegroundGuiThreadInfo(&info)) {
        return false;
    }

    return info.hwndCaret != nullptr;
}

bool VietnameseIME::ExplorerContextHasTextInputScope(ITfContext* pic) {
    if (!pic || !IsExplorerProcess()) {
        return false;
    }

    ComPtr<EditSession> session;
    session.Attach(new (std::nothrow) EditSession(this, pic, EditAction::DetectTextInputScope));
    if (!session) {
        return false;
    }

    HRESULT hr = S_OK;
    HRESULT hrReq = pic->RequestEditSession(client_id_, session.Get(), TF_ES_SYNC | TF_ES_READ, &hr);
    const bool result = SUCCEEDED(hrReq) && SUCCEEDED(hr) && session->is_convertible();
    logger::LogFormat(logger::Level::Debug,
                      L"ExplorerContextHasTextInputScope: hrReq = 0x%08X, hr = 0x%08X, result = %s",
                      hrReq,
                      hr,
                      result ? L"TRUE" : L"FALSE");
    return result;
}

VietnameseIME::ExplorerFocusKind VietnameseIME::GetExplorerFocusKind(ITfContext* pic) {
    // Check if focusing a native list/tree surface regardless of process (e.g. browser file dialogs)
    const bool focused_win32_edit = ClassNameEquals(GetBestFocusWindow(), L"Edit");
    if (ShouldTreatShellSurfaceAsNative(focused_win32_edit, IsExplorerNativeSurfaceFocused(pic))) {
        return ExplorerFocusKind::NativeSurface;
    }

    if (!IsExplorerProcess()) {
        return ExplorerFocusKind::NotExplorer;
    }
    const bool win32_edit = IsExplorerWin32EditFocused();
    const bool native_surface = false; // Checked above and is false
    const bool input_scope = ExplorerContextHasTextInputScope(pic);
    const bool has_caret = ExplorerFocusedThreadHasCaret();
    ExplorerFocusKind kind = ExplorerFocusKind::NativeSurface;

    if (win32_edit) {
        kind = ExplorerFocusKind::Win32Edit;
    } else if (!native_surface) {
        kind = ExplorerFocusKind::TsfTextInput;
    }

    GUITHREADINFO info{};
    const bool has_gui_info = GetForegroundGuiThreadInfo(&info);
    HWND focus = GetBestFocusWindow();
    HWND context_hwnd = GetContextViewWindow(pic);
    std::wstring focus_class = GetClassNameOrEmpty(focus);
    std::wstring context_class = GetClassNameOrEmpty(context_hwnd);
    std::wstring caret_class = has_gui_info ? GetClassNameOrEmpty(info.hwndCaret) : L"";
    std::wstring active_class = has_gui_info ? GetClassNameOrEmpty(info.hwndActive) : L"";
    logger::LogFormat(logger::Level::Debug,
                      L"ExplorerFocusKind=%s win32_edit=%s native_surface=%s input_scope=%s has_caret=%s focus_class=%s context_class=%s caret_class=%s active_class=%s flags=0x%08X",
                      ExplorerFocusKindName(static_cast<int>(kind)),
                      win32_edit ? L"TRUE" : L"FALSE",
                      native_surface ? L"TRUE" : L"FALSE",
                      input_scope ? L"TRUE" : L"FALSE",
                      has_caret ? L"TRUE" : L"FALSE",
                      focus_class.empty() ? L"<empty>" : focus_class.c_str(),
                      context_class.empty() ? L"<empty>" : context_class.c_str(),
                      caret_class.empty() ? L"<empty>" : caret_class.c_str(),
                      active_class.empty() ? L"<empty>" : active_class.c_str(),
                      has_gui_info ? info.flags : 0);
    return kind;
}

bool VietnameseIME::ProcessExplorerEditChar(wchar_t ch) {
    if (ch == 0 || IsSecureInputContext() || !IsExplorerWin32EditFocused()) {
        return false;
    }

    HWND hwnd = GetBestFocusWindow();
    DWORD sel_start = 0;
    DWORD sel_end = 0;
    ::SendMessageW(hwnd, EM_GETSEL, reinterpret_cast<WPARAM>(&sel_start), reinterpret_cast<LPARAM>(&sel_end));

    const bool can_replace_previous =
        direct_inline_display_length_ > 0 &&
        sel_start == sel_end &&
        sel_start >= direct_inline_display_length_;

    if (!can_replace_previous) {
        CaptureWin32ShorthandSelection(
            hwnd, static_cast<size_t>(sel_start),
            static_cast<size_t>(sel_end), ch);
        engine_.Clear();
        direct_inline_display_length_ = 0;
    }

    engine_.ProcessKey(ch);
    std::wstring display = engine_.GetDisplayString();
    if (display.empty()) {
        SecureEraseString(display);
        return false;
    }

    DWORD replace_start = sel_start;
    DWORD replace_end = sel_end;
    if (can_replace_previous) {
        replace_start = sel_start - static_cast<DWORD>(direct_inline_display_length_);
    }

    ::SendMessageW(hwnd, EM_SETSEL, replace_start, replace_end);
    ::SendMessageW(hwnd, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(display.c_str()));
    direct_inline_display_length_ = display.length();
    SecureEraseString(display);
    return true;
}

bool VietnameseIME::ProcessExplorerEditBackspace() {
    if (IsSecureInputContext() || !IsExplorerWin32EditFocused() || !HasDirectInlineState()) {
        return false;
    }

    HWND hwnd = GetBestFocusWindow();
    DWORD sel_start = 0;
    DWORD sel_end = 0;
    ::SendMessageW(hwnd, EM_GETSEL, reinterpret_cast<WPARAM>(&sel_start), reinterpret_cast<LPARAM>(&sel_end));
    if (sel_start != sel_end || sel_start < direct_inline_display_length_) {
        ResetDirectInlineState();
        return false;
    }

    engine_.BackspaceDisplayChar();
    std::wstring raw = engine_.GetRawString();
    std::wstring display = engine_.GetDisplayString();

    DWORD replace_start = sel_start - static_cast<DWORD>(direct_inline_display_length_);
    ::SendMessageW(hwnd, EM_SETSEL, replace_start, sel_end);
    ::SendMessageW(hwnd, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(display.c_str()));

    if (raw.empty() || display.empty()) {
        ResetDirectInlineState();
    } else {
        direct_inline_display_length_ = display.length();
    }

    SecureEraseString(raw);
    SecureEraseString(display);
    return true;
}

bool VietnameseIME::IsNotepadPlusPlusDirectInlineFocused() const {
    std::wstring process_name = host_process_name_.empty() ? GetFocusedProcessName() : host_process_name_;
    HWND hwnd = GetBestFocusWindow();
    if (!hwnd) return false;
    std::wstring class_name = GetClassNameOrEmpty(hwnd);
    
    if (ShouldUseNotepadPlusPlusDirectInline(process_name, class_name)) {
        return true;
    }
    
    bool is_commit = false;
    if (IsCustomDirectApp(&is_commit)) {
        if (!is_commit) {
            return class_name == L"Scintilla" || class_name == L"Edit";
        }
    }
    
    return false;
}

bool VietnameseIME::IsCustomDirectApp(bool* is_commit) const {
    std::wstring process_name = host_process_name_.empty() ? GetFocusedProcessName() : host_process_name_;
    if (process_name.empty()) {
        return false;
    }
    std::wstring norm_name = NormalizeProcessName(process_name);
    for (const auto& app : direct_apps_) {
        if (app.process_name == norm_name) {
            if (is_commit) {
                *is_commit = app.is_commit;
            }
            return true;
        }
    }
    return false;
}

bool VietnameseIME::HasNotepadPlusPlusNativeSelection() const {
    HWND hwnd = GetBestFocusWindow();
    std::wstring class_name = GetClassNameOrEmpty(hwnd);
    if (_wcsicmp(class_name.c_str(), L"Edit") == 0) {
        DWORD sel_start = 0;
        DWORD sel_end = 0;
        ::SendMessageW(hwnd, EM_GETSEL, reinterpret_cast<WPARAM>(&sel_start), reinterpret_cast<LPARAM>(&sel_end));
        return sel_start != sel_end;
    }
    if (_wcsicmp(class_name.c_str(), L"Scintilla") == 0) {
        constexpr LRESULT SCI_GETSELECTIONSTART = 2143;
        constexpr LRESULT SCI_GETSELECTIONEND = 2145;
        LRESULT sel_start = ::SendMessageW(hwnd, SCI_GETSELECTIONSTART, 0, 0);
        LRESULT sel_end = ::SendMessageW(hwnd, SCI_GETSELECTIONEND, 0, 0);
        return sel_start != sel_end;
    }
    return false;
}

bool VietnameseIME::ProcessNotepadPlusPlusDirectChar(wchar_t ch) {
    if (!IsNotepadPlusPlusDirectInlineFocused()) {
        return false;
    }

    HWND hwnd = GetBestFocusWindow();
    std::wstring class_name = GetClassNameOrEmpty(hwnd);
    if (_wcsicmp(class_name.c_str(), L"Edit") == 0) {
        return ProcessWin32EditDirectChar(hwnd, ch);
    }
    if (_wcsicmp(class_name.c_str(), L"Scintilla") == 0) {
        return ProcessScintillaDirectChar(hwnd, ch);
    }
    return false;
}

bool VietnameseIME::ProcessNotepadPlusPlusDirectBackspace() {
    if (!IsNotepadPlusPlusDirectInlineFocused()) {
        return false;
    }

    HWND hwnd = GetBestFocusWindow();
    std::wstring class_name = GetClassNameOrEmpty(hwnd);
    if (_wcsicmp(class_name.c_str(), L"Edit") == 0) {
        return ProcessWin32EditDirectBackspace(hwnd);
    }
    if (_wcsicmp(class_name.c_str(), L"Scintilla") == 0) {
        return ProcessScintillaDirectBackspace(hwnd);
    }
    return false;
}

bool VietnameseIME::ProcessNotepadPlusPlusDirectCommitChar(wchar_t ch) {
    if (!IsNotepadPlusPlusDirectInlineFocused()) {
        return false;
    }

    HWND hwnd = GetBestFocusWindow();
    std::wstring class_name = GetClassNameOrEmpty(hwnd);
    if (_wcsicmp(class_name.c_str(), L"Edit") == 0) {
        return ProcessWin32EditDirectCommitChar(hwnd, ch);
    }
    if (_wcsicmp(class_name.c_str(), L"Scintilla") == 0) {
        return ProcessScintillaDirectCommitChar(hwnd, ch);
    }
    return false;
}

bool VietnameseIME::ProcessWin32EditDirectChar(HWND hwnd, wchar_t ch) {
    if (!hwnd || ch == 0 || IsSecureInputContext() || !ClassNameEquals(hwnd, L"Edit")) {
        return false;
    }

    DWORD sel_start = 0;
    DWORD sel_end = 0;
    ::SendMessageW(hwnd, EM_GETSEL, reinterpret_cast<WPARAM>(&sel_start), reinterpret_cast<LPARAM>(&sel_end));

    const bool can_replace_previous =
        direct_inline_display_length_ > 0 &&
        sel_start == sel_end &&
        sel_start >= direct_inline_display_length_;

    if (!can_replace_previous) {
        CaptureWin32ShorthandSelection(
            hwnd, static_cast<size_t>(sel_start),
            static_cast<size_t>(sel_end), ch);
        engine_.Clear();
        direct_inline_display_length_ = 0;
    }

    engine_.ProcessKey(ch);
    std::wstring display = engine_.GetDisplayString();
    if (display.empty()) {
        SecureEraseString(display);
        return false;
    }

    DWORD replace_start = sel_start;
    DWORD replace_end = sel_end;
    if (can_replace_previous) {
        replace_start = sel_start - static_cast<DWORD>(direct_inline_display_length_);
    }

    ::SendMessageW(hwnd, EM_SETSEL, replace_start, replace_end);
    ::SendMessageW(hwnd, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(display.c_str()));
    direct_inline_display_length_ = display.length();
    SecureEraseString(display);
    return true;
}

bool VietnameseIME::ProcessWin32EditDirectBackspace(HWND hwnd) {
    if (!hwnd || IsSecureInputContext() || !ClassNameEquals(hwnd, L"Edit") || !HasDirectInlineState()) {
        return false;
    }

    DWORD sel_start = 0;
    DWORD sel_end = 0;
    ::SendMessageW(hwnd, EM_GETSEL, reinterpret_cast<WPARAM>(&sel_start), reinterpret_cast<LPARAM>(&sel_end));
    if (sel_start != sel_end || sel_start < direct_inline_display_length_) {
        ResetDirectInlineState();
        return false;
    }

    engine_.BackspaceDisplayChar();
    std::wstring raw = engine_.GetRawString();
    std::wstring display = engine_.GetDisplayString();

    DWORD replace_start = sel_start - static_cast<DWORD>(direct_inline_display_length_);
    ::SendMessageW(hwnd, EM_SETSEL, replace_start, sel_end);
    ::SendMessageW(hwnd, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(display.c_str()));

    if (raw.empty() || display.empty()) {
        ResetDirectInlineState();
    } else {
        direct_inline_display_length_ = display.length();
    }

    SecureEraseString(raw);
    SecureEraseString(display);
    return true;
}

bool VietnameseIME::ProcessWin32EditDirectCommitChar(HWND hwnd, wchar_t ch) {
    if (!hwnd || ch == 0 || IsSecureInputContext() || !ClassNameEquals(hwnd, L"Edit")) {
        return false;
    }

    std::wstring raw = engine_.GetRawString();
    std::wstring pre_speller =
        engine_.GetPreCorrectionDisplayString();
    core::EngineDisplayResult engine_display = engine_.GetDisplayResult();
    std::wstring committed_display = engine_display.text;
    CommitUndoEntry::TransformKind transform_kind =
        engine_display.HasSpellerCorrection()
            ? CommitUndoEntry::TransformKind::SpellerCorrection
            : CommitUndoEntry::TransformKind::None;
    std::wstring previous_token;
    if (enable_fuzzy_input_ && ch == L' ' && !IsExcelApp() &&
        direct_inline_display_length_ == committed_display.length()) {
        DWORD context_start = 0;
        DWORD context_end = 0;
        ::SendMessageW(
            hwnd, EM_GETSEL, reinterpret_cast<WPARAM>(&context_start),
            reinterpret_cast<LPARAM>(&context_end));
        if (context_start == context_end && context_end > 0 &&
            static_cast<size_t>(context_end) <=
                kMaxCommitUndoDisplayChars) {
            std::vector<wchar_t> prefix(
                static_cast<size_t>(context_end) + 1, L'\0');
            const int copied = ::GetWindowTextW(
                hwnd, prefix.data(), static_cast<int>(prefix.size()));
            if (copied >= 0 &&
                static_cast<size_t>(copied) >=
                    static_cast<size_t>(context_end)) {
                const std::wstring_view prefix_view(
                    prefix.data(), static_cast<size_t>(context_end));
                if (const auto previous =
                        core::ExtractImmediatePreviousToken(
                            prefix_view, committed_display)) {
                    previous_token.assign(*previous);
                }
            }
            SecureEraseVector(prefix);
        }
    }
    DirectCommitTransformDecision transform =
        BuildDirectCommitTransformDecision(
            raw, committed_display, pre_speller, ch, previous_token,
            !previous_token.empty(), true);
    core::CommitTransformDecision& decision = transform.decision;

    const bool rewrite_requested =
        decision.RequiresRewrite() ||
        transform.caret_offset.has_value();
    bool rewrite_succeeded = false;
    std::wstring original_text_for_undo;
    std::optional<core::DirectCommitRewriteSpan> applied_rewrite_span;
    if (rewrite_requested && direct_inline_display_length_ > 0) {
        DWORD sel_start = 0;
        DWORD sel_end = 0;
        ::SendMessageW(
            hwnd, EM_GETSEL, reinterpret_cast<WPARAM>(&sel_start),
            reinterpret_cast<LPARAM>(&sel_end));
        const auto rewrite_span = core::ComputeDirectCommitRewriteSpan(
            static_cast<size_t>(sel_end), decision.expected_source.length(),
            decision.text.length());
        bool host_text_matches = false;
        if (sel_start == sel_end && rewrite_span &&
            direct_inline_display_length_ == committed_display.length() &&
            static_cast<size_t>(sel_end) <=
                kMaxCommitUndoDisplayChars) {
            std::vector<wchar_t> host_prefix(
                static_cast<size_t>(sel_end) + 1, L'\0');
            const int copied = ::GetWindowTextW(
                hwnd, host_prefix.data(),
                static_cast<int>(host_prefix.size()));
            if (copied >= 0 &&
                static_cast<size_t>(copied) >=
                    static_cast<size_t>(sel_end)) {
                const std::wstring_view prefix_view(
                    host_prefix.data(), static_cast<size_t>(copied));
                const auto verified = FindVerifiedTextBeforeCaret(
                    prefix_view, static_cast<size_t>(sel_end),
                    decision.expected_source);
                host_text_matches = verified &&
                    verified->start == rewrite_span->start &&
                    verified->end == rewrite_span->old_end;
            }
            SecureEraseVector(host_prefix);
        }
        if (host_text_matches) {
            ::SendMessageW(
                hwnd, EM_SETSEL, rewrite_span->start,
                rewrite_span->old_end);
            ::SendMessageW(
                hwnd, EM_REPLACESEL, TRUE,
                reinterpret_cast<LPARAM>(decision.text.c_str()));
            DWORD actual_start = 0;
            DWORD actual_end = 0;
            ::SendMessageW(
                hwnd, EM_GETSEL,
                reinterpret_cast<WPARAM>(&actual_start),
                reinterpret_cast<LPARAM>(&actual_end));
            rewrite_succeeded =
                actual_start == actual_end &&
                static_cast<size_t>(actual_end) ==
                    rewrite_span->new_caret;
            if (rewrite_succeeded) {
                applied_rewrite_span = rewrite_span;
                committed_display = decision.text;
                transform_kind = decision.transform_kind;
                original_text_for_undo = decision.undo_text;
                direct_inline_display_length_ = committed_display.length();
            }
        }
    }

    if ((!rewrite_requested || rewrite_succeeded) &&
        !transform.caret_offset) {
        CaptureCommitUndoDirectInline(
            hwnd, false, committed_display, transform_kind,
            original_text_for_undo);
    } else {
        ClearLastCommitUndo();
    }
    SecureEraseString(previous_token);
    SecureEraseString(pre_speller);
    SecureEraseString(original_text_for_undo);
    ResetDirectInlineState();
    wchar_t text[2] = { ch, L'\0' };
    ::SendMessageW(hwnd, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(text));
    if (rewrite_succeeded && transform.caret_offset &&
        applied_rewrite_span &&
        *transform.caret_offset <= decision.text.length()) {
        const size_t boundary_end =
            applied_rewrite_span->start + decision.text.length() + 1;
        DWORD actual_start = 0;
        DWORD actual_end = 0;
        ::SendMessageW(
            hwnd, EM_GETSEL, reinterpret_cast<WPARAM>(&actual_start),
            reinterpret_cast<LPARAM>(&actual_end));
        const size_t caret = applied_rewrite_span->start +
            *transform.caret_offset;
        bool cursor_applied =
            actual_start == actual_end &&
            static_cast<size_t>(actual_end) == boundary_end;
        if (cursor_applied) {
            ::SendMessageW(hwnd, EM_SETSEL, caret, caret);
            ::SendMessageW(
                hwnd, EM_GETSEL, reinterpret_cast<WPARAM>(&actual_start),
                reinterpret_cast<LPARAM>(&actual_end));
            cursor_applied = actual_start == actual_end &&
                static_cast<size_t>(actual_end) == caret;
        }
        if (!cursor_applied) {
            const size_t replacement_end = applied_rewrite_span->start +
                decision.text.length();
            ::SendMessageW(
                hwnd, EM_SETSEL, applied_rewrite_span->start,
                replacement_end);
            ::SendMessageW(
                hwnd, EM_REPLACESEL, TRUE,
                reinterpret_cast<LPARAM>(engine_display.text.c_str()));
            const size_t rollback_caret = applied_rewrite_span->start +
                engine_display.text.length() + 1;
            ::SendMessageW(
                hwnd, EM_SETSEL, rollback_caret, rollback_caret);
            ::SendMessageW(
                hwnd, EM_GETSEL, reinterpret_cast<WPARAM>(&actual_start),
                reinterpret_cast<LPARAM>(&actual_end));
            const bool rolled_back = actual_start == actual_end &&
                static_cast<size_t>(actual_end) == rollback_caret;
            logger::LogFormat(
                logger::Level::Warning,
                L"Win32 shorthand CURSOR transaction failed: rollback=%d",
                rolled_back ? 1 : 0);
            ClearLastCommitUndo();
        }
    }
    if (last_commit_undo_ && !last_commit_undo_->is_tsf &&
        last_commit_undo_->hwnd == hwnd) {
        last_commit_undo_->committed_with_ascii_space = ch == L' ';
    }
    SecureEraseString(decision.text);
    SecureEraseString(decision.expected_source);
    SecureEraseString(decision.undo_text);
    SecureEraseString(committed_display);
    SecureEraseString(engine_display.text);
    SecureEraseString(raw);
    SecureEraseBuffer(text, std::size(text));
    return true;
}

bool VietnameseIME::ProcessScintillaDirectChar(HWND hwnd, wchar_t ch) {
    if (!hwnd || ch == 0 || IsSecureInputContext() || !ClassNameEquals(hwnd, L"Scintilla")) {
        return false;
    }

    constexpr LRESULT SCI_GETSELECTIONSTART = 2143;
    constexpr LRESULT SCI_GETSELECTIONEND = 2145;
    constexpr LRESULT SCI_SETSEL = 2160;
    constexpr LRESULT SCI_REPLACESEL = 2170;

    LRESULT sel_start_lr = ::SendMessageW(hwnd, SCI_GETSELECTIONSTART, 0, 0);
    LRESULT sel_end_lr = ::SendMessageW(hwnd, SCI_GETSELECTIONEND, 0, 0);
    if (sel_start_lr < 0 || sel_end_lr < 0) {
        return false;
    }
    size_t sel_start = static_cast<size_t>((std::min)(sel_start_lr, sel_end_lr));
    size_t sel_end = static_cast<size_t>((std::max)(sel_start_lr, sel_end_lr));

    const bool can_replace_previous = CanContinueScintillaDirectInline(
        scintilla_direct_inline_byte_length_ > 0,
        scintilla_direct_inline_start_,
        sel_start,
        sel_end);

    if (!can_replace_previous) {
        CaptureScintillaShorthandSelection(
            hwnd, sel_start, sel_end, ch);
        engine_.Clear();
        direct_inline_display_length_ = 0;
        scintilla_direct_inline_byte_length_ = 0;
        scintilla_direct_inline_start_ = 0;
    }

    engine_.ProcessKey(ch);
    std::wstring display = engine_.GetDisplayString();

    std::string display_utf8;
    if (!ConvertWideToUtf8(display, display_utf8)) {
        SecureEraseString(display);
        SecureEraseStringUtf8(display_utf8);
        return false;
    }

    size_t replace_start = sel_start;
    size_t replace_end = sel_end;
    if (can_replace_previous) {
        replace_start = scintilla_direct_inline_start_;
        replace_end = scintilla_direct_inline_start_ + scintilla_direct_inline_byte_length_;
    }

    ::SendMessageW(hwnd, SCI_SETSEL, replace_start, replace_end);
    ::SendMessageW(hwnd, SCI_REPLACESEL, 0, reinterpret_cast<LPARAM>(display_utf8.c_str()));

    if (display_utf8.empty()) {
        ResetDirectInlineState();
    } else {
        if (!can_replace_previous) {
            scintilla_direct_inline_start_ = sel_start;
        }
        scintilla_direct_inline_byte_length_ = display_utf8.length();
        direct_inline_display_length_ = display.length();
    }

    SecureEraseString(display);
    SecureEraseStringUtf8(display_utf8);
    return true;
}

bool VietnameseIME::ProcessScintillaDirectBackspace(HWND hwnd) {
    if (!hwnd || IsSecureInputContext() || !ClassNameEquals(hwnd, L"Scintilla") || !HasDirectInlineState()) {
        return false;
    }

    constexpr LRESULT SCI_GETSELECTIONSTART = 2143;
    constexpr LRESULT SCI_GETSELECTIONEND = 2145;
    constexpr LRESULT SCI_SETSEL = 2160;
    constexpr LRESULT SCI_REPLACESEL = 2170;

    LRESULT sel_start_lr = ::SendMessageW(hwnd, SCI_GETSELECTIONSTART, 0, 0);
    LRESULT sel_end_lr = ::SendMessageW(hwnd, SCI_GETSELECTIONEND, 0, 0);
    if (sel_start_lr < 0 || sel_end_lr < 0) {
        return false;
    }
    size_t sel_start = static_cast<size_t>((std::min)(sel_start_lr, sel_end_lr));
    size_t sel_end = static_cast<size_t>((std::max)(sel_start_lr, sel_end_lr));

    if (sel_start != sel_end || sel_start < scintilla_direct_inline_start_) {
        ResetDirectInlineState();
        return false;
    }

    engine_.BackspaceDisplayChar();
    std::wstring display = engine_.GetDisplayString();
    std::wstring raw = engine_.GetRawString();

    std::string display_utf8;
    if (!ConvertWideToUtf8(display, display_utf8)) {
        SecureEraseString(raw);
        SecureEraseString(display);
        SecureEraseStringUtf8(display_utf8);
        return false;
    }

    size_t replace_start = scintilla_direct_inline_start_;
    size_t replace_end = scintilla_direct_inline_start_ + scintilla_direct_inline_byte_length_;

    ::SendMessageW(hwnd, SCI_SETSEL, replace_start, replace_end);
    ::SendMessageW(hwnd, SCI_REPLACESEL, 0, reinterpret_cast<LPARAM>(display_utf8.c_str()));

    if (raw.empty() || display.empty()) {
        ResetDirectInlineState();
    } else {
        scintilla_direct_inline_byte_length_ = display_utf8.length();
        direct_inline_display_length_ = display.length();
    }

    SecureEraseString(raw);
    SecureEraseString(display);
    SecureEraseStringUtf8(display_utf8);
    return true;
}

bool VietnameseIME::ProcessScintillaDirectCommitChar(HWND hwnd, wchar_t ch) {
    if (!hwnd || ch == 0 || IsSecureInputContext() || !ClassNameEquals(hwnd, L"Scintilla")) {
        return false;
    }

    constexpr LRESULT SCI_GETSELECTIONSTART = 2143;
    constexpr LRESULT SCI_GETSELECTIONEND = 2145;
    constexpr LRESULT SCI_SETSEL = 2160;
    constexpr LRESULT SCI_REPLACESEL = 2170;
    constexpr LRESULT SCI_GETTEXTRANGE = 2162;
    struct SciCharacterRange {
        LONG_PTR cpMin;
        LONG_PTR cpMax;
    };
    struct SciTextRange {
        SciCharacterRange chrg;
        char* lpstrText;
    };

    std::wstring raw = engine_.GetRawString();
    std::wstring pre_speller =
        engine_.GetPreCorrectionDisplayString();
    core::EngineDisplayResult engine_display = engine_.GetDisplayResult();
    std::wstring committed_display = engine_display.text;
    CommitUndoEntry::TransformKind transform_kind =
        engine_display.HasSpellerCorrection()
            ? CommitUndoEntry::TransformKind::SpellerCorrection
            : CommitUndoEntry::TransformKind::None;
    std::wstring previous_token;
    if (enable_fuzzy_input_ && ch == L' ' && !IsExcelApp()) {
        const LRESULT context_start = ::SendMessageW(
            hwnd, SCI_GETSELECTIONSTART, 0, 0);
        const LRESULT context_end = ::SendMessageW(
            hwnd, SCI_GETSELECTIONEND, 0, 0);
        if (context_start >= 0 && context_start == context_end &&
            static_cast<size_t>(context_end) >=
                scintilla_direct_inline_byte_length_ &&
            static_cast<size_t>(context_end) -
                    scintilla_direct_inline_byte_length_ ==
                scintilla_direct_inline_start_) {
            constexpr size_t kMaxFuzzyPairUtf8Bytes =
                (2 * core::kMaxFuzzyInputTokenLength + 1) * 4;
            const size_t caret = static_cast<size_t>(context_end);
            const size_t read_start = caret > kMaxFuzzyPairUtf8Bytes
                ? caret - kMaxFuzzyPairUtf8Bytes
                : 0;
            const size_t read_length = caret - read_start;
            std::vector<char> context_bytes(read_length + 1, '\0');
            SciTextRange context_range{
                {static_cast<LONG_PTR>(read_start),
                 static_cast<LONG_PTR>(caret)},
                context_bytes.data()};
            const LRESULT copied = ::SendMessageW(
                hwnd, SCI_GETTEXTRANGE, 0,
                reinterpret_cast<LPARAM>(&context_range));
            if (copied >= 0 &&
                static_cast<size_t>(copied) == read_length) {
                size_t utf8_start = 0;
                while (utf8_start < read_length &&
                       (static_cast<unsigned char>(
                            context_bytes[utf8_start]) & 0xC0u) == 0x80u) {
                    ++utf8_start;
                }
                std::wstring context_wide;
                if (utf8_start < read_length &&
                    ConvertUtf8ToWideBounded(
                        std::string_view(
                            context_bytes.data() + utf8_start,
                            read_length - utf8_start),
                        // The byte window can contain many more ASCII code
                        // points than a two-token fuzzy pair. Convert the
                        // whole bounded suffix, then let the token extractor
                        // inspect only the immediate pair at the caret.
                        kMaxFuzzyPairUtf8Bytes,
                        context_wide)) {
                    if (const auto previous =
                            core::ExtractImmediatePreviousToken(
                                context_wide, committed_display,
                                read_start != 0 || utf8_start != 0)) {
                        previous_token.assign(*previous);
                    }
                }
                SecureEraseString(context_wide);
            }
            SecureEraseVector(context_bytes);
        }
    }
    DirectCommitTransformDecision transform =
        BuildDirectCommitTransformDecision(
            raw, committed_display, pre_speller, ch, previous_token,
            !previous_token.empty(), true);
    core::CommitTransformDecision& decision = transform.decision;

    const bool rewrite_requested =
        decision.RequiresRewrite() ||
        transform.caret_offset.has_value();
    bool rewrite_succeeded = false;
    std::wstring original_text_for_undo;
    std::optional<core::DirectCommitRewriteSpan> applied_rewrite_span;
    std::optional<size_t> cursor_byte_offset;
    size_t replacement_byte_length = 0;
    if (rewrite_requested && scintilla_direct_inline_byte_length_ > 0) {
        const LRESULT sel_start_lr = ::SendMessageW(
            hwnd, SCI_GETSELECTIONSTART, 0, 0);
        const LRESULT sel_end_lr = ::SendMessageW(
            hwnd, SCI_GETSELECTIONEND, 0, 0);
        std::string committed_utf8;
        std::string expected_utf8;
        std::string transformed_utf8;
        bool converted =
            ConvertWideToUtf8(committed_display, committed_utf8) &&
            ConvertWideToUtf8(decision.expected_source, expected_utf8) &&
            ConvertWideToUtf8(decision.text, transformed_utf8);
        if (converted && transform.caret_offset) {
            if (*transform.caret_offset > decision.text.length()) {
                converted = false;
            } else {
                std::wstring cursor_prefix = decision.text.substr(
                    0, *transform.caret_offset);
                std::string cursor_prefix_utf8;
                converted = ConvertWideToUtf8(
                    cursor_prefix, cursor_prefix_utf8);
                if (converted) {
                    cursor_byte_offset = cursor_prefix_utf8.length();
                }
                SecureEraseString(cursor_prefix);
                SecureEraseStringUtf8(cursor_prefix_utf8);
            }
        }
        const auto rewrite_span = sel_end_lr >= 0 && converted
            ? core::ComputeDirectCommitRewriteSpan(
                  static_cast<size_t>(sel_end_lr),
                  expected_utf8.length(), transformed_utf8.length())
            : std::nullopt;
        bool host_text_matches = false;
        if (sel_start_lr >= 0 && sel_end_lr >= 0 &&
            sel_start_lr == sel_end_lr && rewrite_span &&
            !committed_utf8.empty() && !expected_utf8.empty() &&
            !transformed_utf8.empty() &&
            expected_utf8.length() <= kMaxCommitUndoDisplayChars &&
            scintilla_direct_inline_byte_length_ ==
                committed_utf8.length() &&
            static_cast<size_t>(sel_end_lr) >= committed_utf8.length() &&
            static_cast<size_t>(sel_end_lr) - committed_utf8.length() ==
                scintilla_direct_inline_start_) {
            std::vector<char> current_bytes(
                expected_utf8.length() + 1, '\0');
            SciTextRange text_range{
                {static_cast<LONG_PTR>(rewrite_span->start),
                 static_cast<LONG_PTR>(rewrite_span->old_end)},
                current_bytes.data()};
            const LRESULT copied = ::SendMessageW(
                hwnd, SCI_GETTEXTRANGE, 0,
                reinterpret_cast<LPARAM>(&text_range));
            if (copied >= 0 &&
                static_cast<size_t>(copied) == expected_utf8.length()) {
                std::string current_text(
                    current_bytes.data(), expected_utf8.length());
                const auto verified = FindVerifiedBytesBeforeCaret(
                    current_text, current_text.length(), expected_utf8);
                host_text_matches = verified && verified->start == 0 &&
                    verified->end == current_text.length();
                SecureEraseStringUtf8(current_text);
            }
            SecureEraseVector(current_bytes);
        }
        if (host_text_matches) {
            ::SendMessageW(
                hwnd, SCI_SETSEL, rewrite_span->start,
                rewrite_span->old_end);
            ::SendMessageW(
                hwnd, SCI_REPLACESEL, 0,
                reinterpret_cast<LPARAM>(transformed_utf8.c_str()));
            const LRESULT actual_start = ::SendMessageW(
                hwnd, SCI_GETSELECTIONSTART, 0, 0);
            const LRESULT actual_end = ::SendMessageW(
                hwnd, SCI_GETSELECTIONEND, 0, 0);
            rewrite_succeeded =
                actual_start >= 0 && actual_start == actual_end &&
                static_cast<size_t>(actual_end) ==
                    rewrite_span->new_caret;
            if (rewrite_succeeded) {
                applied_rewrite_span = rewrite_span;
                replacement_byte_length = transformed_utf8.length();
                committed_display = decision.text;
                transform_kind = decision.transform_kind;
                original_text_for_undo = decision.undo_text;
                scintilla_direct_inline_byte_length_ =
                    transformed_utf8.length();
                direct_inline_display_length_ = committed_display.length();
            }
        }
        SecureEraseStringUtf8(committed_utf8);
        SecureEraseStringUtf8(expected_utf8);
        SecureEraseStringUtf8(transformed_utf8);
    }

    if ((!rewrite_requested || rewrite_succeeded) &&
        !transform.caret_offset) {
        CaptureCommitUndoDirectInline(
            hwnd, true, committed_display, transform_kind,
            original_text_for_undo,
            !original_text_for_undo.empty() && applied_rewrite_span
                ? std::optional<size_t>(applied_rewrite_span->new_caret)
                : std::nullopt);
    } else {
        ClearLastCommitUndo();
    }
    SecureEraseString(previous_token);
    SecureEraseString(pre_speller);
    SecureEraseString(original_text_for_undo);
    ResetDirectInlineState();
    std::wstring text_ws(1, ch);
    std::string text_utf8;
    if (!ConvertWideToUtf8(text_ws, text_utf8)) {
        SecureEraseString(decision.text);
        SecureEraseString(decision.expected_source);
        SecureEraseString(decision.undo_text);
        SecureEraseString(committed_display);
        SecureEraseString(engine_display.text);
        SecureEraseString(raw);
        SecureEraseString(text_ws);
        SecureEraseStringUtf8(text_utf8);
        return false;
    }
    ::SendMessageW(hwnd, SCI_REPLACESEL, 0, reinterpret_cast<LPARAM>(text_utf8.c_str()));
    if (rewrite_succeeded && transform.caret_offset &&
        applied_rewrite_span && cursor_byte_offset) {
        const size_t boundary_end = applied_rewrite_span->start +
            replacement_byte_length + text_utf8.length();
        LRESULT actual_start = ::SendMessageW(
            hwnd, SCI_GETSELECTIONSTART, 0, 0);
        LRESULT actual_end = ::SendMessageW(
            hwnd, SCI_GETSELECTIONEND, 0, 0);
        const size_t caret = applied_rewrite_span->start +
            *cursor_byte_offset;
        bool cursor_applied = actual_start >= 0 &&
            actual_start == actual_end &&
            static_cast<size_t>(actual_end) == boundary_end;
        if (cursor_applied) {
            ::SendMessageW(hwnd, SCI_SETSEL, caret, caret);
            actual_start = ::SendMessageW(
                hwnd, SCI_GETSELECTIONSTART, 0, 0);
            actual_end = ::SendMessageW(
                hwnd, SCI_GETSELECTIONEND, 0, 0);
            cursor_applied = actual_start >= 0 &&
                actual_start == actual_end &&
                static_cast<size_t>(actual_end) == caret;
        }
        if (!cursor_applied) {
            std::string original_utf8;
            const bool converted_original = ConvertWideToUtf8(
                engine_display.text, original_utf8);
            if (converted_original) {
                ::SendMessageW(
                    hwnd, SCI_SETSEL, applied_rewrite_span->start,
                    applied_rewrite_span->start + replacement_byte_length);
                ::SendMessageW(
                    hwnd, SCI_REPLACESEL, 0,
                    reinterpret_cast<LPARAM>(original_utf8.c_str()));
                const size_t rollback_caret = applied_rewrite_span->start +
                    original_utf8.length() + text_utf8.length();
                ::SendMessageW(
                    hwnd, SCI_SETSEL, rollback_caret, rollback_caret);
                actual_start = ::SendMessageW(
                    hwnd, SCI_GETSELECTIONSTART, 0, 0);
                actual_end = ::SendMessageW(
                    hwnd, SCI_GETSELECTIONEND, 0, 0);
                cursor_applied = actual_start >= 0 &&
                    actual_start == actual_end &&
                    static_cast<size_t>(actual_end) == rollback_caret;
            }
            logger::LogFormat(
                logger::Level::Warning,
                L"Scintilla shorthand CURSOR transaction failed: rollback=%d",
                cursor_applied ? 1 : 0);
            SecureEraseStringUtf8(original_utf8);
            ClearLastCommitUndo();
        }
    }
    SecureEraseString(decision.text);
    SecureEraseString(decision.expected_source);
    SecureEraseString(decision.undo_text);
    SecureEraseString(committed_display);
    SecureEraseString(engine_display.text);
    SecureEraseString(raw);
    if (last_commit_undo_ && !last_commit_undo_->is_tsf &&
        last_commit_undo_->hwnd == hwnd) {
        last_commit_undo_->committed_with_ascii_space = ch == L' ';
    }
    SecureEraseString(text_ws);
    SecureEraseStringUtf8(text_utf8);
    return true;
}

bool VietnameseIME::ProcessFakeBackspaceEditChar(wchar_t ch) {
    if (ch == 0 || IsSecureInputContext()) {
        return false;
    }
    const bool direct_post = IsInkscapeApp();
    HWND hwnd = direct_post ? GetBestFocusWindow() : nullptr;
    return vn_ime::fake_backspace::ProcessFakeBackspaceChar(
        engine_, ch, direct_inline_display_length_, hwnd, direct_post);
}

bool VietnameseIME::ProcessFakeBackspaceEditBackspace() {
    if (IsSecureInputContext() || !HasDirectInlineState()) {
        return false;
    }
    const bool direct_post = IsInkscapeApp();
    HWND hwnd = direct_post ? GetBestFocusWindow() : nullptr;
    const bool handled = vn_ime::fake_backspace::ProcessFakeBackspaceBackspace(
        engine_, direct_inline_display_length_, hwnd, direct_post);
    if (handled && direct_inline_display_length_ == 0) {
        ResetDirectInlineState();
    }
    return handled;
}


bool VietnameseIME::TryExplorerEditReconversion(wchar_t ch, bool apply) {
    if (ch == 0 || IsSecureInputContext() || !IsExplorerWin32EditFocused()) {
        return false;
    }

    HWND hwnd = GetBestFocusWindow();
    if (!hwnd) {
        return false;
    }

    DWORD sel_start_dw = 0;
    DWORD sel_end_dw = 0;
    ::SendMessageW(hwnd, EM_GETSEL, reinterpret_cast<WPARAM>(&sel_start_dw), reinterpret_cast<LPARAM>(&sel_end_dw));

    LRESULT text_length_result = ::SendMessageW(hwnd, WM_GETTEXTLENGTH, 0, 0);
    if (text_length_result <= 0 || text_length_result > 4096) {
        return false;
    }

    const size_t text_length = static_cast<size_t>(text_length_result);
    const size_t selection_start = (std::min)(static_cast<size_t>(sel_start_dw), text_length);
    const size_t selection_end = (std::min)(static_cast<size_t>(sel_end_dw), text_length);
    if (selection_start > selection_end) {
        return false;
    }

    std::wstring text(text_length + 1, L'\0');
    LRESULT copied = ::SendMessageW(hwnd, WM_GETTEXT, static_cast<WPARAM>(text.size()), reinterpret_cast<LPARAM>(text.data()));
    if (copied < 0) {
        SecureEraseString(text);
        return false;
    }
    text.resize(static_cast<size_t>(copied));
    if (selection_end > text.length()) {
        SecureEraseString(text);
        return false;
    }

    constexpr size_t kWin32ReconversionContextChars = 32;
    constexpr size_t kWin32ReconversionMaxSelectionChars = 64;
    if (selection_end - selection_start > kWin32ReconversionMaxSelectionChars) {
        SecureEraseString(text);
        return false;
    }

    const size_t window_start = selection_start > kWin32ReconversionContextChars
        ? selection_start - kWin32ReconversionContextChars
        : 0;
    const size_t window_end = (std::min)(text.length(), selection_end + kWin32ReconversionContextChars);
    std::wstring window = text.substr(window_start, window_end - window_start);
    std::optional<core::ReconversionEdit> edit = core::BuildReconversionEdit(
        window,
        selection_start - window_start,
        selection_end - window_start,
        ch,
        engine_.GetInputMethod(),
        window_start > 0,
        window_end < text.length());

    logger::LogFormat(logger::Level::Debug,
                      L"Explorer edit reconversion apply=%s text_length=%zu window_length=%zu valid=%s",
                      apply ? L"TRUE" : L"FALSE",
                      text.length(),
                      window.length(),
                      edit ? L"TRUE" : L"FALSE");

    if (!edit) {
        SecureEraseString(window);
        SecureEraseString(text);
        return false;
    }

    if (apply) {
        const DWORD replace_start = static_cast<DWORD>(window_start + edit->start);
        const DWORD replace_end = static_cast<DWORD>(window_start + edit->end);
        const DWORD restore_start = static_cast<DWORD>(replace_start + edit->selection_start);
        const DWORD restore_end = static_cast<DWORD>(replace_start + edit->selection_end);

        ::SendMessageW(hwnd, EM_SETSEL, replace_start, replace_end);
        ::SendMessageW(hwnd, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(edit->replacement.c_str()));
        ::SendMessageW(hwnd, EM_SETSEL, restore_start, restore_end);
        ResetDirectInlineState();
    }

    SecureEraseString(edit->replacement);
    SecureEraseString(window);
    SecureEraseString(text);
    return true;
}

void VietnameseIME::ClearSensitiveState(bool reset_composition) noexcept {
    telegram_synthetic_selection_suppression_.Clear();
    if (telegram_boundary_resume_state_.IsPending()) {
        ClearLastCommitUndo();
    }
    if (telegram_raw_replay_state_.IsPending()) {
        ClearTelegramRawReplay();
    }
    ResetDirectInlineState();
    pending_commit_caret_policy_ = CommitCaretPolicy::MoveToCompositionEnd;
    mouse_commit_pending_ = false;
    composition_commit_pending_ = false;
    telegram_swallow_real_keydown_ = false;
    if (reset_composition) {
        active_composition_.Reset();
        mouse_cookie_ = 0;
    }
}

void VietnameseIME::UnadviseSelectionSink() {
    if (text_edit_cookie_ != 0 && selection_context_) {
        ComPtr<ITfSource> source;
        if (SUCCEEDED(selection_context_->QueryInterface(IID_ITfSource, reinterpret_cast<void**>(source.GetAddressOf())))) {
            HRESULT hrSink = source->UnadviseSink(text_edit_cookie_);
            logger::LogFormat(logger::Level::Info, L"UnadviseSelectionSink: UnadviseSink(ITfTextEditSink) returned hr = 0x%08X", hrSink);
        }
        text_edit_cookie_ = 0;
        selection_context_.Reset();
    }
}

STDMETHODIMP VietnameseIME::OnEndEdit(ITfContext* pic, TfEditCookie ecReadOnly, ITfEditRecord* pEditRecord) {
    logger::Log(logger::Level::Info, L"OnEndEdit called");
    if (is_updating_selection_) {
        logger::Log(logger::Level::Info, L"OnEndEdit: Ignored (internal update)");
        return S_OK;
    }

    if (HasActiveComposition()) {
        ComPtr<ITfRange> comp_range;
        if (SUCCEEDED(active_composition_->GetRange(comp_range.GetAddressOf())) && comp_range) {
            wchar_t text_buf[256] = {0};
            ULONG fetched_chars = 0;
            if (SUCCEEDED(comp_range->GetText(ecReadOnly, 0, text_buf, 255, &fetched_chars))) {
                text_buf[fetched_chars] = L'\0';
                std::wstring comp_text(text_buf);
                std::wstring current_display = engine_.GetDisplayString();
                if (!comp_text.empty() && comp_text != current_display &&
                    engine_.UpdateCasingFromHost(comp_text)) {
                    logger::LogFormat(logger::Level::Info,
                                      L"OnEndEdit: Casing change detected. host_len=%zu, current_len=%zu",
                                      comp_text.length(), current_display.length());
                }
                SecureEraseString(comp_text);
                SecureEraseString(current_display);
            }
            SecureEraseBuffer(text_buf, std::size(text_buf));
        }

        BOOL fSelectionChanged = FALSE;
        if (SUCCEEDED(pEditRecord->GetSelectionStatus(&fSelectionChanged)) && fSelectionChanged) {
            // Check if the selection is exactly at the end of the composition.
            // If so, it is our internal update and we should ignore it.
            bool is_at_end = false;
            TF_SELECTION sel;
            ULONG fetched = 0;
            if (SUCCEEDED(pic->GetSelection(ecReadOnly, TF_DEFAULT_SELECTION, 1, &sel, &fetched)) && fetched == 1) {
                if (comp_range) {
                    ComPtr<ITfRange> comp_end;
                    if (SUCCEEDED(comp_range->Clone(comp_end.GetAddressOf())) && comp_end) {
                        comp_end->Collapse(ecReadOnly, TF_ANCHOR_END);
                        BOOL startEqual = FALSE;
                        BOOL endEqual = FALSE;
                        if (SUCCEEDED(sel.range->IsEqualStart(ecReadOnly, comp_end.Get(), TF_ANCHOR_START, &startEqual)) && startEqual &&
                            SUCCEEDED(sel.range->IsEqualEnd(ecReadOnly, comp_end.Get(), TF_ANCHOR_END, &endEqual)) && endEqual) {
                            is_at_end = true;
                        }
                    }
                }
                if (sel.range) sel.range->Release();
            }

            if (is_at_end) {
                logger::Log(logger::Level::Info, L"OnEndEdit: Selection is at composition end (internal), skipping commit");
            } else if (IsTelegramProcess()) {
                logger::Log(logger::Level::Info, L"OnEndEdit: Selection change ignored for Telegram to prevent premature composition commit");
            } else if (IsWebRichTextHostProcess()) {
                logger::Log(logger::Level::Info, L"OnEndEdit: Selection change ignored for web rich-text host to prevent host-selection races");
            } else {
                logger::Log(logger::Level::Info, L"OnEndEdit: Selection moved away from composition end, committing active composition");
                pending_commit_caret_policy_ = CommitCaretPolicy::PreserveHostSelection;
                composition_commit_pending_ = true;
                CommitCompositionSync(pic);
            }
        }
    }
    return S_OK;
}

void VietnameseIME::ResetDirectInlineState(
    bool preserve_pending_shorthand_selection) noexcept {
    engine_.SecureClear();
    if (!preserve_pending_shorthand_selection) {
        ClearPendingShorthandSelection();
    }
    direct_inline_display_length_ = 0;
    scintilla_direct_inline_byte_length_ = 0;
    scintilla_direct_inline_start_ = 0;
    word_reconversion_composition_active_ = false;
    ResetBrowserUrlNativeMode();
}

UINT VietnameseIME::SendTelegramBoundarySelectionSequence() noexcept {
    const auto set_key = [](INPUT& input, WORD vk, DWORD flags = 0) {
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = vk;
        input.ki.dwFlags = flags;
        input.ki.dwExtraInfo = kTelegramNativeTransactionMarker;
    };

    INPUT boundary_inputs[2]{};
    set_key(boundary_inputs[0], VK_BACK);
    set_key(boundary_inputs[1], VK_BACK, KEYEVENTF_KEYUP);
    const UINT boundary_sent = ::SendInput(
        static_cast<UINT>(std::size(boundary_inputs)), boundary_inputs,
        sizeof(INPUT));
    if (boundary_sent < std::size(boundary_inputs)) {
        UINT cleanup_sent = 0;
        if (boundary_sent > 0) {
            INPUT backspace_up{};
            set_key(backspace_up, VK_BACK, KEYEVENTF_KEYUP);
            cleanup_sent = ::SendInput(1, &backspace_up, sizeof(INPUT));
        }
        logger::LogFormat(
            logger::Level::Warning,
            L"Telegram boundary injection partial: boundary_sent=%u, cleanup_sent=%u",
            boundary_sent, cleanup_sent);
        return boundary_sent;
    }

    INPUT selection_inputs[6]{};
    set_key(selection_inputs[0], VK_CONTROL);
    set_key(selection_inputs[1], VK_SHIFT);
    set_key(selection_inputs[2], VK_LEFT, KEYEVENTF_EXTENDEDKEY);
    set_key(selection_inputs[3], VK_LEFT,
            KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP);
    set_key(selection_inputs[4], VK_SHIFT, KEYEVENTF_KEYUP);
    set_key(selection_inputs[5], VK_CONTROL, KEYEVENTF_KEYUP);
    const UINT selection_sent = ::SendInput(
        static_cast<UINT>(std::size(selection_inputs)), selection_inputs,
        sizeof(INPUT));

    if (selection_sent < std::size(selection_inputs)) {
        INPUT cleanup_inputs[3]{};
        set_key(cleanup_inputs[0], VK_LEFT,
                KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP);
        set_key(cleanup_inputs[1], VK_SHIFT, KEYEVENTF_KEYUP);
        set_key(cleanup_inputs[2], VK_CONTROL, KEYEVENTF_KEYUP);
        const UINT cleanup_sent = ::SendInput(
            static_cast<UINT>(std::size(cleanup_inputs)), cleanup_inputs,
            sizeof(INPUT));
        const bool collapse_sent = selection_sent >= 3 &&
            SendTelegramSelectionCollapseRight();
        logger::LogFormat(
            logger::Level::Warning,
            L"Telegram selection injection partial: selection_sent=%u, cleanup_sent=%u, collapse_sent=%d",
            selection_sent, cleanup_sent, collapse_sent ? 1 : 0);
    }

    return boundary_sent + selection_sent;
}

bool VietnameseIME::SendTelegramSelectionCollapseRight() noexcept {
    INPUT inputs[2]{};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = VK_RIGHT;
    inputs[0].ki.dwFlags = KEYEVENTF_EXTENDEDKEY;
    inputs[0].ki.dwExtraInfo = kTelegramNativeTransactionMarker;
    inputs[1] = inputs[0];
    inputs[1].ki.dwFlags |= KEYEVENTF_KEYUP;
    return ::SendInput(2, inputs, sizeof(INPUT)) == 2;
}

void VietnameseIME::SendSyntheticNativeKey(WORD vk) {
    if (IsInkscapeApp()) {
        HWND hwnd = GetBestFocusWindow();
        if (hwnd) {
            UINT scanCode = ::MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
            LPARAM downLParam = 1 | (scanCode << 16) | (1 << 28);
            LPARAM upLParam = 0xC0000001 | (scanCode << 16) | (1 << 28);
            ::PostMessageW(hwnd, WM_KEYDOWN, vk, downLParam);
            ::PostMessageW(hwnd, WM_KEYUP, vk, upLParam);
            return;
        }
    }

    bool delay_needed = (vk == VK_RETURN) && IsNativeEnterReplayApp() && (GetFocusedProcessName() != L"notepad++.exe");

    if (delay_needed) {
        logger::Log(logger::Level::Info, L"SendSyntheticNativeKey: delaying Enter key by 50ms to allow DOM update");
        std::thread([vk]() {
            ::Sleep(50);
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
                logger::LogFormat(logger::Level::Warning, L"SendSyntheticNativeKey (delayed) sent %u of 2 inputs", sent);
            }
        }).detach();
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

void VietnameseIME::SendSyntheticUnicodeChar(wchar_t ch) {
    const bool direct_post = IsInkscapeApp();
    HWND hwnd = direct_post ? GetBestFocusWindow() : nullptr;
    vn_ime::fake_backspace::SendSyntheticUnicodeChar(ch, hwnd, direct_post);
}

void VietnameseIME::EnsureInkscapeSubclassed() {
    if (IsInkscapeApp()) {
        HWND hwnd = GetBestFocusWindow();
        if (hwnd) {
            std::vector<HWND> targets;
            HWND current = hwnd;
            int depth = 0;
            while (current && depth < 32) {
                targets.push_back(current);
                current = ::GetParent(current);
                depth++;
            }
            HWND root = ::GetAncestor(hwnd, GA_ROOT);
            if (root) {
                targets.push_back(root);
            }
            HWND root_owner = ::GetAncestor(hwnd, GA_ROOTOWNER);
            if (root_owner) {
                targets.push_back(root_owner);
            }

            for (HWND target : targets) {
                DWORD_PTR ref_data = 0;
                if (!::GetWindowSubclass(target, InkscapeSubclassProc, 0x1991, &ref_data)) {
                    if (::SetWindowSubclass(target, InkscapeSubclassProc, 0x1991, reinterpret_cast<DWORD_PTR>(this))) {
                        subclassed_hwnds_.push_back(target);
                        logger::LogFormat(logger::Level::Info, L"Subclassed Inkscape window 0x%p to suppress IMM32 composition messages", target);
                    }
                }
            }
        }
    }
}


HRESULT VietnameseIME::ReplaceDirectInlineText(
    TfEditCookie ec, ITfContext* pic, ITfRange* caret_range,
    const std::wstring& text, const std::wstring& old_text, wchar_t ch,
    bool* text_applied, ITfRange** applied_range) {
    if (text_applied) {
        *text_applied = false;
    }
    if (applied_range) {
        *applied_range = nullptr;
    }
    if (!pic || !caret_range) {
        return E_INVALIDARG;
    }
    if (IsSecureInputContext()) {
        ClearSensitiveState(false);
        return E_FAIL;
    }

    if (GetFocusedProcessName() == L"anydesk.exe") {
        for (size_t i = 0; i < direct_inline_display_length_; ++i) {
            SendSyntheticNativeKey(VK_BACK);
        }
        for (wchar_t wc : text) {
            SendSyntheticUnicodeChar(wc);
        }
        direct_inline_display_length_ = text.length();
        if (text_applied) {
            *text_applied = true;
        }
        return applied_range ? E_NOTIMPL : S_OK;
    }

    ComPtr<ITfRange> replace_range;
    HRESULT hr = caret_range->Clone(replace_range.GetAddressOf());
    if (FAILED(hr)) {
        return hr;
    }

    bool handled_inkscape = false;
    if (direct_inline_display_length_ > 0 && IsInkscapeApp()) {
        bool transformation = true;
        if (ch != 0) {
            std::wstring expected = old_text + ch;
            if (text == expected) {
                transformation = false;
            }
        }

        if (transformation) {
            for (size_t i = 0; i < direct_inline_display_length_; ++i) {
                SendSyntheticNativeKey(VK_BACK);
            }
            hr = replace_range->SetText(ec, 0, text.c_str(), static_cast<LONG>(text.length()));
        } else {
            wchar_t ch_str[2] = { ch, L'\0' };
            hr = replace_range->SetText(ec, 0, ch_str, 1);
        }
        handled_inkscape = true;
    }

    if (!handled_inkscape) {
        if (direct_inline_display_length_ > 0) {
            replace_range->Collapse(ec, TF_ANCHOR_END);
            LONG shifted = 0;
            hr = replace_range->ShiftStart(ec, -static_cast<LONG>(direct_inline_display_length_), &shifted, nullptr);
            if (FAILED(hr)) {
                return hr;
            }
        }
        hr = replace_range->SetText(ec, 0, text.c_str(), static_cast<LONG>(text.length()));
    }

    if (FAILED(hr)) {
        return hr;
    }

    direct_inline_display_length_ = text.length();
    if (text_applied) {
        *text_applied = true;
    }

    if (applied_range) {
        hr = replace_range->Clone(applied_range);
        if (FAILED(hr)) {
            return hr;
        }
    }

    replace_range->Collapse(ec, TF_ANCHOR_END);
    TF_SELECTION new_sel;
    new_sel.range = replace_range.Get();
    new_sel.style.ase = TF_AE_NONE;
    new_sel.style.fInterimChar = FALSE;
    hr = pic->SetSelection(ec, 1, &new_sel);
    return hr;
}

bool VietnameseIME::IsSecureInputContext() const noexcept {
    password_context::SecureInputDecisionInput input{};
    input.secure_desktop = logger::IsSecureDesktop();
    input.password_input_scope = is_password_field_;
    HWND hwnd = GetBestFocusWindow();
    input.has_window = hwnd != nullptr;
    if (!hwnd || input.secure_desktop || input.password_input_scope) {
        return password_context::IsSecureInputContext(input);
    }

    std::wstring class_name = GetClassNameOrEmpty(hwnd);
    input.class_name_available = !class_name.empty();
    input.password_message_control =
        password_context::SupportsPasswordCharacterMessage(class_name);
    if (!input.class_name_available || !input.password_message_control) {
        return password_context::IsSecureInputContext(input);
    }

    input.password_style =
        (::GetWindowLongPtrW(hwnd, GWL_STYLE) & ES_PASSWORD) != 0;
    DWORD_PTR password_character = 0;
    input.password_query_succeeded =
        ::SendMessageTimeoutW(
            hwnd, EM_GETPASSWORDCHAR, 0, 0,
            SMTO_ABORTIFHUNG | SMTO_BLOCK, 50,
            &password_character) != 0;
    input.password_character =
        static_cast<unsigned long long>(password_character);
    return password_context::IsSecureInputContext(input);
}

bool VietnameseIME::IsKeyFiltered(WPARAM wParam, LPARAM lParam) const noexcept {
    if (IsSecureInputContext()) {
        return false;
    }

    if (HasTextShortcutModifier()) {
        return false;
    }

    if (active_composition_) {
        if (wParam == VK_BACK || wParam == VK_SPACE || wParam == VK_RETURN) {
            return true;
        }
    }

    if (IsValidCompositionKey(wParam, engine_.GetInputMethod())) {
        return true;
    }

    if (active_composition_ &&
        IsSmartContextContinuationKey(wParam, lParam)) {
        return true;
    }

    return false;
}

// ITfThreadMgrEventSink Implementation
STDMETHODIMP VietnameseIME::OnInitDocumentMgr([[maybe_unused]] ITfDocumentMgr* pdm) {
    return S_OK;
}

STDMETHODIMP VietnameseIME::OnUninitDocumentMgr([[maybe_unused]] ITfDocumentMgr* pdm) {
    return S_OK;
}

STDMETHODIMP VietnameseIME::OnSetFocus(ITfDocumentMgr* pdmFocus, ITfDocumentMgr* pdmPrevFocus) {
    logger::Log(logger::Level::Info, L"OnSetFocus (ITfDocumentMgr) called.");
    const bool is_browser = IsBrowserProcess();
    const InputScopeFocusRefreshPolicy refresh_policy =
        SelectInputScopeFocusRefreshPolicy(is_browser);
    if (browser_url_native_mode_active_) {
        ResetDirectInlineState();
    }
    ClearLastCommitUndo();
    ClearTelegramRawReplay();
    is_password_field_ = false;

    if (pdmPrevFocus) {
        if (!is_browser) {
            ComPtr<ITfContext> context;
            if (SUCCEEDED(pdmPrevFocus->GetTop(context.GetAddressOf())) && context) {
                CommitCompositionSync(context.Get());
            }
        }
        ClearSensitiveState(false);
    }

    if (refresh_policy ==
        InputScopeFocusRefreshPolicy::DeferToTextKeySyncOnly) {
        ClearSensitiveState(false);
        if (!pdmFocus) {
            ResetBrowserInputScopeCheck();
            return S_OK;
        }

        EnsureInkscapeSubclassed();
        ComPtr<ITfContext> context;
        if (SUCCEEDED(pdmFocus->GetTop(context.GetAddressOf())) && context) {
            MarkBrowserInputScopeCheckPending(context.Get());
        } else {
            MarkBrowserInputScopeCheckPending(nullptr);
        }
        return S_OK;
    }

    ResetBrowserInputScopeCheck();
    if (pdmFocus) {
        EnsureInkscapeSubclassed();
        ComPtr<ITfContext> context;
        if (SUCCEEDED(pdmFocus->GetTop(context.GetAddressOf())) && context) {
            ComPtr<EditSession> session;
            session.Attach(new (std::nothrow) EditSession(this, context.Get(), EditAction::CheckPassword));
            if (session) {
                HRESULT hr = S_OK;
                HRESULT hrReq = context->RequestEditSession(client_id_, session.Get(), TF_ES_SYNC | TF_ES_READ, &hr);
                if (FAILED(hrReq) || FAILED(hr)) {
                    context->RequestEditSession(client_id_, session.Get(), TF_ES_READ, &hr);
                }
            }
        }
    }
    return S_OK;
}

STDMETHODIMP VietnameseIME::OnPushContext(ITfContext* pic) {
    logger::Log(logger::Level::Info, L"OnPushContext called.");
    const InputScopeFocusRefreshPolicy refresh_policy =
        SelectInputScopeFocusRefreshPolicy(IsBrowserProcess());
    if (refresh_policy ==
        InputScopeFocusRefreshPolicy::DeferToTextKeySyncOnly) {
        ClearSensitiveState(false);
        is_password_field_ = false;
        MarkBrowserInputScopeCheckPending(pic);
        return S_OK;
    }

    ResetBrowserInputScopeCheck();
    if (browser_url_native_mode_active_) {
        ResetDirectInlineState();
    }
    is_password_field_ = false;
    if (pic) {
        ComPtr<EditSession> session;
        session.Attach(new (std::nothrow) EditSession(this, pic, EditAction::CheckPassword));
        if (session) {
            HRESULT hr = S_OK;
            HRESULT hrReq = pic->RequestEditSession(client_id_, session.Get(), TF_ES_SYNC | TF_ES_READ, &hr);
            if (FAILED(hrReq) || FAILED(hr)) {
                pic->RequestEditSession(client_id_, session.Get(), TF_ES_READ, &hr);
            }
        }
    }
    return S_OK;
}

STDMETHODIMP VietnameseIME::OnPopContext(ITfContext* pic) {
    ResetBrowserInputScopeCheck();
    ClearSensitiveState(false);
    if (IsExcelApp()) {
        ResetExcelFormulaSession(L"pop_context");
    }
    return S_OK;
}

// Helper methods for event sinks
HRESULT VietnameseIME::InitKeySink() {
    logger::Log(logger::Level::Info, L"VietnameseIME::InitKeySink called.");
    ComPtr<ITfKeystrokeMgr> keystroke_mgr;
    HRESULT hr = thread_mgr_.As(keystroke_mgr);
    if (FAILED(hr)) {
        logger::LogFormat(logger::Level::Error, L"Failed to query ITfKeystrokeMgr. hr = 0x%08X", hr);
        return hr;
    }

    hr = keystroke_mgr->AdviseKeyEventSink(client_id_, this, TRUE);
    if (FAILED(hr)) {
        logger::LogFormat(logger::Level::Error, L"AdviseKeyEventSink failed. hr = 0x%08X", hr);
        return hr;
    }
    logger::Log(logger::Level::Info, L"VietnameseIME::InitKeySink succeeded.");
    return S_OK;
}

void VietnameseIME::UninitKeySink() {
    logger::Log(logger::Level::Info, L"VietnameseIME::UninitKeySink called.");
    ComPtr<ITfKeystrokeMgr> keystroke_mgr;
    if (SUCCEEDED(thread_mgr_.As(keystroke_mgr))) {
        HRESULT hr = keystroke_mgr->UnadviseKeyEventSink(client_id_);
        logger::LogFormat(logger::Level::Info, L"UnadviseKeyEventSink returned. hr = 0x%08X", hr);
    }
}

HRESULT VietnameseIME::InitThreadMgrEventSink() {
    logger::Log(logger::Level::Info, L"VietnameseIME::InitThreadMgrEventSink called.");
    ComPtr<ITfSource> source;
    HRESULT hr = thread_mgr_.As(source);
    if (FAILED(hr)) {
        logger::LogFormat(logger::Level::Error, L"Failed to query ITfSource for ThreadMgrEventSink. hr = 0x%08X", hr);
        return hr;
    }

    hr = source->AdviseSink(IID_ITfThreadMgrEventSink, static_cast<ITfThreadMgrEventSink*>(this), &thread_mgr_cookie_);
    if (FAILED(hr)) {
        logger::LogFormat(logger::Level::Error, L"AdviseSink failed for ThreadMgrEventSink. hr = 0x%08X", hr);
        return hr;
    }
    logger::LogFormat(logger::Level::Info, L"VietnameseIME::InitThreadMgrEventSink succeeded. cookie = %u", thread_mgr_cookie_);
    return S_OK;
}

void VietnameseIME::UninitThreadMgrEventSink() {
    logger::Log(logger::Level::Info, L"VietnameseIME::UninitThreadMgrEventSink called.");
    ComPtr<ITfSource> source;
    if (thread_mgr_cookie_ != 0 && SUCCEEDED(thread_mgr_.As(source))) {
        HRESULT hr = source->UnadviseSink(thread_mgr_cookie_);
        logger::LogFormat(logger::Level::Info, L"UnadviseSink returned. hr = 0x%08X", hr);
        thread_mgr_cookie_ = 0;
    }
}

// ITfDisplayAttributeProvider implementation
STDMETHODIMP VietnameseIME::EnumDisplayAttributeInfo(IEnumTfDisplayAttributeInfo** ppEnum) {
    if (!ppEnum) return E_INVALIDARG;
    *ppEnum = new (std::nothrow) VietnameseEnumDisplayAttributeInfo();
    return *ppEnum ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP VietnameseIME::GetDisplayAttributeInfo(REFGUID guid, ITfDisplayAttributeInfo** ppInfo) {
    if (!ppInfo) return E_INVALIDARG;
    *ppInfo = nullptr;
    if (guid == GUID_VietnameseDisplayAttribute) {
        *ppInfo = new (std::nothrow) VietnameseDisplayAttributeInfo();
        return *ppInfo ? S_OK : E_OUTOFMEMORY;
    }
    return E_INVALIDARG;
}

STDMETHODIMP VietnameseIME::OnCompositionTerminated([[maybe_unused]] TfEditCookie ecWrite, [[maybe_unused]] ITfComposition *pComposition) {
    logger::Log(logger::Level::Info, L"OnCompositionTerminated called");
    
    // Unadvise Mouse Sink
    if (mouse_cookie_ != 0 && pComposition) {
        ComPtr<ITfRange> comp_range;
        if (SUCCEEDED(pComposition->GetRange(comp_range.GetAddressOf())) && comp_range) {
            ComPtr<ITfContext> context;
            if (SUCCEEDED(comp_range->GetContext(context.GetAddressOf())) && context) {
                ComPtr<ITfMouseTracker> mouse_tracker;
                if (SUCCEEDED(context->QueryInterface(IID_ITfMouseTracker, reinterpret_cast<void**>(mouse_tracker.GetAddressOf())))) {
                    HRESULT hrMouse = mouse_tracker->UnadviseMouseSink(mouse_cookie_);
                    logger::LogFormat(logger::Level::Info, L"OnCompositionTerminated: UnadviseMouseSink returned hr = 0x%08X", hrMouse);
                }
            }
        }
        mouse_cookie_ = 0;
    }

    if (active_subclassed_hwnd_ != nullptr) {
        ::RemoveWindowSubclass(active_subclassed_hwnd_, MouseHookSubclassProc, 0x2026);
        logger::LogFormat(logger::Level::Info, L"OnCompositionTerminated: Removed MouseHookSubclassProc subclass from HWND 0x%p", active_subclassed_hwnd_);
        active_subclassed_hwnd_ = nullptr;
    }
    if (active_subclassed_root_hwnd_ != nullptr) {
        ::RemoveWindowSubclass(active_subclassed_root_hwnd_, MouseHookSubclassProc, 0x2027);
        logger::LogFormat(logger::Level::Info, L"OnCompositionTerminated: Removed MouseHookSubclassProc subclass from Root HWND 0x%p", active_subclassed_root_hwnd_);
        active_subclassed_root_hwnd_ = nullptr;
    }

    RemoveThreadCompositionHooks();

    UnadviseSelectionSink();

    ClearSensitiveState(true);
    return S_OK;
}

// Composition management helper methods
HRESULT VietnameseIME::StartComposition(
    TfEditCookie ec, ITfContext* pic, ITfRange* range,
    bool allow_live_range_fallback) {
    if (IsSecureInputContext()) {
        ClearSensitiveState(false);
        return E_FAIL;
    }
    if (!range) return E_INVALIDARG;
    pending_commit_caret_policy_ = CommitCaretPolicy::MoveToCompositionEnd;
    mouse_commit_pending_ = false;

    ComPtr<ITfContextComposition> context_comp;
    HRESULT hr = pic->QueryInterface(IID_ITfContextComposition, reinterpret_cast<void**>(context_comp.GetAddressOf()));
    logger::LogFormat(logger::Level::Info, L"StartComposition: QueryInterface(IID_ITfContextComposition) returned hr = 0x%08X", hr);
    if (FAILED(hr)) return hr;
    
    ComPtr<ITfRange> cloned_range;
    hr = range->Clone(cloned_range.GetAddressOf());
    logger::LogFormat(logger::Level::Info, L"StartComposition: range->Clone returned hr = 0x%08X", hr);
    const bool use_live_range = FAILED(hr) && allow_live_range_fallback &&
                                IsTelegramProcess();
    if (FAILED(hr) && !use_live_range) return hr;
    ITfRange* const composition_range =
        use_live_range ? range : cloned_range.Get();

    hr = context_comp->StartComposition(
        ec, composition_range, static_cast<ITfCompositionSink*>(this),
        active_composition_.ReleaseAndGetAddressOf());
    logger::LogFormat(
        logger::Level::Info,
        L"StartComposition: context_comp->StartComposition returned hr = 0x%08X, live_range=%d, active=%d",
        hr, use_live_range ? 1 : 0, active_composition_ ? 1 : 0);
    if (SUCCEEDED(hr) && !active_composition_) {
        logger::Log(logger::Level::Warning,
                    L"StartComposition: host accepted request without creating a composition");
        return E_FAIL;
    }
    
    if (SUCCEEDED(hr)) {
        HWND target = GetBestFocusWindow();
        if (target) {
            if (active_subclassed_hwnd_) {
                ::RemoveWindowSubclass(active_subclassed_hwnd_, MouseHookSubclassProc, 0x2026);
                active_subclassed_hwnd_ = nullptr;
            }
            if (::SetWindowSubclass(target, MouseHookSubclassProc, 0x2026, reinterpret_cast<DWORD_PTR>(this))) {
                active_subclassed_hwnd_ = target;
                logger::LogFormat(logger::Level::Info, L"StartComposition: Subclassed focus window 0x%p to monitor mouse clicks", target);
            } else {
                logger::LogFormat(logger::Level::Warning, L"StartComposition: SetWindowSubclass failed for HWND 0x%p", target);
            }

            HWND root = ::GetAncestor(target, GA_ROOT);
            if (root && root != target) {
                if (active_subclassed_root_hwnd_) {
                    ::RemoveWindowSubclass(active_subclassed_root_hwnd_, MouseHookSubclassProc, 0x2027);
                    active_subclassed_root_hwnd_ = nullptr;
                }
                if (::SetWindowSubclass(root, MouseHookSubclassProc, 0x2027, reinterpret_cast<DWORD_PTR>(this))) {
                    active_subclassed_root_hwnd_ = root;
                    logger::LogFormat(logger::Level::Info, L"StartComposition: Subclassed root window 0x%p to monitor mouse clicks", root);
                } else {
                    logger::LogFormat(logger::Level::Warning, L"StartComposition: SetWindowSubclass failed for Root HWND 0x%p", root);
                }
            }
        }

        g_ime_instance = this;
        if (!g_msg_hook) {
            g_msg_hook = ::SetWindowsHookExW(WH_GETMESSAGE, GetMessageHookProc, nullptr, ::GetCurrentThreadId());
            if (g_msg_hook) {
                logger::Log(logger::Level::Info, L"StartComposition: Thread-local GetMessage hook installed");
            } else {
                logger::LogFormat(logger::Level::Warning, L"StartComposition: SetWindowsHookExW(WH_GETMESSAGE) failed, error = %u", ::GetLastError());
            }
        }
        if (!g_call_wnd_hook) {
            g_call_wnd_hook = ::SetWindowsHookExW(WH_CALLWNDPROC, CallWndProc, nullptr, ::GetCurrentThreadId());
            if (g_call_wnd_hook) {
                logger::Log(logger::Level::Info, L"StartComposition: Thread-local CallWndProc hook installed");
            } else {
                logger::LogFormat(logger::Level::Warning, L"StartComposition: SetWindowsHookExW(WH_CALLWNDPROC) failed, error = %u", ::GetLastError());
            }
        }
        if (!g_mouse_hook) {
            g_mouse_hook = ::SetWindowsHookExW(WH_MOUSE, MouseHookProc, nullptr, ::GetCurrentThreadId());
            if (g_mouse_hook) {
                logger::Log(logger::Level::Info, L"StartComposition: Thread-local Mouse hook installed");
            } else {
                logger::LogFormat(logger::Level::Warning, L"StartComposition: SetWindowsHookExW(WH_MOUSE) failed, error = %u", ::GetLastError());
            }
        }

        // Advise text edit sink to monitor caret/selection changes during active composition
        ComPtr<ITfSource> source;
        if (SUCCEEDED(pic->QueryInterface(IID_ITfSource, reinterpret_cast<void**>(source.GetAddressOf())))) {
            HRESULT hrSink = source->AdviseSink(IID_ITfTextEditSink, static_cast<ITfTextEditSink*>(this), &text_edit_cookie_);
            if (SUCCEEDED(hrSink)) {
                selection_context_ = ComPtr<ITfContext>(pic);
                logger::LogFormat(logger::Level::Info, L"StartComposition: AdviseSink(ITfTextEditSink) returned cookie = %u", text_edit_cookie_);
            } else {
                logger::LogFormat(logger::Level::Warning, L"StartComposition: AdviseSink(ITfTextEditSink) failed, hr = 0x%08X", hrSink);
            }
        }

        ComPtr<ITfMouseTracker> mouse_tracker;
        if (SUCCEEDED(pic->QueryInterface(IID_ITfMouseTracker, reinterpret_cast<void**>(mouse_tracker.GetAddressOf())))) {
            // Advise mouse sink on the entire document to capture clicks anywhere in the text box
            ComPtr<ITfRange> entire_range;
            if (SUCCEEDED(pic->GetStart(ec, entire_range.GetAddressOf())) && entire_range) {
                LONG shifted = 0;
                entire_range->ShiftEnd(ec, 1000000, &shifted, nullptr);
                HRESULT hrMouse = mouse_tracker->AdviseMouseSink(entire_range.Get(), static_cast<ITfMouseSink*>(this), &mouse_cookie_);
                logger::LogFormat(logger::Level::Info, L"StartComposition: AdviseMouseSink (entire range) returned hr = 0x%08X, cookie = %u", hrMouse, mouse_cookie_);
            } else {
                HRESULT hrMouse = mouse_tracker->AdviseMouseSink(cloned_range.Get(), static_cast<ITfMouseSink*>(this), &mouse_cookie_);
                logger::LogFormat(logger::Level::Info, L"StartComposition: AdviseMouseSink (fallback) returned hr = 0x%08X, cookie = %u", hrMouse, mouse_cookie_);
            }
        } else {
            logger::Log(logger::Level::Warning, L"StartComposition: Context does not support ITfMouseTracker");
        }
    }

    return hr;
}

void VietnameseIME::ClearShorthandCaretTransaction() noexcept {
    shorthand_transaction_range_.Reset();
    shorthand_caret_range_.Reset();
    SecureEraseString(shorthand_transaction_original_text_);
    shorthand_caret_transaction_active_ = false;
}

VietnameseIME::ShorthandCaretTransactionResult
VietnameseIME::PrepareShorthandCaretTransaction(
    TfEditCookie ec, ITfContext* context, ITfRange* replacement_range,
    std::wstring_view original_text, size_t caret_offset) {
    ClearShorthandCaretTransaction();
    if (!replacement_range) {
        return ShorthandCaretTransactionResult::MutationRetained;
    }

    ComPtr<ITfContext> resolved_context;
    if (!context && SUCCEEDED(replacement_range->GetContext(
            resolved_context.GetAddressOf()))) {
        context = resolved_context.Get();
    }

    auto restore_original = [&]() {
        const HRESULT text_hr = replacement_range->SetText(
            ec, 0, original_text.data(),
            static_cast<LONG>(original_text.length()));
        ComPtr<ITfRange> end_range;
        HRESULT selection_hr = E_FAIL;
        if (SUCCEEDED(text_hr) &&
            SUCCEEDED(replacement_range->Clone(end_range.GetAddressOf())) &&
            end_range &&
            SUCCEEDED(end_range->Collapse(ec, TF_ANCHOR_END))) {
            TF_SELECTION selection{};
            selection.range = end_range.Get();
            selection.style.ase = TF_AE_NONE;
            selection.style.fInterimChar = FALSE;
            selection_hr = context
                ? context->SetSelection(ec, 1, &selection)
                : E_POINTER;
        }
        return SUCCEEDED(text_hr);
    };

    if (!context ||
        caret_offset > static_cast<size_t>((std::numeric_limits<LONG>::max)()) ||
        original_text.length() >
            static_cast<size_t>((std::numeric_limits<LONG>::max)())) {
        const bool rolled_back = restore_original();
        ClearShorthandCaretTransaction();
        return rolled_back
            ? ShorthandCaretTransactionResult::RolledBack
            : ShorthandCaretTransactionResult::MutationRetained;
    }

    HRESULT hr = replacement_range->Clone(
        shorthand_transaction_range_.GetAddressOf());
    if (SUCCEEDED(hr) && shorthand_transaction_range_) {
        hr = shorthand_transaction_range_->SetGravity(
            ec, TF_GRAVITY_BACKWARD, TF_GRAVITY_BACKWARD);
    }
    if (SUCCEEDED(hr)) {
        hr = replacement_range->Clone(
            shorthand_caret_range_.GetAddressOf());
    }
    if (SUCCEEDED(hr) && shorthand_caret_range_) {
        hr = shorthand_caret_range_->Collapse(ec, TF_ANCHOR_START);
    }
    LONG shifted = 0;
    if (SUCCEEDED(hr)) {
        const LONG delta = static_cast<LONG>(caret_offset);
        hr = shorthand_caret_range_->ShiftStart(
            ec, delta, &shifted, nullptr);
        if (SUCCEEDED(hr) && shifted != delta) {
            hr = E_FAIL;
        }
    }
    if (SUCCEEDED(hr)) {
        hr = shorthand_caret_range_->ShiftEnd(ec, 0, &shifted, nullptr);
    }
    if (SUCCEEDED(hr)) {
        hr = shorthand_caret_range_->SetGravity(
            ec, TF_GRAVITY_BACKWARD, TF_GRAVITY_BACKWARD);
    }

    ComPtr<ITfRange> end_range;
    if (SUCCEEDED(hr)) {
        hr = replacement_range->Clone(end_range.GetAddressOf());
    }
    if (SUCCEEDED(hr) && end_range) {
        hr = end_range->Collapse(ec, TF_ANCHOR_END);
    }

    if (SUCCEEDED(hr)) {
        TF_SELECTION selection{};
        selection.range = shorthand_caret_range_.Get();
        selection.style.ase = TF_AE_NONE;
        selection.style.fInterimChar = FALSE;
        hr = context->SetSelection(ec, 1, &selection);
    }
    if (SUCCEEDED(hr)) {
        TF_SELECTION selection{};
        selection.range = end_range.Get();
        selection.style.ase = TF_AE_NONE;
        selection.style.fInterimChar = FALSE;
        hr = context->SetSelection(ec, 1, &selection);
    }

    if (FAILED(hr)) {
        const bool rolled_back = restore_original();
        logger::LogFormat(
            logger::Level::Warning,
            L"Shorthand caret transaction prepare failed: hr=0x%08X, rollback=%d",
            hr, rolled_back ? 1 : 0);
        ClearShorthandCaretTransaction();
        return rolled_back
            ? ShorthandCaretTransactionResult::RolledBack
            : ShorthandCaretTransactionResult::MutationRetained;
    }

    shorthand_transaction_original_text_.assign(original_text);
    shorthand_caret_transaction_active_ = true;
    return ShorthandCaretTransactionResult::Prepared;
}

VietnameseIME::ShorthandCaretTransactionResult
VietnameseIME::FinalizeShorthandCaretTransaction(
    TfEditCookie ec, ITfContext* context) {
    if (!shorthand_caret_transaction_active_) {
        return ShorthandCaretTransactionResult::Applied;
    }
    if (!shorthand_transaction_range_ || !shorthand_caret_range_) {
        ClearShorthandCaretTransaction();
        return ShorthandCaretTransactionResult::MutationRetained;
    }

    ComPtr<ITfContext> resolved_context;
    if (!context && SUCCEEDED(shorthand_transaction_range_->GetContext(
            resolved_context.GetAddressOf()))) {
        context = resolved_context.Get();
    }
    if (!context) {
        ClearShorthandCaretTransaction();
        return ShorthandCaretTransactionResult::MutationRetained;
    }

    TF_SELECTION fallback_selection{};
    ULONG fallback_fetched = 0;
    ComPtr<ITfRange> fallback_range;
    HRESULT fallback_hr = context->GetSelection(
        ec, TF_DEFAULT_SELECTION, 1,
        &fallback_selection, &fallback_fetched);
    if (SUCCEEDED(fallback_hr) && fallback_fetched == 1 &&
        fallback_selection.range) {
        fallback_range.Attach(fallback_selection.range);
        fallback_hr = fallback_range->SetGravity(
            ec, TF_GRAVITY_FORWARD, TF_GRAVITY_FORWARD);
    } else if (fallback_selection.range) {
        fallback_selection.range->Release();
    }

    TF_SELECTION caret_selection{};
    caret_selection.range = shorthand_caret_range_.Get();
    caret_selection.style.ase = TF_AE_NONE;
    caret_selection.style.fInterimChar = FALSE;
    const HRESULT caret_hr = context->SetSelection(
        ec, 1, &caret_selection);
    if (SUCCEEDED(caret_hr)) {
        ClearShorthandCaretTransaction();
        return ShorthandCaretTransactionResult::Applied;
    }

    const HRESULT rollback_text_hr = shorthand_transaction_range_->SetText(
        ec, 0, shorthand_transaction_original_text_.c_str(),
        static_cast<LONG>(shorthand_transaction_original_text_.length()));
    HRESULT rollback_selection_hr = E_FAIL;
    if (SUCCEEDED(rollback_text_hr) && SUCCEEDED(fallback_hr) &&
        fallback_range) {
        TF_SELECTION selection{};
        selection.range = fallback_range.Get();
        selection.style.ase = TF_AE_NONE;
        selection.style.fInterimChar = FALSE;
        rollback_selection_hr = context->SetSelection(ec, 1, &selection);
    }
    const bool rolled_back = SUCCEEDED(rollback_text_hr);
    logger::LogFormat(
        logger::Level::Warning,
        L"Shorthand caret transaction finalize failed: caret=0x%08X, rollback_text=0x%08X, rollback_selection=0x%08X, rollback=%d",
        caret_hr, rollback_text_hr, rollback_selection_hr,
        rolled_back ? 1 : 0);
    ClearShorthandCaretTransaction();
    return rolled_back
        ? ShorthandCaretTransactionResult::RolledBack
        : ShorthandCaretTransactionResult::MutationRetained;
}

VietnameseIME::AppliedCompositionTransform
VietnameseIME::ApplyCompositionCommitTransforms(
    TfEditCookie ec, ITfContext* pic, wchar_t delimiter,
    bool allow_cursor) {
    AppliedCompositionTransform applied;
    if (!active_composition_) {
        return applied;
    }
    const bool secure_input = IsSecureInputContext();
    IMEConfig config = LoadConfigFromRegistry();
    ClearShorthandCaretTransaction();

    ComPtr<ITfRange> commit_range;
    if (SUCCEEDED(active_composition_->GetRange(
            commit_range.GetAddressOf())) && commit_range) {
        wchar_t commit_buf[256] = {0};
        ULONG commit_fetched = 0;
        if (SUCCEEDED(commit_range->GetText(
                ec, 0, commit_buf, 255, &commit_fetched)) &&
            commit_fetched > 0) {
            std::wstring commit_text(commit_buf, commit_fetched);
            bool shorthand_matched = false;

            if (!secure_input && config.enable_shorthand) {
                RefreshShorthandRulesIfChanged();
                auto expanded = LookUpShorthand(
                    commit_text, allow_cursor && !IsExcelApp());
                shorthand_matched = expanded &&
                    (expanded->text != commit_text ||
                     expanded->HasSelection());
                if (shorthand_matched) {
                    const HRESULT hr = commit_range->SetText(
                        ec, 0, expanded->text.c_str(),
                        static_cast<LONG>(expanded->text.length()));
                    if (SUCCEEDED(hr)) {
                        bool keep_mutation = true;
                        if (expanded->HasSelection()) {
                            ComPtr<ITfContext> context;
                            const HRESULT context_hr = commit_range->GetContext(
                                context.GetAddressOf());
                            const ShorthandCaretTransactionResult prepared =
                                PrepareShorthandCaretTransaction(
                                    ec,
                                    SUCCEEDED(context_hr)
                                        ? context.Get()
                                        : nullptr,
                                    commit_range.Get(), commit_text,
                                    *expanded->selection_start);
                            keep_mutation = prepared !=
                                ShorthandCaretTransactionResult::RolledBack;
                        }
                        if (keep_mutation) {
                            applied.transform_kind = CommitUndoEntry::TransformKind::
                                ShorthandExpansion;
                            commit_text = expanded->text;
                        } else {
                            shorthand_matched = false;
                        }
                    } else {
                        shorthand_matched = false;
                    }
                }
                if (expanded) {
                    SecureEraseString(expanded->text);
                }
            }

            if (!shorthand_matched) {
                std::wstring raw = engine_.GetRawString();
                std::wstring pre_speller =
                    engine_.GetPreCorrectionDisplayString();
                std::wstring previous_token;
                const bool fuzzy_enabled =
                    !secure_input && delimiter == L' ' && !IsExcelApp() &&
                    !IsInkscapeApp() && !IsFakeBackspaceApp() &&
                    IsFuzzyInputEffectivelyEnabled(
                        config.enable_fuzzy_input,
                        config.fuzzy_input_flags);
                if (fuzzy_enabled) {
                    previous_token = ReadImmediatePreviousTokenFromTsf(
                        ec, commit_range.Get(), commit_text);
                }
                core::CommitTransformDecision decision =
                    core::DecideCommitTransform({
                        raw,
                        commit_text,
                        engine_.GetInputMethod(),
                        engine_.GetCorrectionLevel(),
                        delimiter,
                        config.enable_auto_word_segmentation,
                        secure_input,
                        false,
                        fuzzy_enabled,
                        static_cast<core::FuzzyInputFlags>(
                            config.fuzzy_input_flags),
                        previous_token,
                        fuzzy_enabled && !previous_token.empty() && pic &&
                            !IsTelegramProcess(),
                        pre_speller,
                    });
                if (decision.RequiresRewrite()) {
                    bool rewrite_succeeded = false;
                    if (decision.rewrite_scope ==
                        core::CommitRewriteScope::CurrentToken) {
                        const HRESULT hr = commit_range->SetText(
                            ec, 0, decision.text.c_str(),
                            static_cast<LONG>(decision.text.length()));
                        rewrite_succeeded = SUCCEEDED(hr);
                    } else if (pic && !previous_token.empty()) {
                        const auto pair_plan =
                            core::BuildCompositionPairRewritePlan(
                                decision, previous_token, commit_text);
                        if (pair_plan) {
                            ComPtr<ITfRange> pair_range;
                            if (SUCCEEDED(pic->GetStart(ec, pair_range.GetAddressOf())) && pair_range) {
                                if (FAILED(pair_range->ShiftEndToRange(ec, commit_range.Get(), TF_ANCHOR_END)) ||
                                    FAILED(pair_range->ShiftStartToRange(ec, commit_range.Get(), TF_ANCHOR_START))) {
                                    pair_range.Reset();
                                }
                            }
                            if (!pair_range) {
                                (void)commit_range->Clone(pair_range.GetAddressOf());
                            }
                            if (pair_range) {
                                LONG shifted_start = 0;
                                const LONG source_prefix_length =
                                    static_cast<LONG>(
                                        pair_plan->source_previous.length() +
                                        1);
                                HRESULT pair_hr = pair_range->ShiftStart(
                                    ec, -source_prefix_length,
                                    &shifted_start, nullptr);

                                std::vector<wchar_t> source_buffer(
                                    decision.expected_source.length() + 1,
                                    L'\0');
                                ULONG source_fetched = 0;
                                const HRESULT source_hr =
                                    SUCCEEDED(pair_hr) &&
                                            shifted_start ==
                                                -source_prefix_length
                                        ? pair_range->GetText(
                                              ec, 0, source_buffer.data(),
                                              static_cast<ULONG>(
                                                  decision.expected_source
                                                      .length()),
                                              &source_fetched)
                                        : E_FAIL;
                                const bool source_matches =
                                    SUCCEEDED(source_hr) &&
                                    source_fetched ==
                                        decision.expected_source.length() &&
                                    std::wstring_view(
                                        source_buffer.data(), source_fetched) ==
                                        decision.expected_source;
                                SecureEraseVector(source_buffer);

                                logger::LogFormat(
                                    logger::Level::Info,
                                    L"Fuzzy pair rewrite check: shifted=%d/-%d, matched=%d, hr=0x%08X",
                                    shifted_start, source_prefix_length,
                                    source_matches ? 1 : 0, source_hr);

                                if (source_matches) {
                                    if (active_composition_) {
                                        HRESULT shift_comp_hr =
                                            active_composition_->ShiftStart(
                                                ec, pair_range.Get());
                                        logger::LogFormat(
                                            logger::Level::Info,
                                            L"Fuzzy active_composition_->ShiftStart: hr=0x%08X",
                                            shift_comp_hr);
                                    }
                                    // Replace the verified pair atomically.
                                    // Splitting this into two SetText calls
                                    // lets some TSF hosts invalidate the
                                    // sibling range after the first write.
                                    const HRESULT replace_hr =
                                        pair_range->SetText(
                                            ec, 0, decision.text.data(),
                                            static_cast<LONG>(
                                                decision.text.length()));
                                    logger::LogFormat(
                                        logger::Level::Info,
                                        L"Fuzzy pair SetText: hr=0x%08X",
                                        replace_hr);
                                    rewrite_succeeded = SUCCEEDED(replace_hr);
                                    if (rewrite_succeeded) {
                                        applied.committed_range =
                                            std::move(pair_range);
                                    }
                                }
                            }
                        }
                    }
                    if (rewrite_succeeded) {
                        applied.transform_kind = decision.transform_kind;
                        applied.original_text = decision.undo_text;
                        applied.display_text = decision.text;
                    }
                }
                SecureEraseString(decision.text);
                SecureEraseString(decision.expected_source);
                SecureEraseString(decision.undo_text);
                SecureEraseString(previous_token);
                SecureEraseString(pre_speller);
                SecureEraseString(raw);
            }
            SecureEraseString(commit_text);
        }
        SecureEraseBuffer(commit_buf, std::size(commit_buf));
    }

    if (!secure_input && config.enable_auto_capitalize) {
        ComPtr<ITfRange> comp_range;
        if (SUCCEEDED(active_composition_->GetRange(comp_range.GetAddressOf())) && comp_range) {
            ComPtr<ITfRange> context_range;
            if (SUCCEEDED(comp_range->Clone(context_range.GetAddressOf())) && context_range) {
                context_range->Collapse(ec, TF_ANCHOR_START);
                LONG shifted = 0;
                context_range->ShiftStart(ec, -20, &shifted, nullptr);
                
                wchar_t buf[32] = {0};
                ULONG fetched = 0;
                if (SUCCEEDED(context_range->GetText(ec, 0, buf, 31, &fetched)) && fetched > 0) {
                    std::wstring preceding_text(buf, fetched);
                    if (HasSentenceBoundaryBeforeCaret(preceding_text)) {
                        wchar_t comp_buf[256] = {0};
                        ULONG comp_fetched = 0;
                        comp_range->GetText(ec, 0, comp_buf, 255, &comp_fetched);
                        if (comp_fetched > 0) {
                            std::wstring comp_text(comp_buf, comp_fetched);
                            const wchar_t uppercase_first =
                                core::rules::ToUpper(comp_text[0]);
                            if (NeedsAutoCapitalizeRewrite(
                                    comp_text[0], uppercase_first)) {
                                comp_text[0] = uppercase_first;
                                comp_range->SetText(
                                    ec, 0, comp_text.c_str(),
                                    static_cast<LONG>(comp_text.length()));
                            }
                            SecureEraseString(comp_text);
                        }
                        SecureEraseBuffer(comp_buf, 256);
                    }
                    SecureEraseString(preceding_text);
                }
                SecureEraseBuffer(buf, 32);
            }
        }
    }

    return applied;
}

VietnameseIME::DirectCommitTransformDecision
VietnameseIME::BuildDirectCommitTransformDecision(
    std::wstring_view raw_token,
    std::wstring_view display_token,
    std::wstring_view pre_speller_token,
    wchar_t delimiter,
    std::wstring_view previous_token,
    bool allow_previous_token_rewrite,
    bool allow_cursor) {
    ClearShorthandCaretTransaction();
    if (GetFocusedProcessName() == L"anydesk.exe") {
        allow_cursor = false;
    }
    const bool secure_input = IsSecureInputContext();
    std::wstring working_text(display_token);
    bool shorthand_applied = false;
    std::optional<size_t> caret_offset;
    if (!secure_input && enable_shorthand_) {
        RefreshShorthandRulesIfChanged();
        auto expanded = LookUpShorthand(working_text, allow_cursor);
        shorthand_applied = expanded &&
            (expanded->text != working_text || expanded->HasSelection());
        if (shorthand_applied) {
            working_text = expanded->text;
            if (expanded->HasSelection()) {
                caret_offset = expanded->selection_start;
            }
        }
        if (expanded) {
            SecureEraseString(expanded->text);
        }
    }

    core::CommitTransformDecision decision = core::DecideCommitTransform({
        raw_token,
        working_text,
        engine_.GetInputMethod(),
        engine_.GetCorrectionLevel(),
        delimiter,
        enable_auto_word_segmentation_,
        secure_input,
        shorthand_applied,
        enable_fuzzy_input_ && !IsExcelApp() && !IsInkscapeApp() &&
            !IsFakeBackspaceApp(),
        fuzzy_input_flags_,
        previous_token,
        allow_previous_token_rewrite && !IsExcelApp() &&
            !IsTelegramProcess(),
        pre_speller_token,
    });
    if (shorthand_applied) {
        // The pure request sees the expanded text, while the host still owns
        // the original current-token display span.
        decision.expected_source.assign(display_token);
        decision.rewrite_scope = core::CommitRewriteScope::CurrentToken;
    }
    if (caret_offset && decision.text != working_text) {
        caret_offset.reset();
    }
    SecureEraseString(working_text);
    return DirectCommitTransformDecision{
        std::move(decision), caret_offset};
}

HRESULT VietnameseIME::EndComposition(
    TfEditCookie ec, bool apply_commit_transforms) {
    if (!active_composition_) return S_OK;

    if (apply_commit_transforms) {
        // This path has no post-commit transaction owner. CURSOR therefore
        // fails closed; explicit keyboard commits finalize it themselves.
        (void)ApplyCompositionCommitTransforms(
            ec, nullptr, L'\0', false);
    }

    // The commit source owns the caret policy. Internal SetText transforms can
    // move the host selection transiently and must not be mistaken for a click.
    const CommitCaretPolicy caret_policy = pending_commit_caret_policy_;
    
    // Unadvise Mouse Sink
    if (mouse_cookie_ != 0) {
        ComPtr<ITfRange> comp_range;
        if (SUCCEEDED(active_composition_->GetRange(comp_range.GetAddressOf())) && comp_range) {
            ComPtr<ITfContext> context;
            if (SUCCEEDED(comp_range->GetContext(context.GetAddressOf())) && context) {
                ComPtr<ITfMouseTracker> mouse_tracker;
                if (SUCCEEDED(context->QueryInterface(IID_ITfMouseTracker, reinterpret_cast<void**>(mouse_tracker.GetAddressOf())))) {
                    HRESULT hrMouse = mouse_tracker->UnadviseMouseSink(mouse_cookie_);
                    logger::LogFormat(logger::Level::Info, L"EndComposition: UnadviseMouseSink returned hr = 0x%08X", hrMouse);
                }
            }
        }
        mouse_cookie_ = 0;
    }

    if (ShouldMoveCommitCaretToCompositionEnd(caret_policy)) {
        // Keyboard commits finalize at the composition end. Mouse-triggered
        // commits preserve the host's newly positioned caret instead.
        ComPtr<ITfRange> final_comp_range;
        if (SUCCEEDED(active_composition_->GetRange(final_comp_range.GetAddressOf())) && final_comp_range) {
            ComPtr<ITfContext> context;
            if (SUCCEEDED(final_comp_range->GetContext(context.GetAddressOf())) && context) {
                ComPtr<ITfRange> caret_range;
                if (SUCCEEDED(final_comp_range->Clone(caret_range.GetAddressOf())) && caret_range) {
                    caret_range->Collapse(ec, TF_ANCHOR_END);

                    TF_SELECTION sel;
                    sel.range = caret_range.Get();
                    sel.style.ase = TF_AE_NONE;
                    sel.style.fInterimChar = FALSE;
                    HRESULT hrSel = context->SetSelection(ec, 1, &sel);
                    logger::LogFormat(logger::Level::Info, L"EndComposition: SetSelection to composition end returned hr = 0x%08X", hrSel);
                }
            }
        }
    }

    if (active_subclassed_hwnd_ != nullptr) {
        ::RemoveWindowSubclass(active_subclassed_hwnd_, MouseHookSubclassProc, 0x2026);
        logger::LogFormat(logger::Level::Info, L"EndComposition: Removed MouseHookSubclassProc subclass from HWND 0x%p", active_subclassed_hwnd_);
        active_subclassed_hwnd_ = nullptr;
    }
    if (active_subclassed_root_hwnd_ != nullptr) {
        ::RemoveWindowSubclass(active_subclassed_root_hwnd_, MouseHookSubclassProc, 0x2027);
        logger::LogFormat(logger::Level::Info, L"EndComposition: Removed MouseHookSubclassProc subclass from Root HWND 0x%p", active_subclassed_root_hwnd_);
        active_subclassed_root_hwnd_ = nullptr;
    }

    RemoveThreadCompositionHooks();

    UnadviseSelectionSink();

    if (IsExcelApp() &&
        excel_formula_state_ == core::ExcelFormulaSessionState::QuotedText) {
        excel_quote_chars_ += engine_.GetDisplayString().length();
        logger::LogFormat(logger::Level::Info, L"EndComposition (Excel QuotedText): added %zu chars, total quote_chars=%zu",
                          engine_.GetDisplayString().length(), excel_quote_chars_);
    }
    HRESULT hr = active_composition_->EndComposition(ec);
    active_composition_.Reset();
    ClearSensitiveState(false);
    return hr;
}

HRESULT VietnameseIME::AbortComposition(TfEditCookie ec, bool clear_text) {
    if (!active_composition_) {
        return S_OK;
    }

    HRESULT hrClearText = S_OK;
    HRESULT hrMouse = S_OK;
    HRESULT hrEnd = S_OK;
    ComPtr<ITfRange> comp_range;
    ComPtr<ITfContext> context;
    ComPtr<ITfComposition> composition = active_composition_;
    hrClearText = composition->GetRange(comp_range.GetAddressOf());
    if (SUCCEEDED(hrClearText) && comp_range) {
        hrClearText = clear_text
            ? comp_range->SetText(ec, 0, L"", 0)
            : S_OK;
        comp_range->GetContext(context.GetAddressOf());
    } else if (SUCCEEDED(hrClearText)) {
        hrClearText = E_POINTER;
    }
    if (!context && selection_context_) {
        context = selection_context_;
    }

    // End the same COM object that was captured before the termination
    // callback can reset active_composition_.  Do not tear down lifecycle
    // resources while TSF still reports an active composition.
    hrEnd = composition->EndComposition(ec);
    const bool composition_end_succeeded = SUCCEEDED(hrEnd);
    bool sink_cleanup_attempted = false;
    bool sink_cleanup_succeeded = mouse_cookie_ == 0;

    if (composition_end_succeeded) {
        if (mouse_cookie_ != 0) {
            sink_cleanup_attempted = true;
            if (context) {
                ComPtr<ITfMouseTracker> mouse_tracker;
                if (SUCCEEDED(context->QueryInterface(
                        IID_ITfMouseTracker,
                        reinterpret_cast<void**>(mouse_tracker.GetAddressOf()))) &&
                    mouse_tracker) {
                    hrMouse = mouse_tracker->UnadviseMouseSink(mouse_cookie_);
                } else {
                    hrMouse = E_NOINTERFACE;
                }
            } else {
                hrMouse = E_FAIL;
            }
            mouse_cookie_ = 0;
            sink_cleanup_succeeded = SUCCEEDED(hrMouse);
        }

        if (active_subclassed_hwnd_ != nullptr) {
            ::RemoveWindowSubclass(active_subclassed_hwnd_, MouseHookSubclassProc, 0x2026);
            active_subclassed_hwnd_ = nullptr;
        }
        if (active_subclassed_root_hwnd_ != nullptr) {
            ::RemoveWindowSubclass(active_subclassed_root_hwnd_, MouseHookSubclassProc, 0x2027);
            active_subclassed_root_hwnd_ = nullptr;
        }

        RemoveThreadCompositionHooks();

        UnadviseSelectionSink();

        // EndComposition succeeded, so releasing a callback that did not
        // clear the member is now lifecycle-safe.
        active_composition_.Reset();
        ClearSensitiveState(false);
    }

    const bool active_cleared = composition_end_succeeded && !HasActiveComposition();
    const bool document_cleanup_succeeded =
        IsCommitUndoDocumentCleanupSuccessful(
            SUCCEEDED(hrClearText), SUCCEEDED(hrEnd), active_cleared);
    logger::LogFormat(
        !clear_text && document_cleanup_succeeded
            ? logger::Level::Info
            : logger::Level::Warning,
        L"AbortComposition: clear_text=%d, clear_hr=0x%08X, mouse_hr=0x%08X, end_hr=0x%08X, active_cleared=%s, "
        L"document_cleanup=%s, sink_attempted=%s, sink_cleanup=%s, "
        L"text_edit_cookie=%u, mouse_cookie=%u",
        clear_text ? 1 : 0, hrClearText, hrMouse, hrEnd,
        active_cleared ? L"TRUE" : L"FALSE",
        document_cleanup_succeeded ? L"TRUE" : L"FALSE",
        sink_cleanup_attempted ? L"TRUE" : L"FALSE",
        sink_cleanup_succeeded ? L"TRUE" : L"FALSE",
        text_edit_cookie_, mouse_cookie_);
    if (!composition_end_succeeded) {
        logger::LogFormat(
            logger::Level::Warning,
            L"AbortComposition: EndComposition failed; retaining active TSF lifecycle, hr=0x%08X",
            hrEnd);
    } else if (sink_cleanup_attempted && !sink_cleanup_succeeded) {
        logger::LogFormat(
            logger::Level::Warning,
            L"AbortComposition: mouse sink teardown failed, hr=0x%08X, document_cleanup=%s",
            hrMouse, document_cleanup_succeeded ? L"TRUE" : L"FALSE");
    }
    if (document_cleanup_succeeded) {
        return S_OK;
    }
    if (FAILED(hrClearText)) {
        return hrClearText;
    }
    if (FAILED(hrEnd)) {
        return hrEnd;
    }
    return E_FAIL;
}

HRESULT VietnameseIME::UpdateCompositionText(TfEditCookie ec, ITfContext* pic, ITfRange* range, const std::wstring& text) {
    if (IsSecureInputContext()) {
        ClearSensitiveState(false);
        return E_FAIL;
    }
    logger::LogFormat(logger::Level::Info, L"UpdateCompositionText called: text_length = %zu", text.length());
    if (!active_composition_) {
        logger::Log(logger::Level::Error, L"UpdateCompositionText: active_composition_ is null");
        return E_FAIL;
    }
    
    ComPtr<ITfRange> comp_range;
    HRESULT hr = active_composition_->GetRange(comp_range.GetAddressOf());
    logger::LogFormat(logger::Level::Info, L"UpdateCompositionText: active_composition_->GetRange returned hr = 0x%08X", hr);
    if (FAILED(hr)) return hr;
    
    hr = comp_range->SetText(ec, 0, text.c_str(), static_cast<LONG>(text.length()));
    logger::LogFormat(logger::Level::Info, L"UpdateCompositionText: comp_range->SetText returned hr = 0x%08X", hr);
    if (FAILED(hr)) return hr;
    
    if (display_attribute_atom_ != 0 && !IsInkscapeApp()) {
        ComPtr<ITfProperty> prop;
        if (SUCCEEDED(pic->GetProperty(GUID_PROP_ATTRIBUTE, prop.GetAddressOf()))) {
            VARIANT var;
            var.vt = VT_I4;
            var.lVal = static_cast<LONG>(display_attribute_atom_);
            HRESULT hrProp = prop->SetValue(ec, comp_range.Get(), &var);
            logger::LogFormat(logger::Level::Info, L"UpdateCompositionText: prop->SetValue returned hr = 0x%08X", hrProp);
        } else {
            logger::Log(logger::Level::Warning, L"UpdateCompositionText: GetProperty(GUID_PROP_ATTRIBUTE) failed");
        }
    } else if (display_attribute_atom_ == 0) {
        logger::Log(logger::Level::Warning, L"UpdateCompositionText: display_attribute_atom_ is 0");
    }
    
    ComPtr<ITfRange> end_range;
    hr = comp_range->Clone(end_range.GetAddressOf());
    if (SUCCEEDED(hr)) {
        end_range->Collapse(ec, TF_ANCHOR_END);
        TF_SELECTION sel;
        sel.range = end_range.Get();
        sel.style.ase = TF_AE_NONE;
        sel.style.fInterimChar = FALSE;
        HRESULT hrSetSel = pic->SetSelection(ec, 1, &sel);
        logger::LogFormat(logger::Level::Info, L"UpdateCompositionText: pic->SetSelection (caret update) returned hr = 0x%08X", hrSetSel);
    } else {
        logger::LogFormat(logger::Level::Warning, L"UpdateCompositionText: Clone returned hr = 0x%08X", hr);
    }

    return S_OK;
}

void VietnameseIME::CommitCompositionAsync(ITfContext* pic, WORD replay_vk) {
    if (!active_composition_) return;
    
    ComPtr<ITfEditSession> session;
    session.Attach(new (std::nothrow) EditSession(this, pic, EditAction::Commit, L'\0', nullptr, replay_vk));
    if (session) {
        HRESULT hr = 0;
        pic->RequestEditSession(client_id_, session.Get(), TF_ES_ASYNCDONTCARE | TF_ES_READWRITE, &hr);
    }
}

void VietnameseIME::CommitCompositionSync(
    ITfContext* pic,
    WORD replay_vk,
    wchar_t host_owned_commit_delimiter) {
    if (!active_composition_) return;
    
    ComPtr<ITfEditSession> session;
    session.Attach(new (std::nothrow) EditSession(
        this, pic, EditAction::Commit, L'\0', nullptr, replay_vk,
        host_owned_commit_delimiter));
    if (session) {
        HRESULT hr = 0;
        HRESULT hrReq = pic->RequestEditSession(client_id_, session.Get(), TF_ES_SYNC | TF_ES_READWRITE, &hr);
        if (FAILED(hrReq) || FAILED(hr)) {
            logger::LogFormat(logger::Level::Warning, L"CommitCompositionSync: Sync request failed (hrReq = 0x%08X, hr = 0x%08X), falling back to async", hrReq, hr);
            pic->RequestEditSession(client_id_, session.Get(), TF_ES_ASYNCDONTCARE | TF_ES_READWRITE, &hr);
        }
    }
}

// ITfFunctionProvider methods
STDMETHODIMP VietnameseIME::GetType(GUID* pguid) {
    if (!pguid) return E_INVALIDARG;
    *pguid = CLSID_VietnameseIME;
    return S_OK;
}

STDMETHODIMP VietnameseIME::GetDescription(BSTR* pbstrDesc) {
    if (!pbstrDesc) return E_INVALIDARG;
    *pbstrDesc = SysAllocString(L"Vietnamese IME Function Provider");
    return *pbstrDesc ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP VietnameseIME::GetFunction(REFGUID rguid, REFIID riid, IUnknown** ppunk) {
    if (!ppunk) return E_INVALIDARG;
    *ppunk = nullptr;
    if (rguid == IID_ITfFnReconversion) {
        return QueryInterface(riid, reinterpret_cast<void**>(ppunk));
    }
    return E_INVALIDARG;
}

// ITfFunction methods (base of ITfFnReconversion)
STDMETHODIMP VietnameseIME::GetDisplayName(BSTR* pbstrName) {
    if (!pbstrName) return E_INVALIDARG;
    *pbstrName = SysAllocString(L"Reconversion");
    return *pbstrName ? S_OK : E_OUTOFMEMORY;
}

// ITfFnReconversion methods
STDMETHODIMP VietnameseIME::QueryRange(ITfRange* pRange, ITfRange** ppNewRange, BOOL* pfConvertible) {
    if (!pRange || !pfConvertible) return E_INVALIDARG;
    if (ppNewRange) *ppNewRange = nullptr;
    *pfConvertible = FALSE;
    if (IsSecureInputContext()) return S_OK;

    ComPtr<ITfContext> context;
    HRESULT hr = pRange->GetContext(context.GetAddressOf());
    if (FAILED(hr) || !context) return FAILED(hr) ? hr : E_FAIL;
    ComPtr<EditSession> session;
    session.Attach(new (std::nothrow) EditSession(this, context.Get(), EditAction::QueryReconversionRange, 0, pRange));
    if (!session) return E_OUTOFMEMORY;
    HRESULT session_hr = S_OK;
    hr = context->RequestEditSession(client_id_, session.Get(), TF_ES_SYNC | TF_ES_READ, &session_hr);
    if (FAILED(hr) || FAILED(session_hr)) return FAILED(hr) ? hr : session_hr;
    if (!session->is_convertible()) return S_OK;
    *pfConvertible = TRUE;
    if (ppNewRange) {
        *ppNewRange = session->detach_result_range();
    }
    return S_OK;
}

STDMETHODIMP VietnameseIME::GetReconversion(ITfRange* pRange, ITfCandidateList** ppCandList) {
    if (!pRange || !ppCandList) return E_INVALIDARG;
    *ppCandList = nullptr;
    if (IsSecureInputContext()) return E_FAIL;
    ComPtr<ITfContext> context;
    HRESULT hr = pRange->GetContext(context.GetAddressOf());
    if (FAILED(hr) || !context) return FAILED(hr) ? hr : E_FAIL;
    ComPtr<EditSession> session;
    session.Attach(new (std::nothrow) EditSession(this, context.Get(), EditAction::ReadReconversionText, 0, pRange));
    if (!session) return E_OUTOFMEMORY;
    HRESULT session_hr = S_OK;
    hr = context->RequestEditSession(client_id_, session.Get(), TF_ES_SYNC | TF_ES_READ, &session_hr);
    if (FAILED(hr) || FAILED(session_hr)) return FAILED(hr) ? hr : session_hr;
    if (!session->is_convertible()) return E_FAIL;
    *ppCandList = new (std::nothrow) ReconversionCandidateList(session->get_result_text());
    return *ppCandList ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP VietnameseIME::Reconvert(ITfRange* pRange) {
    if (!pRange || HasActiveComposition() || IsSecureInputContext()) return E_INVALIDARG;
    ComPtr<ITfContext> context;
    HRESULT hr = pRange->GetContext(context.GetAddressOf());
    if (FAILED(hr) || !context) return FAILED(hr) ? hr : E_FAIL;
    ComPtr<EditSession> session;
    session.Attach(new (std::nothrow) EditSession(this, context.Get(), EditAction::StartReconversion, 0, pRange));
    if (!session) return E_OUTOFMEMORY;
    HRESULT session_hr = S_OK;
    hr = context->RequestEditSession(client_id_, session.Get(), TF_ES_SYNC | TF_ES_READWRITE, &session_hr);
    if (FAILED(hr) || FAILED(session_hr)) return FAILED(hr) ? hr : session_hr;
    return session->is_convertible() ? S_OK : E_FAIL;
}

// ITfMouseSink methods
STDMETHODIMP VietnameseIME::OnMouseEvent(ULONG uEdge, ULONG uQuadrant, DWORD dwBtnStatus, BOOL* pfEaten) {
    if (!pfEaten) return E_INVALIDARG;
    
    logger::LogFormat(logger::Level::Info, L"OnMouseEvent: uEdge = %u, uQuadrant = %u, dwBtnStatus = 0x%X", uEdge, uQuadrant, dwBtnStatus);

    const bool button_click =
        (dwBtnStatus & (MK_LBUTTON | MK_RBUTTON | MK_MBUTTON)) != 0;
    if (button_click && last_commit_undo_ &&
        ShouldCaptureSmartUndo(*last_commit_undo_)) {
        ClearLastCommitUndo();
    }
    
    if (IsBrowserProcess()) {
        logger::Log(logger::Level::Info, L"OnMouseEvent: Browser process detected, skipping forced commit");
        *pfEaten = FALSE;
        return S_OK;
    }
    
    // If a button click occurred (left, right, or middle mouse button)
    // We should commit the active composition.
    if (button_click) {
        ResetExcelFormulaSession(L"mouse_click");
        if (!mouse_commit_pending_) {
            logger::Log(logger::Level::Info, L"OnMouseEvent: Mouse click detected, committing composition asynchronously");
        
            // We need an ITfContext to commit the composition.
            // We can get the context from the active composition's range.
            if (active_composition_) {
                ComPtr<ITfRange> comp_range;
                if (SUCCEEDED(active_composition_->GetRange(comp_range.GetAddressOf())) && comp_range) {
                    ComPtr<ITfContext> context;
                    if (SUCCEEDED(comp_range->GetContext(context.GetAddressOf())) && context) {
                        pending_commit_caret_policy_ = CommitCaretPolicy::PreserveHostSelection;
                        mouse_commit_pending_ = true;
                        CommitCompositionAsync(context.Get());
                    }
                }
            }
        }
    }
    
    // Always set pfEaten to FALSE so the application handles the click (e.g. moves the caret)
    *pfEaten = FALSE;
    return S_OK;
}

// Key translation helper
wchar_t VietnameseIME::TranslateKey(WPARAM wParam, LPARAM lParam) const {
    wchar_t ch = 0;
    BYTE keyboard_state[256]{};
    const bool num_lock_on =
        (::GetKeyState(VK_NUMLOCK) & 0x0001) != 0;
    if (::GetKeyboardState(keyboard_state)) {
        const UINT scan_code =
            static_cast<UINT>((lParam >> 16) & 0xFF);
        const HKL keyboard_layout = ::GetKeyboardLayout(0);
        ch = TranslateVirtualKeyWithoutStateMutation(
            static_cast<UINT>(wParam),
            scan_code,
            keyboard_state,
            keyboard_layout,
            num_lock_on,
            [](UINT virtual_key, UINT scan, const BYTE* state,
               LPWSTR buffer, int buffer_size, UINT flags, HKL layout) {
                return ::ToUnicodeEx(
                    virtual_key, scan, state, buffer, buffer_size,
                    flags, layout);
            });
    }
    if (ch == 0 && wParam >= 0x60 && wParam <= 0x69) {
        if (num_lock_on) {
            ch = L'0' + (static_cast<wchar_t>(wParam) - 0x60);
        }
    }
    return ch;
}

void VietnameseIME::ReloadConfig() {
    telegram_synthetic_selection_suppression_.Clear();
    if (telegram_boundary_resume_state_.IsPending()) {
        ClearLastCommitUndo();
    }
    if (telegram_raw_replay_state_.IsPending()) {
        ClearTelegramRawReplay();
    }
    IMEConfig config = LoadConfigFromRegistry();
    logger::SetEnabled(config.enable_log);
    logger::Log(logger::Level::Info, L"VietnameseIME::ReloadConfig loading configuration...");
    engine_.SetCorrectionLevel(config.auto_correct_level);
    engine_.SetEnglishProtectionLevel(config.english_protection_level);
    engine_.SetSmartContextProtection(
        config.enable_smart_context_protection);
    enable_smart_undo_ = config.enable_smart_undo;
    enable_shorthand_ = config.enable_shorthand;
    enable_auto_word_segmentation_ =
        config.enable_auto_word_segmentation;
    fuzzy_input_flags_ = core::SanitizeFuzzyInputFlags(
        static_cast<core::FuzzyInputFlags>(config.fuzzy_input_flags));
    enable_fuzzy_input_ = IsFuzzyInputEffectivelyEnabled(
        config.enable_fuzzy_input,
        static_cast<DWORD>(fuzzy_input_flags_));
    global_input_method_ = config.input_method;
    global_typing_mode_ = config.typing_mode;
    enable_app_input_profiles_ = config.enable_app_input_profiles;
    enable_auto_app_input_profiles_ =
        config.enable_auto_app_input_profiles;
    app_input_profiles_ = NormalizeAppInputProfiles(
        config.app_input_profiles);
    direct_apps_.clear();
    for (const auto& app_str : config.direct_apps) {
        if (app_str.empty()) continue;
        
        std::wstring raw_app = app_str;
        std::wstring mode = L"inline";
        size_t colon = raw_app.find_last_of(L':');
        if (colon != std::wstring::npos && colon > 1) {
            mode = raw_app.substr(colon + 1);
            raw_app = raw_app.substr(0, colon);
        }
        
        std::wstring norm_name = NormalizeProcessName(raw_app);
        if (norm_name.empty()) continue;
        
        DirectAppConfig item;
        item.process_name = norm_name;
        
        for (wchar_t& c : mode) {
            if (c >= L'A' && c <= L'Z') {
                c = c - L'A' + L'a';
            }
        }
        while (!mode.empty() && (mode.front() == L' ' || mode.front() == L'\t')) mode.erase(0, 1);
        while (!mode.empty() && (mode.back() == L' ' || mode.back() == L'\t' || mode.back() == L'\r' || mode.back() == L'\n')) mode.pop_back();
        
        item.is_commit = (mode == L"commit");
        direct_apps_.push_back(item);
    }
    cached_process_id_ = 0;
    cached_process_name_.clear();
    effective_process_name_ = host_process_name_.empty()
        ? GetFocusedProcessName()
        : host_process_name_;
    const ResolvedAppInputProfile effective =
        ResolveEffectiveAppInputProfile(
            enable_app_input_profiles_, app_input_profiles_,
            effective_process_name_, global_typing_mode_ == 0,
            global_input_method_);
    current_app_explicitly_disabled_ =
        IsExplicitAppInputProfileDisabled(
            enable_app_input_profiles_, effective);
    engine_.SetInputMethod(effective.input_method);
    typing_mode_ = effective.enabled ? 0 : 1;
    if (hotkey_mode_ != config.hotkey_mode) {
        hotkey_toggle_state_.Reset();
    }
    hotkey_mode_ = config.hotkey_mode;

    // Load shorthand rules
    LoadShorthandRules();

    logger::LogFormat(logger::Level::Info, L"Config loaded: global_input_method = %d, effective_input_method = %d, auto_correct_level = %d, enable_log = %s, enable_shorthand = %s, enable_smart_undo = %s, enable_smart_context = %s, enable_segmentation = %s, enable_fuzzy = %s, fuzzy_flags = 0x%X, enable_app_profiles = %s, app_profiles = %zu, enable_auto_profiles = %s, effective_typing_mode = %u, hotkey_mode = %u",
                      static_cast<int>(global_input_method_), static_cast<int>(effective.input_method), static_cast<int>(config.auto_correct_level),
                      config.enable_log ? L"true" : L"false", config.enable_shorthand ? L"true" : L"false",
                      enable_smart_undo_ ? L"true" : L"false",
                      engine_.GetSmartContextProtection() ? L"true" : L"false",
                      enable_auto_word_segmentation_ ? L"true" : L"false",
                      enable_fuzzy_input_ ? L"true" : L"false",
                      static_cast<unsigned int>(fuzzy_input_flags_),
                      enable_app_input_profiles_ ? L"true" : L"false", app_input_profiles_.size(),
                      enable_auto_app_input_profiles_ ? L"true" : L"false",
                      typing_mode_, hotkey_mode_);
}

std::optional<DynamicShorthandResult> VietnameseIME::LookUpShorthand(
    const std::wstring& shortcut, bool allow_cursor) {
    if (shorthand_map_.empty() || shortcut.empty()) {
        return std::nullopt;
    }

    // Normalize shortcut to lower case for map lookup
    std::wstring lower_shortcut;
    lower_shortcut.reserve(shortcut.length());
    for (wchar_t c : shortcut) {
        lower_shortcut.push_back(core::rules::ToLower(c));
    }

    auto it = shorthand_map_.find(lower_shortcut);
    if (it == shorthand_map_.end()) {
        return std::nullopt;
    }

    const std::wstring& expansion_rule = it->second;
    if (expansion_rule.empty()) {
        return std::nullopt;
    }

    std::wstring expansion = expansion_rule;

    // Casing checks
    bool all_upper = true;
    for (wchar_t c : shortcut) {
        if (IsLowerChar(c)) all_upper = false;
    }

    // Case preservation logic
    if (all_upper) {
        for (wchar_t& c : expansion) {
            c = core::rules::ToUpper(c);
        }
    } else {
        // Capitalized (first character uppercase, rest lowercase)
        bool first_upper_rest_lower = false;
        if (IsUpperChar(shortcut[0])) {
            bool rest_lower = true;
            for (size_t i = 1; i < shortcut.length(); ++i) {
                if (IsUpperChar(shortcut[i])) {
                    rest_lower = false;
                    break;
                }
            }
            if (rest_lower) {
                first_upper_rest_lower = true;
            }
        }

        if (first_upper_rest_lower) {
            expansion[0] = core::rules::ToUpper(expansion[0]);
        }
    }

    const bool needs_date = HasShorthandDateTag(expansion);
    const bool needs_time = HasShorthandTimeTag(expansion);
    const bool needs_weekday = HasShorthandWeekdayTag(expansion);
    const bool needs_uuid = HasShorthandUuidTag(expansion);
    const bool needs_clipboard = HasShorthandClipboardTag(expansion);
    const bool needs_clipboard_trim =
        HasShorthandClipboardTrimTag(expansion);
    const bool needs_clipboard_upper =
        HasShorthandClipboardUpperTag(expansion);
    const bool needs_clipboard_lower =
        HasShorthandClipboardLowerTag(expansion);
    const bool needs_selection = HasShorthandSelectionTag(expansion);
    const bool needs_cursor = HasShorthandCursorTag(expansion);
    if (needs_cursor && !allow_cursor) {
        SecureEraseString(expansion);
        return std::nullopt;
    }
    if (!needs_date && !needs_time && !needs_weekday &&
        !needs_uuid && !needs_clipboard && !needs_selection &&
        !needs_cursor) {
        return DynamicShorthandResult{std::move(expansion), {}, {}};
    }

    DynamicShorthandValues values;
    std::wstring formatted_date;
    std::wstring formatted_time;
    std::wstring generated_uuid;
    SensitiveWString clipboard_text;
    SensitiveWString clipboard_trimmed;
    SensitiveWString clipboard_upper;
    SensitiveWString clipboard_lower;

    if (needs_date || needs_time || needs_weekday) {
        SYSTEMTIME local_time{};
        ::GetLocalTime(&local_time);
        if (needs_date) {
            auto date = FormatShorthandDate(
                local_time.wDay, local_time.wMonth, local_time.wYear);
            if (!date) {
                SecureEraseString(expansion);
                return std::nullopt;
            }
            formatted_date = std::move(*date);
            values.date = std::wstring_view(formatted_date);
        }
        if (needs_time) {
            auto time = FormatShorthandTime(
                local_time.wHour, local_time.wMinute);
            if (!time) {
                SecureEraseString(expansion);
                return std::nullopt;
            }
            formatted_time = std::move(*time);
            values.time = std::wstring_view(formatted_time);
        }
        if (needs_weekday) {
            const auto weekday = FormatShorthandWeekday(
                local_time.wDayOfWeek);
            if (!weekday) {
                SecureEraseString(expansion);
                return std::nullopt;
            }
            values.weekday = *weekday;
        }
    }

    if (needs_uuid) {
        if (!GenerateShorthandUuid(generated_uuid)) {
            SecureEraseString(expansion);
            return std::nullopt;
        }
        values.uuid = std::wstring_view(generated_uuid);
    }

    if (needs_clipboard) {
        if (!ReadUnicodeClipboardTextBounded(
                MAX_SHORTHAND_VALUE_CHARS, clipboard_text.value)) {
            SecureEraseString(expansion);
            return std::nullopt;
        }
        values.clipboard = std::wstring_view(clipboard_text.value);
        if (needs_clipboard_trim) {
            clipboard_trimmed.value = TrimShorthandText(
                clipboard_text.value);
            values.clipboard_trim =
                std::wstring_view(clipboard_trimmed.value);
        }
        if (needs_clipboard_upper) {
            if (!MapShorthandTextCaseBounded(
                    clipboard_text.value, LCMAP_UPPERCASE,
                    MAX_SHORTHAND_VALUE_CHARS,
                    clipboard_upper.value)) {
                SecureEraseString(expansion);
                return std::nullopt;
            }
            values.clipboard_upper =
                std::wstring_view(clipboard_upper.value);
        }
        if (needs_clipboard_lower) {
            if (!MapShorthandTextCaseBounded(
                    clipboard_text.value, LCMAP_LOWERCASE,
                    MAX_SHORTHAND_VALUE_CHARS,
                    clipboard_lower.value)) {
                SecureEraseString(expansion);
                return std::nullopt;
            }
            values.clipboard_lower =
                std::wstring_view(clipboard_lower.value);
        }
    }

    if (needs_selection) {
        if (!pending_shorthand_selection_) {
            SecureEraseString(expansion);
            return std::nullopt;
        }
        values.selection =
            std::wstring_view(*pending_shorthand_selection_);
    }

    auto resolved = ResolveDynamicShorthandTemplateWithSelection(
        expansion, values, MAX_SHORTHAND_VALUE_CHARS);
    SecureEraseString(expansion);
    if (!resolved) {
        return std::nullopt;
    }
    return resolved;
}

void VietnameseIME::ClearPendingShorthandSelection() noexcept {
    if (pending_shorthand_selection_) {
        SecureEraseString(*pending_shorthand_selection_);
        pending_shorthand_selection_.reset();
    }
}

bool VietnameseIME::HasSelectionShorthandStartingWith(
    wchar_t first_char) const {
    const wchar_t normalized = core::rules::ToLower(first_char);
    for (const auto& [key, value] : shorthand_map_) {
        if (!key.empty() && key.front() == normalized &&
            HasShorthandSelectionTag(value)) {
            return true;
        }
    }
    return false;
}

bool VietnameseIME::CaptureTsfShorthandSelection(
    TfEditCookie ec, ITfRange* selection_range,
    wchar_t first_char) {
    if (pending_shorthand_selection_) {
        return true;
    }
    if (!selection_range || !enable_shorthand_ ||
        IsSecureInputContext() || IsExcelApp()) {
        return false;
    }
    RefreshShorthandRulesIfChanged();
    if (!HasSelectionShorthandStartingWith(first_char)) {
        return false;
    }

    BOOL empty = TRUE;
    if (FAILED(selection_range->IsEmpty(ec, &empty)) || empty) {
        return false;
    }
    std::vector<wchar_t> buffer(
        MAX_SHORTHAND_VALUE_CHARS + 1, L'\0');
    ULONG fetched = 0;
    const HRESULT hr = selection_range->GetText(
        ec, 0, buffer.data(), static_cast<ULONG>(buffer.size()), &fetched);
    if (SUCCEEDED(hr) && fetched > 0 &&
        fetched <= MAX_SHORTHAND_VALUE_CHARS) {
        pending_shorthand_selection_.emplace(
            buffer.data(), static_cast<size_t>(fetched));
    }
    SecureEraseVector(buffer);
    return pending_shorthand_selection_.has_value();
}

bool VietnameseIME::CaptureFocusedWin32ShorthandSelection(
    ITfContext* pic, wchar_t first_char) {
    if (pending_shorthand_selection_) {
        return true;
    }
    if (!pic || !enable_shorthand_ || IsSecureInputContext() ||
        IsExcelApp()) {
        return false;
    }

    std::vector<HWND> candidates;
    candidates.reserve(32);
    const auto add_candidate = [&candidates](HWND hwnd) {
        if (!hwnd || candidates.size() >= 128 ||
            std::find(candidates.begin(), candidates.end(), hwnd) !=
                candidates.end()) {
            return;
        }
        DWORD process_id = 0;
        ::GetWindowThreadProcessId(hwnd, &process_id);
        if (process_id == ::GetCurrentProcessId()) {
            candidates.push_back(hwnd);
        }
    };

    const HWND focus_hwnd = GetBestFocusWindow();
    const HWND context_hwnd = GetContextViewWindow(pic);
    add_candidate(focus_hwnd);
    add_candidate(context_hwnd);

    HWND root_hwnd = focus_hwnd
        ? ::GetAncestor(focus_hwnd, GA_ROOT)
        : nullptr;
    if (!root_hwnd && context_hwnd) {
        root_hwnd = ::GetAncestor(context_hwnd, GA_ROOT);
    }
    if (!root_hwnd) {
        root_hwnd = ::GetForegroundWindow();
    }
    add_candidate(root_hwnd);

    struct ChildCollector {
        DWORD process_id;
        std::vector<HWND>* candidates;
    } collector{::GetCurrentProcessId(), &candidates};
    if (root_hwnd) {
        ::EnumChildWindows(
            root_hwnd,
            [](HWND child, LPARAM parameter) -> BOOL {
                auto* state =
                    reinterpret_cast<ChildCollector*>(parameter);
                if (!state || !state->candidates ||
                    state->candidates->size() >= 128) {
                    return FALSE;
                }
                DWORD process_id = 0;
                ::GetWindowThreadProcessId(child, &process_id);
                if (process_id == state->process_id &&
                    ::IsWindowVisible(child) &&
                    std::find(
                        state->candidates->begin(),
                        state->candidates->end(), child) ==
                        state->candidates->end()) {
                    state->candidates->push_back(child);
                }
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&collector));
    }

    struct NativeSelection {
        HWND hwnd = nullptr;
        DWORD start = 0;
        DWORD end = 0;
    };
    std::optional<NativeSelection> selected_control;
    for (const HWND hwnd : candidates) {
        if (!::IsWindowVisible(hwnd)) {
            continue;
        }

        DWORD process_id = 0;
        ::GetWindowThreadProcessId(hwnd, &process_id);
        if (process_id != ::GetCurrentProcessId()) {
            continue;
        }

        const std::wstring class_name = GetClassNameOrEmpty(hwnd);
        const bool is_edit = _wcsicmp(class_name.c_str(), L"Edit") == 0;
        const bool is_rich_edit =
            class_name.size() >= 8 &&
            _wcsnicmp(class_name.c_str(), L"RichEdit", 8) == 0;
        if (!is_edit && !is_rich_edit) {
            continue;
        }
        if ((::GetWindowLongPtrW(hwnd, GWL_STYLE) & ES_PASSWORD) != 0) {
            continue;
        }

        DWORD selection_start = 0;
        DWORD selection_end = 0;
        DWORD_PTR message_result = 0;
        if (::SendMessageTimeoutW(
                hwnd, EM_GETSEL,
                reinterpret_cast<WPARAM>(&selection_start),
                reinterpret_cast<LPARAM>(&selection_end),
                SMTO_ABORTIFHUNG | SMTO_BLOCK, 50,
                &message_result) == 0) {
            continue;
        }
        if (selection_start == selection_end) {
            continue;
        }
        if (selected_control) {
            return false;
        }
        selected_control = NativeSelection{
            hwnd, selection_start, selection_end};
    }

    if (!selected_control) {
        return false;
    }
    CaptureWin32ShorthandSelection(
        selected_control->hwnd,
        static_cast<size_t>(selected_control->start),
        static_cast<size_t>(selected_control->end), first_char);
    return pending_shorthand_selection_.has_value();
}

void VietnameseIME::CaptureWin32ShorthandSelection(
    HWND hwnd, size_t selection_start, size_t selection_end,
    wchar_t first_char) {
    ClearPendingShorthandSelection();
    if (!hwnd || !enable_shorthand_ || IsSecureInputContext()) {
        return;
    }
    RefreshShorthandRulesIfChanged();
    if (!HasSelectionShorthandStartingWith(first_char)) {
        return;
    }
    if (selection_start > selection_end) {
        (std::swap)(selection_start, selection_end);
    }
    const size_t selection_length = selection_end - selection_start;
    if (selection_length == 0 ||
        selection_length > MAX_SHORTHAND_VALUE_CHARS) {
        return;
    }

    DWORD process_id = 0;
    ::GetWindowThreadProcessId(hwnd, &process_id);
    if (process_id != ::GetCurrentProcessId() ||
        (::GetWindowLongPtrW(hwnd, GWL_STYLE) & ES_PASSWORD) != 0) {
        return;
    }

    const std::wstring class_name = GetClassNameOrEmpty(hwnd);
    const bool is_edit = _wcsicmp(class_name.c_str(), L"Edit") == 0;
    const bool is_rich_edit =
        class_name.size() >= 8 &&
        _wcsnicmp(class_name.c_str(), L"RichEdit", 8) == 0;
    if (!is_edit && !is_rich_edit) {
        return;
    }

    if (is_rich_edit &&
        selection_start <= static_cast<size_t>((std::numeric_limits<LONG>::max)()) &&
        selection_end <= static_cast<size_t>((std::numeric_limits<LONG>::max)())) {
        std::vector<wchar_t> selection(selection_length + 1, L'\0');
        TEXTRANGEW text_range{};
        text_range.chrg.cpMin = static_cast<LONG>(selection_start);
        text_range.chrg.cpMax = static_cast<LONG>(selection_end);
        text_range.lpstrText = selection.data();
        DWORD_PTR copied_result = 0;
        const LRESULT delivered = ::SendMessageTimeoutW(
            hwnd, EM_GETTEXTRANGE, 0,
            reinterpret_cast<LPARAM>(&text_range),
            SMTO_ABORTIFHUNG | SMTO_BLOCK, 50, &copied_result);
        const size_t copied = static_cast<size_t>(copied_result);
        if (delivered != 0 && copied > 0 && copied <= selection_length) {
            pending_shorthand_selection_.emplace(
                selection.data(), copied);
        }
        SecureEraseVector(selection);
        return;
    }

    const int window_length = ::GetWindowTextLengthW(hwnd);
    const size_t max_window_chars = MAX_SHORTHAND_VALUE_CHARS * 4;
    if (window_length < 0 ||
        static_cast<size_t>(window_length) < selection_end ||
        static_cast<size_t>(window_length) > max_window_chars) {
        return;
    }
    std::vector<wchar_t> window(
        static_cast<size_t>(window_length) + 1, L'\0');
    const int copied = ::GetWindowTextW(
        hwnd, window.data(), static_cast<int>(window.size()));
    if (copied >= 0 && static_cast<size_t>(copied) >= selection_end) {
        pending_shorthand_selection_.emplace(
            window.data() + selection_start, selection_length);
    }
    SecureEraseVector(window);
}

void VietnameseIME::CaptureScintillaShorthandSelection(
    HWND hwnd, size_t selection_start, size_t selection_end,
    wchar_t first_char) {
    ClearPendingShorthandSelection();
    if (!hwnd || !enable_shorthand_ || IsSecureInputContext()) {
        return;
    }
    RefreshShorthandRulesIfChanged();
    if (!HasSelectionShorthandStartingWith(first_char)) {
        return;
    }
    if (selection_start > selection_end) {
        (std::swap)(selection_start, selection_end);
    }
    const size_t byte_length = selection_end - selection_start;
    if (byte_length == 0 ||
        byte_length > MAX_SHORTHAND_VALUE_CHARS * 4) {
        return;
    }

    constexpr LRESULT SCI_GETTEXTRANGE = 2162;
    struct SciCharacterRange {
        LONG_PTR cpMin;
        LONG_PTR cpMax;
    };
    struct SciTextRange {
        SciCharacterRange chrg;
        char* lpstrText;
    };
    std::vector<char> bytes(byte_length + 1, '\0');
    SciTextRange text_range{
        {static_cast<LONG_PTR>(selection_start),
         static_cast<LONG_PTR>(selection_end)},
        bytes.data()};
    const LRESULT copied = ::SendMessageW(
        hwnd, SCI_GETTEXTRANGE, 0,
        reinterpret_cast<LPARAM>(&text_range));
    if (copied >= 0 && static_cast<size_t>(copied) == byte_length) {
        std::wstring selection;
        if (ConvertUtf8ToWideBounded(
                std::string_view(bytes.data(), byte_length),
                MAX_SHORTHAND_VALUE_CHARS, selection)) {
            pending_shorthand_selection_ = std::move(selection);
        }
        SecureEraseString(selection);
    }
    SecureEraseVector(bytes);
}

void VietnameseIME::RefreshShorthandRulesIfChanged() {
    if (shorthand_file_path_.empty()) {
        shorthand_file_path_ = GetShorthandFilePath(g_hInst);
    }
    const auto observed =
        ReadShorthandFileVersion(shorthand_file_path_);
    if (!ShouldReloadShorthandFile(
            shorthand_file_version_, observed)) {
        return;
    }
    LoadShorthandRules();
}

void VietnameseIME::LoadShorthandRules() {
    shorthand_file_path_ = GetShorthandFilePath(g_hInst);
    if (shorthand_file_path_.empty()) {
        shorthand_map_.clear();
        shorthand_file_version_.reset();
        return;
    }

    const auto observed =
        ReadShorthandFileVersion(shorthand_file_path_);
    const IMEConfig config = LoadConfigFromRegistry();
    if (!config.enable_shorthand) {
        shorthand_map_.clear();
        shorthand_file_version_ = observed;
        return;
    }

    std::wstring utf16_content;
    if (!ReadUtf8TextFile(shorthand_file_path_, utf16_content)) {
        if (observed && !observed->exists) {
            shorthand_map_.clear();
            shorthand_file_version_ = observed;
        }
        logger::Log(
            logger::Level::Warning,
            L"Shorthand file not found or cannot be opened");
        return;
    }

    ShorthandParseResult parsed = ParseShorthandRules(utf16_content);
    SecureEraseString(utf16_content);
    std::unordered_map<std::wstring, std::wstring> updated_rules;
    updated_rules.reserve(parsed.rules.size());
    for (const auto& rule : parsed.rules) {
        updated_rules[rule.key] = rule.value;
    }
    shorthand_map_.swap(updated_rules);

    // Keep the pre-read version. If the file changes during the read, the
    // next commit boundary observes a different version and reloads again.
    shorthand_file_version_ = observed;

    logger::LogFormat(
        logger::Level::Info,
        L"Loaded %zu shorthand rules, invalid_lines = %zu, duplicate_lines = %zu, limit_exceeded_lines = %zu",
        shorthand_map_.size(), parsed.invalid_lines,
        parsed.duplicate_lines, parsed.limit_exceeded_lines);
}

void VietnameseIME::CheckAndReloadConfig() {
    if (config_changed_.exchange(false)) {
        ReloadConfig();
    }
}

DWORD WINAPI VietnameseIME::RegistryWatchThreadProc(LPVOID lpParam) {
    logger::Log(logger::Level::Info, L"RegistryWatchThreadProc started");
    VietnameseIME* pThis = reinterpret_cast<VietnameseIME*>(lpParam);
    if (!pThis) return 0;

    HKEY hKey = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, REG_KEY_PATH, 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_READ, nullptr, &hKey, nullptr) != ERROR_SUCCESS) {
        logger::Log(logger::Level::Error, L"RegistryWatchThreadProc: Failed to open/create Registry key for watching");
        pThis->Release();
        return 0;
    }

    HANDLE waitHandles[2] = { pThis->registry_shutdown_event_, pThis->registry_watch_event_ };

    while (true) {
        LONG status = RegNotifyChangeKeyValue(hKey, FALSE, REG_NOTIFY_CHANGE_LAST_SET, pThis->registry_watch_event_, TRUE);
        if (status != ERROR_SUCCESS) {
            logger::LogFormat(logger::Level::Error, L"RegNotifyChangeKeyValue failed: %d", status);
            break;
        }

        DWORD waitResult = WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE);
        if (waitResult == WAIT_OBJECT_0) {
            logger::Log(logger::Level::Info, L"RegistryWatchThreadProc: Shutdown event signaled");
            break;
        } else if (waitResult == WAIT_OBJECT_0 + 1) {
            logger::Log(logger::Level::Info, L"RegistryWatchThreadProc: Registry change detected");
            pThis->config_changed_ = true;
        } else {
            logger::LogFormat(logger::Level::Error, L"WaitForMultipleObjects failed or returned unexpected result: %u", waitResult);
            break;
        }
    }

    if (hKey) {
        RegCloseKey(hKey);
    }
    logger::Log(logger::Level::Info, L"RegistryWatchThreadProc exiting");
    pThis->Release();
    return 0;
}

LRESULT CALLBACK VietnameseIME::MouseHookSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    if (uIdSubclass == 0x2026 || uIdSubclass == 0x2027) {
        auto* ime = reinterpret_cast<VietnameseIME*>(dwRefData);
        if (ime) {
            bool trigger_commit = false;
            const wchar_t* msg_name = L"";

            if (uMsg == WM_LBUTTONDOWN) { msg_name = L"WM_LBUTTONDOWN"; trigger_commit = true; }
            else if (uMsg == WM_RBUTTONDOWN) { msg_name = L"WM_RBUTTONDOWN"; trigger_commit = true; }
            else if (uMsg == WM_MBUTTONDOWN) { msg_name = L"WM_MBUTTONDOWN"; trigger_commit = true; }
            else if (uMsg == 0x0246) { msg_name = L"WM_POINTERDOWN"; trigger_commit = true; }
            else if (uMsg == WM_NCLBUTTONDOWN) { msg_name = L"WM_NCLBUTTONDOWN"; trigger_commit = true; }
            else if (uMsg == WM_NCRBUTTONDOWN) { msg_name = L"WM_NCRBUTTONDOWN"; trigger_commit = true; }
            else if (uMsg == WM_KILLFOCUS) { msg_name = L"WM_KILLFOCUS"; trigger_commit = true; }
            else if (uMsg == WM_MOUSEACTIVATE) { msg_name = L"WM_MOUSEACTIVATE"; trigger_commit = true; }
            else if (uMsg == WM_LBUTTONUP) { msg_name = L"WM_LBUTTONUP"; trigger_commit = true; }
            else if (uMsg == 0x0247) { msg_name = L"WM_POINTERUP"; trigger_commit = true; }
            else if (uMsg == WM_NCLBUTTONUP) { msg_name = L"WM_NCLBUTTONUP"; trigger_commit = true; }

            if (trigger_commit) {
                logger::LogFormat(logger::Level::Info, L"MouseHookSubclassProc: Message %s observed", msg_name);
                if (ime->IsExcelApp()) {
                    ime->ResetExcelFormulaSession(L"mouse_click_hook");
                }
                if (!ime->IsBrowserProcess()) {
                    ComPtr<ITfDocumentMgr> doc_mgr;
                    if (SUCCEEDED(ime->thread_mgr_->GetFocus(doc_mgr.GetAddressOf())) && doc_mgr) {
                        ComPtr<ITfContext> context;
                        if (SUCCEEDED(doc_mgr->GetTop(context.GetAddressOf())) && context) {
                            ime->pending_commit_caret_policy_ = CommitCaretPolicy::PreserveHostSelection;
                            ime->CommitCompositionSync(context.Get());
                        }
                    }
                } else {
                    logger::Log(logger::Level::Info, L"MouseHookSubclassProc: Browser process detected, skipping forced commit");
                }
            }
        }
    }
    return ::DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

void VietnameseIME::CommitActiveCompositionFromHook() {
    if (IsBrowserProcess()) return;
    if (!active_composition_) return;
    ComPtr<ITfDocumentMgr> doc_mgr;
    if (SUCCEEDED(thread_mgr_->GetFocus(doc_mgr.GetAddressOf())) && doc_mgr) {
        ComPtr<ITfContext> context;
        if (SUCCEEDED(doc_mgr->GetTop(context.GetAddressOf())) && context) {
            pending_commit_caret_policy_ = CommitCaretPolicy::PreserveHostSelection;
            CommitCompositionSync(context.Get());
        }
    }
}

bool VietnameseIME::ScheduleTelegramCommittedWordResume(
    ITfContext* pic, UINT delay_ms) noexcept {
    if (!pic || delay_ms == 0 ||
        telegram_boundary_resume_state_.phase !=
            TelegramBoundaryResumePhase::TimerScheduled ||
        telegram_boundary_resume_timer_id_ != 0 ||
        g_telegram_resume_timer.owner != nullptr) {
        return false;
    }

    const UINT_PTR timer_id = ::SetTimer(
        nullptr, 0, delay_ms,
        TelegramCommittedWordResumeTimerProc);
    if (timer_id == 0) {
        return false;
    }

    AddRef();
    telegram_boundary_resume_context_ = ComPtr<ITfContext>(pic);
    telegram_boundary_resume_timer_id_ = timer_id;
    telegram_boundary_resume_thread_id_ = ::GetCurrentThreadId();
    g_telegram_resume_timer = {timer_id, this};
    return true;
}

bool VietnameseIME::CancelTelegramCommittedWordResumeTimer() noexcept {
    telegram_synthetic_selection_suppression_.Clear();
    if (telegram_boundary_resume_timer_id_ == 0) {
        return false;
    }

    if (telegram_boundary_resume_thread_id_ != ::GetCurrentThreadId()) {
        logger::Log(logger::Level::Warning,
                    L"Telegram resume timer cancel deferred: thread mismatch");
        return false;
    }

    const UINT_PTR timer_id = telegram_boundary_resume_timer_id_;
    ::KillTimer(nullptr, timer_id);
    telegram_boundary_resume_timer_id_ = 0;
    telegram_boundary_resume_thread_id_ = 0;
    if (g_telegram_resume_timer.timer_id == timer_id &&
        g_telegram_resume_timer.owner == this) {
        g_telegram_resume_timer = {};
    }
    return true;
}

VOID CALLBACK VietnameseIME::TelegramCommittedWordResumeTimerProc(
    [[maybe_unused]] HWND hwnd,
    [[maybe_unused]] UINT message,
    UINT_PTR timer_id,
    [[maybe_unused]] DWORD time) {
    ::KillTimer(nullptr, timer_id);

    const TelegramResumeTimerRegistration registration =
        g_telegram_resume_timer;
    if (registration.timer_id != timer_id || !registration.owner) {
        return;
    }
    g_telegram_resume_timer = {};

    VietnameseIME* const ime = registration.owner;
    ime->telegram_boundary_resume_timer_id_ = 0;
    ime->telegram_boundary_resume_thread_id_ = 0;
    ComPtr<ITfContext> context = ime->telegram_boundary_resume_context_;
    const bool requested = ime->RequestTelegramCommittedWordResume(context.Get());
    logger::LogFormat(
        requested ? logger::Level::Info : logger::Level::Warning,
        L"Telegram resume timer fired: requested=%d", requested ? 1 : 0);
    ime->Release();
}

bool VietnameseIME::ScheduleTelegramRawReplay(
    ITfContext* pic,
    std::vector<TelegramRawReplayKey>&& plan,
    bool caps_lock_on) noexcept {
    const ULONGLONG now = GetTickCount64();
    if (!pic || plan.empty() ||
        telegram_raw_replay_timer_id_ != 0 ||
        g_telegram_raw_replay_timer.owner != nullptr ||
        !telegram_raw_replay_state_.Begin(
            plan.size(), now, core::kMaxRawKeysPerComposition)) {
        SecureEraseTelegramRawReplayPlan(plan);
        return false;
    }

    const UINT_PTR timer_id = ::SetTimer(
        nullptr, 0, kTelegramRawReplayDelayMs,
        TelegramRawReplayTimerProc);
    if (timer_id == 0) {
        telegram_raw_replay_state_.Cancel();
        SecureEraseTelegramRawReplayPlan(plan);
        return false;
    }

    AddRef();
    SecureEraseTelegramRawReplayPlan(telegram_raw_replay_plan_);
    telegram_raw_replay_plan_ = std::move(plan);
    telegram_raw_replay_context_ = ComPtr<ITfContext>(pic);
    telegram_raw_replay_timer_id_ = timer_id;
    telegram_raw_replay_thread_id_ = ::GetCurrentThreadId();
    telegram_raw_replay_foreground_ = ::GetForegroundWindow();
    telegram_raw_replay_method_ = engine_.GetInputMethod();
    telegram_raw_replay_caps_lock_on_ = caps_lock_on;
    g_telegram_raw_replay_timer = {timer_id, this};
    return true;
}

bool VietnameseIME::CancelTelegramRawReplayTimer() noexcept {
    if (telegram_raw_replay_timer_id_ == 0) {
        return false;
    }
    if (telegram_raw_replay_thread_id_ != ::GetCurrentThreadId()) {
        logger::Log(
            logger::Level::Warning,
            L"Telegram raw replay timer cancel deferred: thread mismatch");
        return false;
    }

    const UINT_PTR timer_id = telegram_raw_replay_timer_id_;
    ::KillTimer(nullptr, timer_id);
    telegram_raw_replay_timer_id_ = 0;
    telegram_raw_replay_thread_id_ = 0;
    if (g_telegram_raw_replay_timer.timer_id == timer_id &&
        g_telegram_raw_replay_timer.owner == this) {
        g_telegram_raw_replay_timer = {};
    }
    return true;
}

void VietnameseIME::ClearTelegramRawReplay() noexcept {
    const bool release_timer_reference = CancelTelegramRawReplayTimer();
    telegram_raw_replay_state_.Cancel();
    SecureEraseTelegramRawReplayPlan(telegram_raw_replay_plan_);
    telegram_raw_replay_context_.Reset();
    telegram_raw_replay_foreground_ = nullptr;
    telegram_raw_replay_method_ = core::InputMethod::Telex;
    telegram_raw_replay_caps_lock_on_ = false;
    if (release_timer_reference) {
        Release();
    }
}

bool VietnameseIME::SendTelegramRawReplayKey(
    const TelegramRawReplayKey& key) noexcept {
    if (key.virtual_key == 0) {
        return false;
    }

    INPUT inputs[4]{};
    UINT input_count = 0;
    const auto append_key = [&](WORD virtual_key, DWORD flags) {
        INPUT& input = inputs[input_count++];
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = virtual_key;
        input.ki.wScan = static_cast<WORD>(
            ::MapVirtualKeyW(virtual_key, MAPVK_VK_TO_VSC));
        input.ki.dwFlags = flags;
        input.ki.dwExtraInfo = kTelegramRawReplayMarker;
    };

    if (key.shift_down) {
        append_key(VK_SHIFT, 0);
    }
    append_key(key.virtual_key, 0);
    append_key(key.virtual_key, KEYEVENTF_KEYUP);
    if (key.shift_down) {
        append_key(VK_SHIFT, KEYEVENTF_KEYUP);
    }

    const UINT sent = ::SendInput(input_count, inputs, sizeof(INPUT));
    const auto decision = DecideTelegramRawReplaySend(key, sent);
    if (!decision.complete) {
        INPUT cleanup[2]{};
        UINT cleanup_count = 0;
        if (decision.cleanup_key_up) {
            cleanup[cleanup_count].type = INPUT_KEYBOARD;
            cleanup[cleanup_count].ki.wVk = key.virtual_key;
            cleanup[cleanup_count].ki.wScan = static_cast<WORD>(
                ::MapVirtualKeyW(key.virtual_key, MAPVK_VK_TO_VSC));
            cleanup[cleanup_count].ki.dwFlags = KEYEVENTF_KEYUP;
            cleanup[cleanup_count].ki.dwExtraInfo =
                kTelegramRawReplayMarker;
            ++cleanup_count;
        }
        if (decision.cleanup_shift_up) {
            cleanup[cleanup_count].type = INPUT_KEYBOARD;
            cleanup[cleanup_count].ki.wVk = VK_SHIFT;
            cleanup[cleanup_count].ki.wScan = static_cast<WORD>(
                ::MapVirtualKeyW(VK_SHIFT, MAPVK_VK_TO_VSC));
            cleanup[cleanup_count].ki.dwFlags = KEYEVENTF_KEYUP;
            cleanup[cleanup_count].ki.dwExtraInfo =
                kTelegramRawReplayMarker;
            ++cleanup_count;
        }
        if (cleanup_count != 0) {
            ::SendInput(cleanup_count, cleanup, sizeof(INPUT));
        }
        SecureZeroMemory(cleanup, sizeof(cleanup));
    }

    SecureZeroMemory(inputs, sizeof(inputs));
    return decision.complete;
}

bool VietnameseIME::DispatchTelegramRawReplay() noexcept {
    const ULONGLONG now = GetTickCount64();
    const bool valid =
        telegram_raw_replay_state_.MarkDispatching(now) &&
        !telegram_raw_replay_plan_.empty() &&
        telegram_raw_replay_context_ &&
        last_commit_undo_ && last_commit_undo_->is_tsf &&
        last_commit_undo_->committed_with_ascii_space &&
        last_commit_undo_->committed_text_range &&
        last_commit_undo_->method == telegram_raw_replay_method_ &&
        telegram_boundary_resume_state_.phase ==
            TelegramBoundaryResumePhase::TimerScheduled &&
        IsCommitUndoRestoreWindowValid(
            now, last_commit_undo_->committed_tick) &&
        IsTelegramProcess() && !HasActiveComposition() &&
        !IsSecureInputContext() && typing_mode_ == 0 &&
        engine_.GetInputMethod() == telegram_raw_replay_method_ &&
        ::GetForegroundWindow() == telegram_raw_replay_foreground_;
    if (!valid) {
        logger::Log(
            logger::Level::Warning,
            L"Telegram raw replay rejected by dispatch preconditions");
        engine_.SecureClear();
        ClearLastCommitUndo();
        ClearTelegramRawReplay();
        return false;
    }

    ComPtr<ITfDocumentMgr> document_mgr;
    ComPtr<ITfContext> current_context;
    const bool same_context = thread_mgr_ &&
        SUCCEEDED(thread_mgr_->GetFocus(document_mgr.GetAddressOf())) &&
        document_mgr &&
        SUCCEEDED(document_mgr->GetTop(current_context.GetAddressOf())) &&
        current_context &&
        IsSameComObject(
            current_context.Get(), telegram_raw_replay_context_.Get());
    if (!same_context || IsCurrentAppBlocked(current_context.Get())) {
        logger::Log(
            logger::Level::Warning,
            L"Telegram raw replay rejected by context verification");
        engine_.SecureClear();
        ClearLastCommitUndo();
        ClearTelegramRawReplay();
        return false;
    }

    engine_.SecureClear();
    ClearLastCommitUndo();

    bool sent_all = true;
    size_t sent_keys = 0;
    for (const TelegramRawReplayKey& key : telegram_raw_replay_plan_) {
        if (!SendTelegramRawReplayKey(key)) {
            sent_all = false;
            break;
        }
        ++sent_keys;
    }

    const bool complete = sent_all &&
        sent_keys == telegram_raw_replay_plan_.size() &&
        telegram_raw_replay_state_.Complete();
    logger::LogFormat(
        complete ? logger::Level::Info : logger::Level::Warning,
        L"Telegram raw replay dispatched: complete=%d, sent_keys=%zu, total_keys=%zu, caps=%d",
        complete ? 1 : 0, sent_keys,
        telegram_raw_replay_plan_.size(),
        telegram_raw_replay_caps_lock_on_ ? 1 : 0);
    ClearTelegramRawReplay();
    return complete;
}

VOID CALLBACK VietnameseIME::TelegramRawReplayTimerProc(
    [[maybe_unused]] HWND hwnd,
    [[maybe_unused]] UINT message,
    UINT_PTR timer_id,
    [[maybe_unused]] DWORD time) {
    ::KillTimer(nullptr, timer_id);

    const TelegramResumeTimerRegistration registration =
        g_telegram_raw_replay_timer;
    if (registration.timer_id != timer_id || !registration.owner) {
        return;
    }
    g_telegram_raw_replay_timer = {};

    VietnameseIME* const ime = registration.owner;
    ime->telegram_raw_replay_timer_id_ = 0;
    ime->telegram_raw_replay_thread_id_ = 0;
    const bool dispatched = ime->DispatchTelegramRawReplay();
    logger::LogFormat(
        dispatched ? logger::Level::Info : logger::Level::Warning,
        L"Telegram raw replay timer fired: dispatched=%d",
        dispatched ? 1 : 0);
    ime->Release();
}

bool VietnameseIME::RequestTelegramCommittedWordResume(ITfContext* pic) {
    if (telegram_boundary_resume_state_.phase ==
        TelegramBoundaryResumePhase::ResumeRequested) {
        return true;
    }

    const bool valid = pic &&
        telegram_boundary_resume_state_.phase ==
            TelegramBoundaryResumePhase::TimerScheduled &&
        last_commit_undo_ && last_commit_undo_->is_tsf &&
        last_commit_undo_->committed_with_ascii_space &&
        last_commit_undo_->committed_text_range &&
        last_commit_undo_->method == engine_.GetInputMethod() &&
        IsTelegramProcess() && !HasActiveComposition() &&
        IsCommitUndoRestoreWindowValid(
            GetTickCount64(), last_commit_undo_->committed_tick) &&
        IsSameComObject(pic, last_commit_undo_->expected_context.Get()) &&
        IsSameComObject(pic, telegram_boundary_resume_context_.Get());
    if (!valid || !telegram_boundary_resume_state_.MarkResumeRequested()) {
        logger::LogFormat(
            logger::Level::Warning,
            L"Telegram resume request rejected: valid=%d, phase=%d, entry=%d, active=%d",
            valid ? 1 : 0,
            static_cast<int>(telegram_boundary_resume_state_.phase),
            last_commit_undo_.has_value() ? 1 : 0,
            HasActiveComposition() ? 1 : 0);
        if (!HasActiveComposition()) {
            engine_.SecureClear();
        }
        ClearLastCommitUndo();
        return false;
    }

    ComPtr<EditSession> session;
    session.Attach(new (std::nothrow) EditSession(
        this, pic, EditAction::ResumeTelegramCommittedWord));
    if (!session) {
        engine_.SecureClear();
        ClearLastCommitUndo();
        return false;
    }

    HRESULT hrSession = E_FAIL;
    const HRESULT hrRequest = pic->RequestEditSession(
        client_id_, session.Get(), TF_ES_ASYNC | TF_ES_READWRITE, &hrSession);
    const bool requested = SUCCEEDED(hrRequest) && SUCCEEDED(hrSession);
    logger::LogFormat(
        requested ? logger::Level::Info : logger::Level::Warning,
        L"Telegram resume edit session request: request_hr=0x%08X, session_hr=0x%08X, requested=%d",
        hrRequest, hrSession, requested ? 1 : 0);
    if (!requested) {
        engine_.SecureClear();
        ClearLastCommitUndo();
    }
    return requested;
}

bool VietnameseIME::CancelTelegramNativeSelectionForRealKey(
    ITfContext* pic) {
    const bool valid = pic && telegram_boundary_resume_state_.IsPending() &&
        last_commit_undo_ &&
        IsSameComObject(pic, last_commit_undo_->expected_context.Get()) &&
        IsSameComObject(pic, telegram_boundary_resume_context_.Get());
    if (!valid) {
        if (!HasActiveComposition()) {
            engine_.SecureClear();
        }
        ClearLastCommitUndo();
        return false;
    }

    ComPtr<EditSession> session;
    session.Attach(new (std::nothrow) EditSession(
        this, pic, EditAction::CancelTelegramNativeSelection));
    if (!session) {
        engine_.SecureClear();
        ClearLastCommitUndo();
        return false;
    }

    HRESULT hrSession = E_FAIL;
    const HRESULT hrRequest = pic->RequestEditSession(
        client_id_, session.Get(), TF_ES_SYNC | TF_ES_READWRITE, &hrSession);
    const bool canceled_safely = SUCCEEDED(hrRequest) &&
        SUCCEEDED(hrSession) && session->action_succeeded();
    logger::LogFormat(
        canceled_safely ? logger::Level::Info : logger::Level::Warning,
        L"Telegram pending native selection cancel: request_hr=0x%08X, session_hr=0x%08X, safe=%d",
        hrRequest, hrSession, canceled_safely ? 1 : 0);
    if (!HasActiveComposition()) {
        engine_.SecureClear();
    }
    ClearLastCommitUndo();
    return canceled_safely;
}

bool VietnameseIME::CollapseTelegramNativeSelection(
    TfEditCookie ec, ITfContext* pic) {
    TF_SELECTION selection{};
    ULONG fetched = 0;
    const HRESULT hrSelection = pic
        ? pic->GetSelection(
              ec, TF_DEFAULT_SELECTION, 1, &selection, &fetched)
        : E_POINTER;
    ComPtr<ITfRange> selection_range;
    if (selection.range) {
        selection_range.Attach(selection.range);
    }

    BOOL empty = TRUE;
    const HRESULT hrEmpty = selection_range
        ? selection_range->IsEmpty(ec, &empty)
        : E_POINTER;
    HRESULT hrCollapse = S_OK;
    HRESULT hrSetSelection = S_OK;
    if (SUCCEEDED(hrSelection) && fetched == 1 && selection_range &&
        SUCCEEDED(hrEmpty) && !empty) {
        hrCollapse = selection_range->Collapse(ec, TF_ANCHOR_END);
        if (SUCCEEDED(hrCollapse)) {
            TF_SELECTION collapsed_selection{};
            collapsed_selection.range = selection_range.Get();
            collapsed_selection.style.ase = TF_AE_NONE;
            collapsed_selection.style.fInterimChar = FALSE;
            hrSetSelection = pic->SetSelection(
                ec, 1, &collapsed_selection);
        }
    }
    const bool safe = SUCCEEDED(hrSelection) && fetched == 1 &&
        selection_range && SUCCEEDED(hrEmpty) &&
        (empty || (SUCCEEDED(hrCollapse) && SUCCEEDED(hrSetSelection)));
    logger::LogFormat(
        safe ? logger::Level::Info : logger::Level::Warning,
        L"Telegram pending selection collapse: selection_hr=0x%08X, fetched=%u, empty_hr=0x%08X, empty=%d, collapse_hr=0x%08X, set_hr=0x%08X, safe=%d",
        hrSelection, fetched, hrEmpty, empty ? 1 : 0,
        hrCollapse, hrSetSelection, safe ? 1 : 0);
    return safe;
}

bool VietnameseIME::ResumeTelegramCommittedWord(
    TfEditCookie ec, ITfContext* pic) {
    const bool preconditions = pic && last_commit_undo_ &&
        telegram_boundary_resume_state_.phase ==
            TelegramBoundaryResumePhase::ResumeRequested &&
        last_commit_undo_->is_tsf &&
        last_commit_undo_->committed_with_ascii_space &&
        last_commit_undo_->committed_text_range &&
        last_commit_undo_->method == engine_.GetInputMethod() &&
        IsTelegramProcess() && !HasActiveComposition() &&
        !IsSecureInputContext() &&
        IsCommitUndoRestoreWindowValid(
            GetTickCount64(), last_commit_undo_->committed_tick) &&
        IsSameComObject(pic, last_commit_undo_->expected_context.Get()) &&
        IsSameComObject(pic, telegram_boundary_resume_context_.Get());
    if (!preconditions) {
        logger::LogFormat(
            logger::Level::Warning,
            L"Telegram resume verification failed: stage=1, entry=%d, phase=%d, active=%d",
            last_commit_undo_.has_value() ? 1 : 0,
            static_cast<int>(telegram_boundary_resume_state_.phase),
            HasActiveComposition() ? 1 : 0);
        if (!HasActiveComposition()) {
            engine_.SecureClear();
        }
        ClearLastCommitUndo();
        return false;
    }

    const size_t display_length = last_commit_undo_->display_text.length();
    const size_t raw_length = last_commit_undo_->raw_keys.length();

    TF_SELECTION current_selection{};
    ULONG selection_fetched = 0;
    const HRESULT hrSelection = pic->GetSelection(
        ec, TF_DEFAULT_SELECTION, 1, &current_selection, &selection_fetched);
    ComPtr<ITfRange> transaction_range;
    if (current_selection.range) {
        transaction_range.Attach(current_selection.range);
    }

    BOOL selection_empty = TRUE;
    const HRESULT hrEmpty = transaction_range
        ? transaction_range->IsEmpty(ec, &selection_empty)
        : E_POINTER;
    const bool selection_ready_empty = SUCCEEDED(hrSelection) &&
        selection_fetched == 1 && transaction_range &&
        SUCCEEDED(hrEmpty) && selection_empty;
    const bool selection_nonempty = SUCCEEDED(hrSelection) &&
        selection_fetched == 1 && transaction_range &&
        SUCCEEDED(hrEmpty) && !selection_empty;
    if (selection_nonempty) {
        telegram_synthetic_selection_suppression_.Clear();
    }

    HRESULT hrRead = E_FAIL;
    ULONG selected_fetched = 0;
    std::wstring selected_text;
    if (selection_nonempty &&
        display_length <= core::kMaxRawKeysPerComposition &&
        display_length < static_cast<size_t>((std::numeric_limits<ULONG>::max)())) {
        selected_text.assign(display_length + 1, L'\0');
        hrRead = transaction_range->GetText(
            ec, 0, selected_text.data(),
            static_cast<ULONG>(selected_text.size()), &selected_fetched);
    }
    const bool selection_matches = SUCCEEDED(hrRead) &&
        IsVerifiedTelegramNativeSelection(
            std::wstring_view(selected_text.data(), selected_fetched),
            last_commit_undo_->display_text, selection_nonempty,
            core::kMaxRawKeysPerComposition);

    engine_.Clear();
    if (selection_matches) {
        for (wchar_t key : last_commit_undo_->raw_keys) {
            engine_.ProcessKey(key);
        }
    }
    std::wstring resume_display = engine_.GetDisplayString();
    const bool replay_matches = selection_matches &&
        resume_display == last_commit_undo_->display_text;
    const bool verification_succeeded = replay_matches &&
        telegram_boundary_resume_state_.MarkSelectionVerified();

    logger::LogFormat(
        verification_succeeded ? logger::Level::Info : logger::Level::Warning,
        L"Telegram native selection verification: selection_hr=0x%08X, fetched=%u, "
        L"empty_hr=0x%08X, empty=%d, read_hr=0x%08X, text_fetched=%u, "
        L"selection_match=%d, replay_match=%d, phase=%d, display_len=%zu, raw_len=%zu",
        hrSelection, selection_fetched, hrEmpty, selection_empty ? 1 : 0,
        hrRead, selected_fetched, selection_matches ? 1 : 0,
        replay_matches ? 1 : 0,
        static_cast<int>(telegram_boundary_resume_state_.phase),
        display_length, raw_length);

    if (!verification_succeeded && selection_ready_empty) {
        const ULONGLONG now = GetTickCount64();
        const unsigned probe_attempt =
            telegram_boundary_resume_state_.selection_probe_attempts;
        const ULONGLONG elapsed_ms =
            now >= telegram_boundary_resume_state_.started_tick
                ? now - telegram_boundary_resume_state_.started_tick
                : (std::numeric_limits<ULONGLONG>::max)();
        const bool retry_state =
            telegram_boundary_resume_state_.MarkSelectionRetryScheduled(now);
        const bool rescheduled = retry_state &&
            ScheduleTelegramCommittedWordResume(
                pic, kTelegramSelectionRetryDelayMs);
        logger::LogFormat(
            rescheduled ? logger::Level::Info : logger::Level::Warning,
            L"Telegram native selection readiness: empty=1, attempt=%u, elapsed_ms=%llu, retry_state=%d, rescheduled=%d",
            probe_attempt, elapsed_ms,
            retry_state ? 1 : 0, rescheduled ? 1 : 0);
        SecureEraseString(selected_text);
        SecureEraseString(resume_display);
        engine_.SecureClear();
        if (rescheduled) {
            return false;
        }

        const bool collapsed = CollapseTelegramNativeSelection(ec, pic);
        logger::LogFormat(
            collapsed ? logger::Level::Info : logger::Level::Warning,
            L"Telegram native selection readiness exhausted: collapsed=%d, attempt=%u",
            collapsed ? 1 : 0, probe_attempt);
        ClearLastCommitUndo();
        return false;
    }

    if (!verification_succeeded) {
        const bool collapse_sent = selection_nonempty &&
            SendTelegramSelectionCollapseRight();
        logger::LogFormat(
            logger::Level::Warning,
            L"Telegram native selection rejected: nonempty=%d, collapse_sent=%d",
            selection_nonempty ? 1 : 0, collapse_sent ? 1 : 0);
        SecureEraseString(selected_text);
        SecureEraseString(resume_display);
        engine_.SecureClear();
        ClearLastCommitUndo();
        return false;
    }

    const bool caps_lock_on =
        (::GetKeyState(VK_CAPITAL) & 0x0001) != 0;
    auto replay_plan = BuildTelegramRawReplayPlan(
        last_commit_undo_->raw_keys, caps_lock_on,
        core::kMaxRawKeysPerComposition);
    const bool replay_scheduled = replay_plan &&
        ScheduleTelegramRawReplay(
            pic, std::move(*replay_plan), caps_lock_on);

    logger::LogFormat(
        replay_scheduled ? logger::Level::Info : logger::Level::Warning,
        L"Telegram verified selection raw replay: scheduled=%d, display_len=%zu, raw_len=%zu, caps=%d",
        replay_scheduled ? 1 : 0, display_length, raw_length,
        caps_lock_on ? 1 : 0);

    SecureEraseString(selected_text);
    SecureEraseString(resume_display);
    engine_.SecureClear();
    if (!replay_scheduled) {
        SendTelegramSelectionCollapseRight();
    }
    ClearLastCommitUndo();
    return replay_scheduled;
}

void VietnameseIME::ClearLastCommitUndo() noexcept {
    const bool release_timer_reference =
        CancelTelegramCommittedWordResumeTimer();
    telegram_boundary_resume_state_.Cancel();
    telegram_boundary_resume_context_.Reset();
    if (last_commit_undo_) {
        SecureClearCommitUndoEntry(*last_commit_undo_);
    }
    last_commit_undo_.reset();
    if (release_timer_reference) {
        Release();
    }
}

bool VietnameseIME::TrySmartUndoLastCommittedCorrection(
    TfEditCookie ec, ITfContext* pic) {
    if (!last_commit_undo_ || !last_commit_undo_->is_tsf || !pic ||
        !last_commit_undo_->committed_text_range) {
        ClearLastCommitUndo();
        return false;
    }

    const HWND focus_hwnd = GetBestFocusWindow();
    const bool focus_matches = focus_hwnd != nullptr &&
        focus_hwnd == last_commit_undo_->hwnd;
    const bool context_matches = IsSameComObject(
        pic, last_commit_undo_->expected_context.Get());
    const bool safe_context = typing_mode_ == 0 &&
        !IsSecureInputContext() && !IsCurrentAppBlocked(pic) &&
        !IsBuiltInNativeBypassProcess(GetFocusedProcessName());
    if (!ShouldRouteSmartUndoBackspace(
            *last_commit_undo_, enable_smart_undo_, GetTickCount64(),
            HasActiveComposition(), true, focus_matches, context_matches,
            true, IsSecureInputContext(), safe_context)) {
        ClearLastCommitUndo();
        return false;
    }

    TF_SELECTION selection{};
    ULONG fetched_selection = 0;
    ComPtr<ITfRange> caret_range;
    const HRESULT selection_hr = pic->GetSelection(
        ec, TF_DEFAULT_SELECTION, 1, &selection, &fetched_selection);
    if (selection.range) {
        caret_range.Attach(selection.range);
    }
    BOOL selection_empty = FALSE;
    if (FAILED(selection_hr) || fetched_selection != 1 || !caret_range ||
        FAILED(caret_range->IsEmpty(ec, &selection_empty)) ||
        !selection_empty) {
        ClearLastCommitUndo();
        return false;
    }

    ComPtr<ITfRange> transaction_range;
    if (FAILED(last_commit_undo_->committed_text_range->Clone(
            transaction_range.GetAddressOf())) || !transaction_range) {
        ClearLastCommitUndo();
        return false;
    }

    LONG shifted = 0;
    if (FAILED(transaction_range->ShiftEnd(
            ec, 1, &shifted, nullptr)) || shifted != 1) {
        ClearLastCommitUndo();
        return false;
    }

    LONG caret_comparison = 1;
    if (FAILED(caret_range->CompareStart(
            ec, transaction_range.Get(), TF_ANCHOR_END,
            &caret_comparison)) || caret_comparison != 0) {
        ClearLastCommitUndo();
        return false;
    }

    const size_t expected_length =
        last_commit_undo_->display_text.length() + 1;
    if (expected_length <= 1 ||
        expected_length > kMaxCommitUndoDisplayChars + 1) {
        ClearLastCommitUndo();
        return false;
    }

    std::vector<wchar_t> current_text(expected_length + 1, L'\0');
    ULONG fetched_chars = 0;
    const HRESULT text_hr = transaction_range->GetText(
        ec, 0, current_text.data(),
        static_cast<ULONG>(expected_length + 1), &fetched_chars);
    const bool text_matches = SUCCEEDED(text_hr) &&
        fetched_chars == expected_length &&
        current_text[expected_length - 1] == L' ' &&
        std::wstring_view(current_text.data(), expected_length - 1) ==
            last_commit_undo_->display_text;
    if (!text_matches) {
        SecureEraseVector(current_text);
        ClearLastCommitUndo();
        return false;
    }

    const std::wstring& restore_text =
        last_commit_undo_->original_text.empty()
            ? last_commit_undo_->raw_keys
            : last_commit_undo_->original_text;
    const size_t restore_length = restore_text.length();
    is_updating_selection_ = true;
    const HRESULT replace_hr = transaction_range->SetText(
        ec, 0, restore_text.c_str(),
        static_cast<LONG>(restore_length));
    HRESULT set_selection_hr = E_FAIL;
    if (SUCCEEDED(replace_hr)) {
        transaction_range->Collapse(ec, TF_ANCHOR_END);
        TF_SELECTION restored_selection{};
        restored_selection.range = transaction_range.Get();
        restored_selection.style.ase = TF_AE_NONE;
        restored_selection.style.fInterimChar = FALSE;
        set_selection_hr = pic->SetSelection(
            ec, 1, &restored_selection);
    }
    is_updating_selection_ = false;

    SecureEraseVector(current_text);
    // Once SetText succeeds the physical Backspace must stay consumed; passing
    // it through would delete one more raw character even if caret placement
    // failed afterward.
    const bool restored = SUCCEEDED(replace_hr);
    logger::LogFormat(
        restored ? logger::Level::Info : logger::Level::Warning,
        L"Smart Undo TSF transaction: restored=%d, selection_set=%d, display_len=%zu, restore_len=%zu",
        restored ? 1 : 0,
        SUCCEEDED(set_selection_hr) ? 1 : 0,
        last_commit_undo_->display_text.length(), restore_length);
    engine_.SecureClear();
    ClearLastCommitUndo();
    return restored;
}

bool VietnameseIME::TrySmartUndoLastCommittedCorrectionDirectInline(
    HWND hwnd) {
    if (!last_commit_undo_ || last_commit_undo_->is_tsf || !hwnd ||
        hwnd != last_commit_undo_->hwnd || IsSecureInputContext() ||
        !enable_smart_undo_ || typing_mode_ != 0 ||
        IsBuiltInNativeBypassProcess(GetFocusedProcessName()) ||
        !ShouldCaptureSmartUndo(*last_commit_undo_) ||
        !last_commit_undo_->committed_with_ascii_space) {
        ClearLastCommitUndo();
        return false;
    }

    if (ClassNameEquals(hwnd, L"Scintilla")) {
        constexpr LRESULT SCI_GETSELECTIONSTART = 2143;
        constexpr LRESULT SCI_GETSELECTIONEND = 2145;
        constexpr LRESULT SCI_SETSEL = 2160;
        constexpr LRESULT SCI_REPLACESEL = 2170;
        constexpr LRESULT SCI_GETTEXTRANGE = 2162;
        struct SciCharacterRange {
            LONG_PTR cpMin;
            LONG_PTR cpMax;
        };
        struct SciTextRange {
            SciCharacterRange chrg;
            char* lpstrText;
        };

        const LRESULT sel_start = ::SendMessageW(
            hwnd, SCI_GETSELECTIONSTART, 0, 0);
        const LRESULT sel_end = ::SendMessageW(
            hwnd, SCI_GETSELECTIONEND, 0, 0);
        if (sel_start < 0 || sel_start != sel_end) {
            ClearLastCommitUndo();
            return false;
        }

        const size_t caret = static_cast<size_t>(sel_end);
        if (caret != last_commit_undo_->expected_caret_offset + 1) {
            ClearLastCommitUndo();
            return false;
        }

        const std::wstring& restore_text =
            last_commit_undo_->original_text.empty()
                ? last_commit_undo_->raw_keys
                : last_commit_undo_->original_text;
        std::string display_utf8;
        std::string restore_utf8;
        if (!ConvertWideToUtf8(
                last_commit_undo_->display_text, display_utf8) ||
            !ConvertWideToUtf8(restore_text, restore_utf8) ||
            display_utf8.empty() || restore_utf8.empty() ||
            caret < display_utf8.length() + 1) {
            SecureEraseStringUtf8(display_utf8);
            SecureEraseStringUtf8(restore_utf8);
            ClearLastCommitUndo();
            return false;
        }

        const size_t span_length = display_utf8.length() + 1;
        const size_t span_start = caret - span_length;
        std::vector<char> current_bytes(span_length + 1, '\0');
        SciTextRange text_range{
            {static_cast<LONG_PTR>(span_start),
             static_cast<LONG_PTR>(caret)},
            current_bytes.data()};
        ::SendMessageW(hwnd, SCI_GETTEXTRANGE, 0,
                       reinterpret_cast<LPARAM>(&text_range));
        std::string current_text(
            current_bytes.data(),
            strnlen(current_bytes.data(), span_length));
        const auto verified_span =
            FindVerifiedSmartUndoBytesBeforeCaret(
                current_text, current_text.length(), display_utf8,
                *last_commit_undo_);
        if (!verified_span || verified_span->start != 0 ||
            verified_span->end != span_length) {
            SecureEraseVector(current_bytes);
            SecureEraseStringUtf8(current_text);
            SecureEraseStringUtf8(display_utf8);
            SecureEraseStringUtf8(restore_utf8);
            ClearLastCommitUndo();
            return false;
        }

        ::SendMessageW(hwnd, SCI_SETSEL, span_start, caret);
        ::SendMessageW(hwnd, SCI_REPLACESEL, 0,
                       reinterpret_cast<LPARAM>(restore_utf8.c_str()));
        logger::LogFormat(
            logger::Level::Info,
            L"Smart Undo Scintilla transaction: display_bytes=%zu, restore_bytes=%zu",
            display_utf8.length(), restore_utf8.length());
        SecureEraseVector(current_bytes);
        SecureEraseStringUtf8(current_text);
        SecureEraseStringUtf8(display_utf8);
        SecureEraseStringUtf8(restore_utf8);
        ResetDirectInlineState();
        ClearLastCommitUndo();
        return true;
    }

    if (ClassNameEquals(hwnd, L"Edit")) {
        DWORD sel_start = 0;
        DWORD sel_end = 0;
        ::SendMessageW(hwnd, EM_GETSEL,
                       reinterpret_cast<WPARAM>(&sel_start),
                       reinterpret_cast<LPARAM>(&sel_end));
        const size_t caret = static_cast<size_t>(sel_end);
        if (sel_start != sel_end ||
            caret != last_commit_undo_->expected_caret_offset + 1) {
            ClearLastCommitUndo();
            return false;
        }

        if (caret == 0 || caret > kMaxCommitUndoDisplayChars + 1) {
            ClearLastCommitUndo();
            return false;
        }

        // Standard Edit controls do not expose a bounded text-range read.
        // Read only the prefix through the verified caret and fail closed for
        // unusually large fields instead of copying the whole document.
        std::vector<wchar_t> text_buffer(caret + 1, L'\0');
        const int copied = ::GetWindowTextW(
            hwnd, text_buffer.data(),
            static_cast<int>(text_buffer.size()));
        if (copied < 0 || static_cast<size_t>(copied) < caret) {
            SecureEraseVector(text_buffer);
            ClearLastCommitUndo();
            return false;
        }
        std::wstring current_text(
            text_buffer.data(),
            static_cast<size_t>(copied));
        const auto verified_span = FindVerifiedSmartUndoTextBeforeCaret(
            current_text, caret, *last_commit_undo_);
        if (!verified_span) {
            SecureEraseVector(text_buffer);
            SecureEraseString(current_text);
            ClearLastCommitUndo();
            return false;
        }

        ::SendMessageW(hwnd, EM_SETSEL,
                       verified_span->start, verified_span->end);
        const std::wstring& restore_text =
            last_commit_undo_->original_text.empty()
                ? last_commit_undo_->raw_keys
                : last_commit_undo_->original_text;
        ::SendMessageW(
            hwnd, EM_REPLACESEL, TRUE,
            reinterpret_cast<LPARAM>(restore_text.c_str()));
        logger::LogFormat(
            logger::Level::Info,
            L"Smart Undo Edit transaction: display_len=%zu, restore_len=%zu",
            last_commit_undo_->display_text.length(),
            restore_text.length());
        SecureEraseVector(text_buffer);
        SecureEraseString(current_text);
        ResetDirectInlineState();
        ClearLastCommitUndo();
        return true;
    }

    ClearLastCommitUndo();
    return false;
}

void VietnameseIME::CaptureCommitUndo(
    TfEditCookie ec, ITfContext* pic,
    CommitUndoEntry::TransformKind transform_kind,
    std::wstring_view original_text,
    std::wstring_view display_override,
    ITfRange* committed_range_override) {
    if (!active_composition_) return;

    ComPtr<ITfRange> comp_range;
    if (committed_range_override) {
        if (FAILED(committed_range_override->Clone(
                comp_range.GetAddressOf())) || !comp_range) {
            return;
        }
    } else {
        if (FAILED(active_composition_->GetRange(
                comp_range.GetAddressOf())) || !comp_range) {
            return;
        }
    }

    std::vector<wchar_t> buf(
        kMaxCommitUndoDisplayChars + 2, L'\0');
    ULONG fetched_chars = 0;
    comp_range->GetText(
        ec, 0, buf.data(),
        static_cast<ULONG>(kMaxCommitUndoDisplayChars + 1),
        &fetched_chars);
    std::wstring display(buf.data(), fetched_chars);
    if (!display_override.empty() && display != display_override) {
        SecureEraseString(display);
        SecureEraseVector(buf);
        return;
    }
    std::wstring raw = engine_.GetRawString();
    core::EngineDisplayResult engine_display = engine_.GetDisplayResult();
    if (transform_kind == CommitUndoEntry::TransformKind::None &&
        engine_display.HasSpellerCorrection()) {
        transform_kind = CommitUndoEntry::TransformKind::SpellerCorrection;
    }

    const std::wstring_view restore_text = original_text.empty()
        ? std::wstring_view(raw)
        : original_text;
    if (!ShouldCaptureCommitUndo(restore_text, display)) {
        SecureEraseString(engine_display.text);
        SecureEraseString(raw);
        SecureEraseString(display);
        SecureEraseVector(buf);
        return;
    }

    CommitUndoEntry entry;
    entry.raw_keys = raw;
    entry.original_text.assign(original_text);
    entry.display_text = display;
    entry.method = engine_.GetInputMethod();
    entry.transform_kind = transform_kind;
    entry.committed_tick = GetTickCount64();
    entry.hwnd = GetBestFocusWindow();
    entry.expected_context = ComPtr<ITfContext>(pic);
    entry.is_tsf = true;

    ComPtr<ITfRange> committed_text_range;
    HRESULT hrCommittedRange = comp_range->Clone(committed_text_range.GetAddressOf());
    HRESULT hrCommittedGravity = E_FAIL;
    if (SUCCEEDED(hrCommittedRange) && committed_text_range) {
        hrCommittedGravity = committed_text_range->SetGravity(
            ec, TF_GRAVITY_FORWARD, TF_GRAVITY_BACKWARD);
        if (SUCCEEDED(hrCommittedGravity)) {
            entry.committed_text_range = committed_text_range;
        } else {
            committed_text_range.Reset();
        }
    }

    ComPtr<ITfRange> caret_range;
    if (SUCCEEDED(comp_range->Clone(caret_range.GetAddressOf())) && caret_range) {
        caret_range->Collapse(ec, TF_ANCHOR_END);
        entry.expected_caret_range = caret_range;
    }

    ClearLastCommitUndo();
    last_commit_undo_ = entry;
    logger::LogFormat(logger::Level::Info,
                      L"CaptureCommitUndo (TSF): raw_len=%zu, display_len=%zu, range_hr=0x%08X, gravity_hr=0x%08X, stored_range=%d",
                      raw.length(), display.length(), hrCommittedRange,
                      hrCommittedGravity, entry.committed_text_range ? 1 : 0);
    SecureClearCommitUndoEntry(entry);
    SecureEraseString(engine_display.text);
    SecureEraseString(raw);
    SecureEraseString(display);
    SecureEraseVector(buf);
}

void VietnameseIME::CaptureCommitUndoDirectInlineTsf(
    TfEditCookie ec, ITfContext* pic,
    std::wstring_view committed_display,
    CommitUndoEntry::TransformKind transform_kind,
    std::wstring_view original_text,
    ITfRange* committed_range_override) {
    if (!pic || committed_display.empty()) {
        return;
    }

    std::wstring raw = engine_.GetRawString();
    std::wstring display(committed_display);
    const std::wstring_view restore_text = original_text.empty()
        ? std::wstring_view(raw)
        : original_text;
    if (!ShouldCaptureCommitUndo(restore_text, display)) {
        SecureEraseString(raw);
        SecureEraseString(display);
        return;
    }

    TF_SELECTION selection{};
    ULONG fetched = 0;
    ComPtr<ITfRange> caret_range;
    if (FAILED(pic->GetSelection(
            ec, TF_DEFAULT_SELECTION, 1, &selection, &fetched)) ||
        fetched == 0 || !selection.range) {
        SecureEraseString(raw);
        SecureEraseString(display);
        return;
    }
    caret_range.Attach(selection.range);

    ComPtr<ITfRange> committed_range;
    LONG shifted = 0;
    HRESULT hrRange = S_OK;
    if (committed_range_override) {
        hrRange = committed_range_override->Clone(
            committed_range.GetAddressOf());
        shifted = -static_cast<LONG>(display.length());
    } else {
        hrRange = caret_range->Clone(committed_range.GetAddressOf());
        if (SUCCEEDED(hrRange) && committed_range) {
            committed_range->Collapse(ec, TF_ANCHOR_END);
            hrRange = committed_range->ShiftStart(
                ec, -static_cast<LONG>(display.length()), &shifted, nullptr);
        }
    }
    if (FAILED(hrRange) || !committed_range ||
        shifted != -static_cast<LONG>(display.length())) {
        SecureEraseString(raw);
        SecureEraseString(display);
        return;
    }

    std::vector<wchar_t> verified_text(display.length() + 1, L'\0');
    ULONG verified_fetched = 0;
    const HRESULT hrVerify = committed_range->GetText(
        ec, 0, verified_text.data(),
        static_cast<ULONG>(display.length()), &verified_fetched);
    const bool text_matches = SUCCEEDED(hrVerify) &&
        verified_fetched == display.length() &&
        std::wstring_view(verified_text.data(), verified_fetched) == display;
    const HRESULT hrGravity = text_matches
        ? committed_range->SetGravity(
              ec, TF_GRAVITY_FORWARD, TF_GRAVITY_BACKWARD)
        : E_FAIL;
    SecureEraseVector(verified_text);
    if (!text_matches || FAILED(hrGravity)) {
        SecureEraseString(raw);
        SecureEraseString(display);
        return;
    }

    CommitUndoEntry entry;
    entry.raw_keys = raw;
    entry.original_text.assign(original_text);
    entry.display_text = display;
    entry.method = engine_.GetInputMethod();
    entry.transform_kind = transform_kind;
    entry.committed_tick = GetTickCount64();
    entry.hwnd = GetBestFocusWindow();
    entry.expected_context = ComPtr<ITfContext>(pic);
    entry.committed_text_range = committed_range;
    entry.expected_caret_range = caret_range;
    entry.is_tsf = true;

    ClearLastCommitUndo();
    last_commit_undo_ = entry;
    logger::LogFormat(
        logger::Level::Info,
        L"CaptureCommitUndo (TSF direct): raw_len=%zu, display_len=%zu, kind=%d",
        raw.length(), display.length(), static_cast<int>(transform_kind));
    SecureClearCommitUndoEntry(entry);
    SecureEraseString(raw);
    SecureEraseString(display);
}

void VietnameseIME::CaptureCommitUndoDirectInline(
    HWND hwnd, bool is_scintilla,
    std::wstring_view committed_display,
    CommitUndoEntry::TransformKind transform_kind,
    std::wstring_view original_text,
    std::optional<size_t> expected_caret_override) {
    core::EngineDisplayResult engine_display = engine_.GetDisplayResult();
    std::wstring display = committed_display.empty()
        ? engine_display.text
        : std::wstring(committed_display);
    std::wstring raw = engine_.GetRawString();

    const std::wstring_view restore_text = original_text.empty()
        ? std::wstring_view(raw)
        : original_text;
    if (!ShouldCaptureCommitUndo(restore_text, display)) {
        SecureEraseString(engine_display.text);
        SecureEraseString(raw);
        SecureEraseString(display);
        return;
    }

    CommitUndoEntry entry;
    entry.raw_keys = raw;
    entry.original_text.assign(original_text);
    entry.display_text = display;
    entry.method = engine_.GetInputMethod();
    if (transform_kind == CommitUndoEntry::TransformKind::None &&
        engine_display.HasSpellerCorrection()) {
        transform_kind = CommitUndoEntry::TransformKind::SpellerCorrection;
    }
    entry.transform_kind = transform_kind;
    entry.committed_tick = GetTickCount64();
    entry.hwnd = hwnd;
    entry.is_tsf = false;

    if (expected_caret_override) {
        entry.expected_caret_offset = *expected_caret_override;
    } else if (is_scintilla) {
        entry.expected_caret_offset = scintilla_direct_inline_start_ + scintilla_direct_inline_byte_length_;
    } else {
        DWORD sel_start = 0;
        DWORD sel_end = 0;
        ::SendMessageW(hwnd, EM_GETSEL, reinterpret_cast<WPARAM>(&sel_start), reinterpret_cast<LPARAM>(&sel_end));
        entry.expected_caret_offset = static_cast<size_t>(sel_end);
    }

    ClearLastCommitUndo();
    last_commit_undo_ = entry;
    logger::LogFormat(logger::Level::Info,
                      L"CaptureCommitUndo (Direct): raw_len=%zu, display_len=%zu, scintilla=%d, offset=%zu",
                      raw.length(), display.length(), is_scintilla, entry.expected_caret_offset);
    SecureClearCommitUndoEntry(entry);
    SecureEraseString(engine_display.text);
    SecureEraseString(raw);
    SecureEraseString(display);
}

bool VietnameseIME::TryRestoreLastCommittedRaw(
    TfEditCookie ec, ITfContext* pic, bool from_backspace) {
    if (last_commit_undo_ && !last_commit_undo_->original_text.empty()) {
        ClearLastCommitUndo();
        return false;
    }

    enum class VerificationFailureStage {
        Preconditions = 1,
        Context,
        Selection,
        Empty,
        Clone,
        Shift,
        Read,
        Match,
        StoredClone,
        StoredRead,
        StoredBoundary,
        StoredCaret,
        StoredReady,
    };

    if (!last_commit_undo_ || !last_commit_undo_->is_tsf || !pic) {
        logger::LogFormat(
            logger::Level::Warning,
            L"TSF restore verify failed: stage=%d, entry=%d, tsf=%d, context=%d",
            static_cast<int>(VerificationFailureStage::Preconditions),
            last_commit_undo_.has_value() ? 1 : 0,
            last_commit_undo_ && last_commit_undo_->is_tsf ? 1 : 0,
            pic ? 1 : 0);
        return false;
    }

    const bool telegram_tsf = IsTelegramProcess();
    const bool same_tsf_context = telegram_tsf &&
                                  IsSameComObject(pic, last_commit_undo_->expected_context.Get());
    const bool context_valid = telegram_tsf
        ? same_tsf_context
        : GetBestFocusWindow() == last_commit_undo_->hwnd;
    if (!context_valid) {
        logger::LogFormat(
            logger::Level::Warning,
            L"TSF restore verify failed: stage=%d, telegram=%d, context=%d, display_len=%zu, raw_len=%zu",
            static_cast<int>(VerificationFailureStage::Context),
            telegram_tsf ? 1 : 0, context_valid ? 1 : 0,
            last_commit_undo_->display_text.length(),
            last_commit_undo_->raw_keys.length());
        ClearLastCommitUndo();
        return false;
    }

    TF_SELECTION sel{};
    ULONG fetched = 0;
    const HRESULT hrSelection = pic->GetSelection(
        ec, TF_DEFAULT_SELECTION, 1, &sel, &fetched);
    ComPtr<ITfRange> range;
    if (sel.range) {
        range.Attach(sel.range);
    }
    if (FAILED(hrSelection) || fetched == 0 || !range) {
        logger::LogFormat(
            logger::Level::Warning,
            L"TSF restore verify failed: stage=%d, selection_hr=0x%08X, fetched=%u, range=%d, display_len=%zu, raw_len=%zu",
            static_cast<int>(VerificationFailureStage::Selection),
            hrSelection, fetched, range ? 1 : 0,
            last_commit_undo_->display_text.length(), last_commit_undo_->raw_keys.length());
        ClearLastCommitUndo();
        return false;
    }

    BOOL empty = TRUE;
    const HRESULT hrEmpty = range->IsEmpty(ec, &empty);
    if (FAILED(hrEmpty) || !empty) {
        logger::LogFormat(
            logger::Level::Warning,
            L"TSF restore verify failed: stage=%d, telegram=%d, empty_hr=0x%08X, empty=%d, display_len=%zu, raw_len=%zu",
            static_cast<int>(VerificationFailureStage::Empty),
            telegram_tsf ? 1 : 0, hrEmpty, empty ? 1 : 0,
            last_commit_undo_->display_text.length(), last_commit_undo_->raw_keys.length());
        ClearLastCommitUndo();
        return false;
    }

    {
        ComPtr<ITfRange> verify_range;
        bool match_found = false;
        bool has_trailing_space = false;
        bool used_stored_range_fallback = false;
        std::wstring matched_text;
        const size_t display_length = last_commit_undo_->display_text.length();
        ComPtr<ITfRange> stored_word_range;
        ComPtr<ITfRange> stored_boundary_range;

        struct CandidateVerificationStatus {
            HRESULT clone_hr = E_FAIL;
            HRESULT shift_hr = E_FAIL;
            HRESULT read_hr = E_FAIL;
            LONG shifted = 0;
            ULONG fetched = 0;
            bool shift_succeeded = false;
            bool read_succeeded = false;
            bool matched = false;
        };

        CandidateVerificationStatus exact_status;
        exact_status.clone_hr = range->Clone(verify_range.GetAddressOf());
        CandidateVerificationStatus spaced_status;

        auto verify_candidate = [&](ITfRange* candidate,
                                     size_t expected_length,
                                     bool candidate_has_trailing_space,
                                     CandidateVerificationStatus& status) -> bool {
            if (!candidate || expected_length == 0 ||
                expected_length > static_cast<size_t>((std::numeric_limits<ULONG>::max)())) {
                return false;
            }

            std::wstring text_buf(expected_length, L'\0');
            status.read_hr = candidate->GetText(
                ec, 0, &text_buf[0], static_cast<ULONG>(text_buf.size()), &status.fetched);
            status.read_succeeded = SUCCEEDED(status.read_hr) &&
                                    status.fetched == static_cast<ULONG>(text_buf.size());
            bool matched = status.read_succeeded;
            if (matched) {
                matched = candidate_has_trailing_space
                    ? text_buf.back() == L' ' &&
                      text_buf.compare(0, display_length, last_commit_undo_->display_text) == 0
                    : text_buf == last_commit_undo_->display_text;
            }
            if (matched) {
                matched_text = std::move(text_buf);
            }
            status.matched = matched;
            SecureEraseString(text_buf);
            return matched;
        };

        if (SUCCEEDED(exact_status.clone_hr) && verify_range) {
            const LONG to_shift = -static_cast<LONG>(display_length);
            exact_status.shift_hr = verify_range->ShiftStart(
                ec, to_shift, &exact_status.shifted, nullptr);
            exact_status.shift_succeeded = SUCCEEDED(exact_status.shift_hr) &&
                (exact_status.shifted == to_shift || exact_status.shifted == -to_shift);
            if (exact_status.shift_succeeded) {
                match_found = verify_candidate(
                    verify_range.Get(), display_length, false, exact_status);
            }
        }

        if (!match_found) {
            verify_range.Reset();
            spaced_status.clone_hr = range->Clone(verify_range.GetAddressOf());
            if (SUCCEEDED(spaced_status.clone_hr) && verify_range) {
                const size_t boundary_length = display_length + 1;
                const LONG to_shift_space = -static_cast<LONG>(boundary_length);
                spaced_status.shift_hr = verify_range->ShiftStart(
                    ec, to_shift_space, &spaced_status.shifted, nullptr);
                spaced_status.shift_succeeded = SUCCEEDED(spaced_status.shift_hr) &&
                    (spaced_status.shifted == to_shift_space ||
                     spaced_status.shifted == -to_shift_space);
                if (spaced_status.shift_succeeded) {
                    match_found = verify_candidate(
                        verify_range.Get(), boundary_length, true, spaced_status);
                    has_trailing_space = match_found;
                }
            }
        }

        const bool selection_path_unreadable =
            !spaced_status.shift_succeeded || !spaced_status.read_succeeded;
        if (!match_found && telegram_tsf && selection_path_unreadable) {
            HRESULT hrStoredClone = E_POINTER;
            HRESULT hrStoredRead = E_FAIL;
            ULONG stored_fetched = 0;
            bool stored_word_matches = false;
            HRESULT hrBoundaryClone = E_FAIL;
            HRESULT hrBoundaryCollapse = E_FAIL;
            HRESULT hrBoundaryShift = E_FAIL;
            LONG boundary_shifted = 0;
            HRESULT hrBoundaryRead = E_FAIL;
            ULONG boundary_fetched = 0;
            bool boundary_is_space = false;
            HRESULT hrBoundaryEndClone = E_FAIL;
            HRESULT hrBoundaryEndCollapse = E_FAIL;
            HRESULT hrCaretStart = E_FAIL;
            HRESULT hrCaretEnd = E_FAIL;
            LONG caret_start_comparison = 1;
            LONG caret_end_comparison = 1;
            bool caret_at_boundary_end = false;
            HRESULT hrCombinedClone = E_FAIL;
            HRESULT hrCombinedShift = E_FAIL;
            LONG combined_shifted = 0;

            if (last_commit_undo_->committed_text_range) {
                hrStoredClone = last_commit_undo_->committed_text_range->Clone(
                    stored_word_range.GetAddressOf());
            }
            if (SUCCEEDED(hrStoredClone) && stored_word_range &&
                display_length < static_cast<size_t>((std::numeric_limits<ULONG>::max)())) {
                std::wstring stored_text(display_length + 1, L'\0');
                hrStoredRead = stored_word_range->GetText(
                    ec, 0, stored_text.data(), static_cast<ULONG>(stored_text.size()),
                    &stored_fetched);
                stored_word_matches = SUCCEEDED(hrStoredRead) &&
                    stored_fetched == display_length &&
                    stored_text.compare(0, display_length,
                                        last_commit_undo_->display_text) == 0;
                SecureEraseString(stored_text);
            }

            if (stored_word_matches) {
                hrBoundaryClone = stored_word_range->Clone(
                    stored_boundary_range.GetAddressOf());
                if (SUCCEEDED(hrBoundaryClone) && stored_boundary_range) {
                    hrBoundaryCollapse = stored_boundary_range->Collapse(
                        ec, TF_ANCHOR_END);
                }
                if (SUCCEEDED(hrBoundaryCollapse)) {
                    hrBoundaryShift = stored_boundary_range->ShiftEnd(
                        ec, 1, &boundary_shifted, nullptr);
                }
                if (SUCCEEDED(hrBoundaryShift) && boundary_shifted == 1) {
                    wchar_t boundary_text[2] = {0, 0};
                    hrBoundaryRead = stored_boundary_range->GetText(
                        ec, 0, boundary_text, 2, &boundary_fetched);
                    boundary_is_space = SUCCEEDED(hrBoundaryRead) &&
                        boundary_fetched == 1 && boundary_text[0] == L' ';
                    SecureEraseBuffer(boundary_text, std::size(boundary_text));
                }
            }

            ComPtr<ITfRange> boundary_end;
            if (boundary_is_space) {
                hrBoundaryEndClone = stored_boundary_range->Clone(
                    boundary_end.GetAddressOf());
                if (SUCCEEDED(hrBoundaryEndClone) && boundary_end) {
                    hrBoundaryEndCollapse = boundary_end->Collapse(ec, TF_ANCHOR_END);
                }
                if (SUCCEEDED(hrBoundaryEndCollapse)) {
                    hrCaretStart = range->CompareStart(
                        ec, boundary_end.Get(), TF_ANCHOR_START,
                        &caret_start_comparison);
                    hrCaretEnd = range->CompareEnd(
                        ec, boundary_end.Get(), TF_ANCHOR_END,
                        &caret_end_comparison);
                    caret_at_boundary_end = SUCCEEDED(hrCaretStart) &&
                        SUCCEEDED(hrCaretEnd) &&
                        caret_start_comparison == 0 &&
                        caret_end_comparison == 0;
                }
            }

            bool fallback_accepted = CanUseStoredTsfRangeFallback(
                telegram_tsf, selection_path_unreadable, stored_word_matches,
                boundary_is_space, caret_at_boundary_end);
            if (fallback_accepted) {
                verify_range.Reset();
                hrCombinedClone = stored_word_range->Clone(
                    verify_range.GetAddressOf());
                if (SUCCEEDED(hrCombinedClone) && verify_range) {
                    hrCombinedShift = verify_range->ShiftEnd(
                        ec, 1, &combined_shifted, nullptr);
                }
                fallback_accepted = SUCCEEDED(hrCombinedClone) &&
                    SUCCEEDED(hrCombinedShift) && combined_shifted == 1;
            }

            VerificationFailureStage fallback_stage =
                VerificationFailureStage::StoredReady;
            if (FAILED(hrStoredClone) || !stored_word_range) {
                fallback_stage = VerificationFailureStage::StoredClone;
            } else if (!stored_word_matches) {
                fallback_stage = VerificationFailureStage::StoredRead;
            } else if (!boundary_is_space) {
                fallback_stage = VerificationFailureStage::StoredBoundary;
            } else if (!caret_at_boundary_end) {
                fallback_stage = VerificationFailureStage::StoredCaret;
            } else if (FAILED(hrCombinedClone) || FAILED(hrCombinedShift) ||
                       combined_shifted != 1) {
                fallback_stage = VerificationFailureStage::StoredBoundary;
            }

            logger::LogFormat(
                fallback_accepted ? logger::Level::Info : logger::Level::Warning,
                L"Telegram stored-range verify: stage=%d, clone_hr=0x%08X, "
                L"word_read_hr=0x%08X, word_fetched=%u, word_match=%d, "
                L"boundary_clone_hr=0x%08X, boundary_collapse_hr=0x%08X, "
                L"boundary_shift_hr=0x%08X, boundary_shift=%ld, "
                L"boundary_read_hr=0x%08X, boundary_fetched=%u, boundary_match=%d, "
                L"boundary_end_clone_hr=0x%08X, boundary_end_collapse_hr=0x%08X, "
                L"caret_start_hr=0x%08X, caret_start_cmp=%ld, "
                L"caret_end_hr=0x%08X, caret_end_cmp=%ld, caret_match=%d, "
                L"combined_clone_hr=0x%08X, combined_shift_hr=0x%08X, "
                L"combined_shift=%ld, accepted=%d, display_len=%zu, raw_len=%zu",
                static_cast<int>(fallback_stage), hrStoredClone,
                hrStoredRead, stored_fetched, stored_word_matches ? 1 : 0,
                hrBoundaryClone, hrBoundaryCollapse, hrBoundaryShift,
                boundary_shifted, hrBoundaryRead, boundary_fetched,
                boundary_is_space ? 1 : 0, hrBoundaryEndClone,
                hrBoundaryEndCollapse, hrCaretStart, caret_start_comparison,
                hrCaretEnd, caret_end_comparison,
                caret_at_boundary_end ? 1 : 0, hrCombinedClone,
                hrCombinedShift, combined_shifted,
                fallback_accepted ? 1 : 0, display_length,
                last_commit_undo_->raw_keys.length());

            if (fallback_accepted) {
                matched_text = last_commit_undo_->display_text;
                matched_text.push_back(L' ');
                match_found = true;
                has_trailing_space = true;
                used_stored_range_fallback = true;
            }
        }

        if (!match_found) {
            const bool clone_succeeded = SUCCEEDED(exact_status.clone_hr) ||
                                         SUCCEEDED(spaced_status.clone_hr);
            const bool shift_succeeded = exact_status.shift_succeeded ||
                                         spaced_status.shift_succeeded;
            const bool read_succeeded = exact_status.read_succeeded ||
                                        spaced_status.read_succeeded;
            VerificationFailureStage stage = VerificationFailureStage::Match;
            if (!clone_succeeded) {
                stage = VerificationFailureStage::Clone;
            } else if (!shift_succeeded) {
                stage = VerificationFailureStage::Shift;
            } else if (!read_succeeded) {
                stage = VerificationFailureStage::Read;
            }
            logger::LogFormat(
                logger::Level::Warning,
                L"TSF restore verify failed: stage=%d, telegram=%d, "
                L"exact_clone_hr=0x%08X, exact_shift_hr=0x%08X, exact_shift=%ld, "
                L"exact_read_hr=0x%08X, exact_fetched=%u, exact_match=%d, "
                L"spaced_clone_hr=0x%08X, spaced_shift_hr=0x%08X, spaced_shift=%ld, "
                L"spaced_read_hr=0x%08X, spaced_fetched=%u, spaced_match=%d, "
                L"display_len=%zu, raw_len=%zu",
                static_cast<int>(stage), telegram_tsf ? 1 : 0,
                exact_status.clone_hr, exact_status.shift_hr, exact_status.shifted,
                exact_status.read_hr, exact_status.fetched, exact_status.matched ? 1 : 0,
                spaced_status.clone_hr, spaced_status.shift_hr, spaced_status.shifted,
                spaced_status.read_hr, spaced_status.fetched, spaced_status.matched ? 1 : 0,
                display_length, last_commit_undo_->raw_keys.length());
        }

        if (match_found && verify_range) {
                    logger::LogFormat(logger::Level::Info, L"TryRestoreLastCommittedRaw (TSF) match: replacing word (len: %zu) with raw (len: %zu)",
                                      last_commit_undo_->display_text.length(), last_commit_undo_->raw_keys.length());

                    if (telegram_tsf) {
                        ComPtr<ITfRange> original_caret;
                        ComPtr<ITfRange> restore_anchor;
                        ComPtr<ITfRange> transaction_range;
                        ComPtr<ITfRange> boundary_range;
                        HRESULT hrOriginalCaret = range->Clone(original_caret.GetAddressOf());
                        HRESULT hrRestoreAnchor = verify_range->Clone(restore_anchor.GetAddressOf());
                        if (SUCCEEDED(hrRestoreAnchor) && restore_anchor) {
                            hrRestoreAnchor = restore_anchor->Collapse(ec, TF_ANCHOR_START);
                        }

                        HRESULT hrRangePrep = S_OK;
                        if (used_stored_range_fallback) {
                            hrRangePrep = stored_word_range->Clone(
                                transaction_range.GetAddressOf());
                            if (SUCCEEDED(hrRangePrep) && transaction_range) {
                                hrRangePrep = stored_boundary_range->Clone(
                                    boundary_range.GetAddressOf());
                            }
                            if (SUCCEEDED(hrRangePrep) && !boundary_range) {
                                hrRangePrep = E_POINTER;
                            }
                        } else if (has_trailing_space) {
                            hrRangePrep = verify_range->Clone(transaction_range.GetAddressOf());
                            if (SUCCEEDED(hrRangePrep) && transaction_range) {
                                LONG shifted_end = 0;
                                hrRangePrep = transaction_range->ShiftEnd(
                                    ec, -1, &shifted_end, nullptr);
                                if (SUCCEEDED(hrRangePrep) &&
                                    shifted_end != -1 && shifted_end != 1) {
                                    hrRangePrep = E_FAIL;
                                }
                            }
                            if (SUCCEEDED(hrRangePrep)) {
                                hrRangePrep = verify_range->Clone(boundary_range.GetAddressOf());
                            }
                            if (SUCCEEDED(hrRangePrep) && boundary_range) {
                                LONG shifted_start = 0;
                                const LONG display_shift =
                                    static_cast<LONG>(display_length);
                                hrRangePrep = boundary_range->ShiftStart(
                                    ec, display_shift, &shifted_start, nullptr);
                                if (SUCCEEDED(hrRangePrep) &&
                                    shifted_start != display_shift &&
                                    shifted_start != -display_shift) {
                                    hrRangePrep = E_FAIL;
                                }
                            }
                        } else {
                            transaction_range = verify_range;
                        }

                        engine_.Clear();
                        for (wchar_t key : last_commit_undo_->raw_keys) {
                            engine_.ProcessKey(key);
                        }
                        std::wstring restored_display = engine_.GetDisplayString();
                        const bool replay_valid = !restored_display.empty();
                        HRESULT hrRemove = E_FAIL;
                        HRESULT hrCollapse = has_trailing_space ? hrRangePrep : E_FAIL;
                        HRESULT hrComp = E_FAIL;
                        HRESULT hrUpdate = E_FAIL;
                        HRESULT hrCaret = E_FAIL;
                        bool caret_positioned = false;
                        bool text_removed = false;

                        auto verify_current_selection = [&]() -> bool {
                            if (matched_text.empty() ||
                                matched_text.length() >
                                    static_cast<size_t>((std::numeric_limits<ULONG>::max)())) {
                                return false;
                            }

                            TF_SELECTION current_selection{};
                            ULONG current_fetched = 0;
                            if (FAILED(pic->GetSelection(
                                    ec, TF_DEFAULT_SELECTION, 1,
                                    &current_selection, &current_fetched)) ||
                                current_fetched == 0 ||
                                !current_selection.range) {
                                return false;
                            }

                            ComPtr<ITfRange> current_range;
                            current_range.Attach(current_selection.range);
                            BOOL selection_empty = TRUE;
                            if (FAILED(current_range->IsEmpty(ec, &selection_empty)) ||
                                !selection_empty) {
                                return false;
                            }

                            LONG shifted = 0;
                            const LONG to_shift =
                                -static_cast<LONG>(matched_text.length());
                            if (FAILED(current_range->ShiftStart(
                                    ec, to_shift, &shifted, nullptr)) ||
                                (shifted != to_shift && shifted != -to_shift)) {
                                return false;
                            }

                            std::wstring text_buf(matched_text.length(), L'\0');
                            ULONG fetched_chars = 0;
                            const HRESULT hrText = current_range->GetText(
                                ec, 0, &text_buf[0],
                                static_cast<ULONG>(text_buf.size()), &fetched_chars);
                            const bool matched =
                                SUCCEEDED(hrText) &&
                                fetched_chars == static_cast<ULONG>(text_buf.size()) &&
                                text_buf == matched_text;
                            SecureEraseString(text_buf);
                            return matched;
                        };

                        SelectionUpdateScope selection_update_scope(is_updating_selection_);

                        auto rollback_telegram_restore = [&]() -> bool {
                            HRESULT hrAbort = S_OK;
                            if (HasActiveComposition()) {
                                hrAbort = AbortComposition(ec);
                            }

                            engine_.SecureClear();
                            HRESULT hrRestoreText = text_removed ? S_FALSE : S_OK;
                            HRESULT hrRestoreSelection = S_FALSE;
                            if (text_removed && has_trailing_space && transaction_range) {
                                // The trailing boundary was the only text removed in
                                // the canonical Telegram transaction. Reapply the
                                // word through the original word range first so a
                                // failed abort cannot duplicate it, then restore the
                                // single verified delimiter.
                                hrRestoreText = transaction_range->SetText(
                                    ec, 0, last_commit_undo_->display_text.c_str(),
                                    static_cast<LONG>(last_commit_undo_->display_text.length()));
                                if (SUCCEEDED(hrRestoreText)) {
                                    hrRestoreText = transaction_range->Collapse(
                                        ec, TF_ANCHOR_END);
                                }
                                if (SUCCEEDED(hrRestoreText)) {
                                    hrRestoreText = transaction_range->SetText(
                                        ec, 0, L" ", 1);
                                }
                                if (SUCCEEDED(hrRestoreText)) {
                                    hrRestoreText = transaction_range->Collapse(
                                        ec, TF_ANCHOR_END);
                                }
                                if (SUCCEEDED(hrRestoreText)) {
                                    TF_SELECTION restore_selection{};
                                    restore_selection.range = transaction_range.Get();
                                    restore_selection.style = sel.style;
                                    hrRestoreSelection = pic->SetSelection(
                                        ec, 1, &restore_selection);
                                }
                            } else if (text_removed && restore_anchor) {
                                hrRestoreText = restore_anchor->SetText(
                                    ec, 0, matched_text.c_str(),
                                    static_cast<LONG>(matched_text.length()));
                                if (SUCCEEDED(hrRestoreText)) {
                                    hrRestoreText = restore_anchor->Collapse(ec, TF_ANCHOR_END);
                                    if (SUCCEEDED(hrRestoreText)) {
                                        TF_SELECTION restore_selection{};
                                        restore_selection.range = restore_anchor.Get();
                                        restore_selection.style = sel.style;
                                        hrRestoreSelection = pic->SetSelection(
                                            ec, 1, &restore_selection);
                                    }
                                }
                            } else if (!text_removed && original_caret) {
                                TF_SELECTION restore_selection{};
                                restore_selection.range = original_caret.Get();
                                restore_selection.style = sel.style;
                                hrRestoreSelection = pic->SetSelection(
                                    ec, 1, &restore_selection);
                            }

                            bool rollback_text_verified = false;
                            bool rollback_selection_verified = false;
                            if (SUCCEEDED(hrRestoreSelection)) {
                                const bool verified = verify_current_selection();
                                rollback_text_verified = verified;
                                rollback_selection_verified = verified;
                            }
                            const bool rollback_verified =
                                rollback_text_verified && rollback_selection_verified &&
                                SUCCEEDED(hrAbort);
                            bool boundary_removed_for_backspace = false;
                            bool native_replay_succeeded = false;
                            HRESULT hrBoundaryFallback = E_FAIL;
                            HRESULT hrNativeReplay = E_FAIL;
                            if (from_backspace && has_trailing_space && rollback_verified) {
                                ComPtr<ITfRange> fallback_boundary;
                                if (text_removed && transaction_range) {
                                    hrBoundaryFallback = transaction_range->Clone(
                                        fallback_boundary.GetAddressOf());
                                    if (SUCCEEDED(hrBoundaryFallback) && fallback_boundary) {
                                        LONG shifted_start = 0;
                                        hrBoundaryFallback = fallback_boundary->ShiftStart(
                                            ec, -1, &shifted_start, nullptr);
                                        if (SUCCEEDED(hrBoundaryFallback) &&
                                            shifted_start != -1 && shifted_start != 1) {
                                            hrBoundaryFallback = E_FAIL;
                                        }
                                    }
                                } else if (boundary_range) {
                                    // If deleting the delimiter failed before any
                                    // mutation, boundary_range is already the exact
                                    // verified one-character range.
                                    hrBoundaryFallback = boundary_range->Clone(
                                        fallback_boundary.GetAddressOf());
                                }
                                if (SUCCEEDED(hrBoundaryFallback) && fallback_boundary) {
                                    std::wstring boundary_text(1, L'\0');
                                    ULONG boundary_fetched = 0;
                                    const HRESULT hrBoundaryText = fallback_boundary->GetText(
                                        ec, 0, &boundary_text[0], 1, &boundary_fetched);
                                    const bool boundary_matches =
                                        SUCCEEDED(hrBoundaryText) &&
                                        boundary_fetched == 1 && boundary_text[0] == L' ';
                                    SecureEraseString(boundary_text);
                                    if (boundary_matches) {
                                        hrBoundaryFallback = fallback_boundary->SetText(
                                            ec, 0, L"", 0);
                                        if (SUCCEEDED(hrBoundaryFallback)) {
                                            // The delimiter is gone even if caret
                                            // positioning below is rejected.
                                            boundary_removed_for_backspace = true;
                                            hrBoundaryFallback = fallback_boundary->Collapse(
                                                ec, TF_ANCHOR_START);
                                            if (SUCCEEDED(hrBoundaryFallback)) {
                                                TF_SELECTION fallback_selection{};
                                                fallback_selection.range = fallback_boundary.Get();
                                                fallback_selection.style = sel.style;
                                                hrBoundaryFallback = pic->SetSelection(
                                                    ec, 1, &fallback_selection);
                                            }
                                        }
                                    }
                                }

                                if (!boundary_removed_for_backspace) {
                                    INPUT inputs[2]{};
                                    inputs[0].type = INPUT_KEYBOARD;
                                    inputs[0].ki.wVk = VK_BACK;
                                    inputs[0].ki.dwExtraInfo = 0xDEADC0DE;
                                    inputs[1].type = INPUT_KEYBOARD;
                                    inputs[1].ki.wVk = VK_BACK;
                                    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
                                    inputs[1].ki.dwExtraInfo = 0xDEADC0DE;
                                    const UINT sent = ::SendInput(
                                        2, inputs, sizeof(INPUT));
                                    hrNativeReplay = sent == 2 ? S_OK : E_FAIL;
                                    native_replay_succeeded = sent == 2;
                                }
                            }
                            const CommitUndoRollbackDisposition disposition =
                                DecideCommitUndoRollbackDisposition(
                                    rollback_text_verified,
                                    rollback_selection_verified,
                                    SUCCEEDED(hrAbort),
                                    has_trailing_space,
                                    from_backspace);
                            const bool consume_backspace = CanConsumeCommitUndoBackspace(
                                false,
                                rollback_verified,
                                has_trailing_space,
                                boundary_removed_for_backspace,
                                native_replay_succeeded);
                            logger::LogFormat(
                                logger::Level::Warning,
                                L"Telegram restore rollback: remove_hr=0x%08X, collapse_hr=0x%08X, "
                                L"start_hr=0x%08X, update_hr=0x%08X, abort_hr=0x%08X, "
                                L"restore_text_hr=0x%08X, restore_selection_hr=0x%08X, removed=%s, "
                                L"text_verified=%s, selection_verified=%s, boundary_removed=%s, "
                                L"native_replay=%s, consume=%s, disposition=%d, "
                                L"display_len=%zu, raw_len=%zu",
                                hrRemove, hrCollapse, hrComp, hrUpdate, hrAbort,
                                hrRestoreText, hrRestoreSelection, text_removed ? L"TRUE" : L"FALSE",
                                rollback_text_verified ? L"TRUE" : L"FALSE",
                                rollback_selection_verified ? L"TRUE" : L"FALSE",
                                boundary_removed_for_backspace ? L"TRUE" : L"FALSE",
                                native_replay_succeeded ? L"TRUE" : L"FALSE",
                                consume_backspace ? L"TRUE" : L"FALSE",
                                static_cast<int>(disposition),
                                matched_text.length(), last_commit_undo_->raw_keys.length());
                            logger::LogFormat(
                                logger::Level::Info,
                                L"Telegram restore fallback: boundary_hr=0x%08X, replay_hr=0x%08X",
                                hrBoundaryFallback, hrNativeReplay);
                            SecureEraseString(restored_display);
                            SecureEraseString(matched_text);
                            ClearLastCommitUndo();
                            return from_backspace && consume_backspace;
                        };

                        if (!replay_valid ||
                            FAILED(hrOriginalCaret) ||
                            FAILED(hrRestoreAnchor) ||
                            !restore_anchor ||
                            FAILED(hrRangePrep) ||
                            !transaction_range) {
                            return rollback_telegram_restore();
                        }

                        // Keep OnEndEdit out of the document transaction from the
                        // first text mutation through rollback/commit cleanup.
                        if (has_trailing_space) {
                            hrRemove = boundary_range
                                ? boundary_range->SetText(ec, 0, L"", 0)
                                : E_POINTER;
                        } else {
                            hrRemove = transaction_range->SetText(ec, 0, L"", 0);
                        }
                        text_removed = SUCCEEDED(hrRemove);
                        if (!has_trailing_space && SUCCEEDED(hrRemove)) {
                            hrCollapse = transaction_range->Collapse(ec, TF_ANCHOR_START);
                        }
                        if (SUCCEEDED(hrRemove) && SUCCEEDED(hrCollapse)) {
                            hrComp = StartComposition(ec, pic, transaction_range.Get());
                            if (SUCCEEDED(hrComp)) {
                                hrUpdate = UpdateCompositionText(
                                    ec, pic, transaction_range.Get(), restored_display);
                                if (SUCCEEDED(hrUpdate) && HasActiveComposition()) {
                                    ComPtr<ITfRange> comp_range;
                                    if (SUCCEEDED(active_composition_->GetRange(
                                            comp_range.GetAddressOf())) &&
                                        comp_range) {
                                        ComPtr<ITfRange> caret_range;
                                        hrCaret = comp_range->Clone(caret_range.GetAddressOf());
                                        if (SUCCEEDED(hrCaret) && caret_range) {
                                            hrCaret = caret_range->Collapse(ec, TF_ANCHOR_END);
                                        } else if (SUCCEEDED(hrCaret)) {
                                            hrCaret = E_POINTER;
                                        }
                                        if (SUCCEEDED(hrCaret) && caret_range) {
                                            TF_SELECTION caret_selection{};
                                            caret_selection.range = caret_range.Get();
                                            caret_selection.style = sel.style;
                                            // Telegram may not expose a synchronized
                                            // selection range for CompareStart here;
                                            // the direct SetSelection HRESULT is the
                                            // authoritative caret result.
                                            hrCaret = pic->SetSelection(
                                                ec, 1, &caret_selection);
                                        }
                                    }
                                }
                                caret_positioned = SUCCEEDED(hrCaret);
                            }
                        }

                        const CommitUndoResumeDisposition disposition =
                            DecideCommitUndoResumeDisposition(
                                SUCCEEDED(hrComp), SUCCEEDED(hrUpdate),
                                HasActiveComposition(), caret_positioned);
                        logger::LogFormat(
                            logger::Level::Info,
                            L"Telegram restore transaction: boundary=%s, remove_hr=0x%08X, "
                            L"collapse_hr=0x%08X, start_hr=0x%08X, update_hr=0x%08X, "
                            L"caret_hr=0x%08X, active=%s, caret=%s, disposition=%d, "
                            L"display_len=%zu, raw_len=%zu",
                            has_trailing_space ? L"TRUE" : L"FALSE",
                            hrRemove, hrCollapse, hrComp, hrUpdate, hrCaret,
                            HasActiveComposition() ? L"TRUE" : L"FALSE",
                            caret_positioned ? L"TRUE" : L"FALSE",
                            static_cast<int>(disposition),
                            restored_display.length(), last_commit_undo_->raw_keys.length());
                        if (disposition == CommitUndoResumeDisposition::ResumeComposition) {
                            SecureEraseString(restored_display);
                            SecureEraseString(matched_text);
                            ClearLastCommitUndo();
                            return true;
                        }
                        return rollback_telegram_restore();
                    }

                    is_updating_selection_ = true;
                    engine_.Clear();
                    for (wchar_t key : last_commit_undo_->raw_keys) {
                        engine_.ProcessKey(key);
                    }
                    HRESULT hrComp = StartComposition(ec, pic, verify_range.Get());
                    if (SUCCEEDED(hrComp)) {
                        UpdateCompositionText(ec, pic, verify_range.Get(), engine_.GetDisplayString());
                    } else {
                        verify_range->SetText(ec, 0, last_commit_undo_->raw_keys.c_str(), static_cast<LONG>(last_commit_undo_->raw_keys.length()));
                        verify_range->Collapse(ec, TF_ANCHOR_END);
                        TF_SELECTION restored_selection{};
                        restored_selection.range = verify_range.Get();
                        restored_selection.style = sel.style;
                        restored_selection.style.ase = TF_AE_NONE;
                        restored_selection.style.fInterimChar = FALSE;
                        pic->SetSelection(ec, 1, &restored_selection);
                    }
                    is_updating_selection_ = false;
                    SecureEraseString(matched_text);
                    ClearLastCommitUndo();
                    return true;
                }
                SecureEraseString(matched_text);
            }
    ClearLastCommitUndo();
    return false;
}

bool VietnameseIME::TryRestoreLastCommittedRawDirectInline(HWND hwnd, bool resume_after_boundary) {
    if (!last_commit_undo_ || last_commit_undo_->is_tsf) {
        return false;
    }
    if (!last_commit_undo_->original_text.empty()) {
        ClearLastCommitUndo();
        return false;
    }

    if (hwnd != last_commit_undo_->hwnd) {
        ClearLastCommitUndo();
        return false;
    }

    if (ClassNameEquals(hwnd, L"Scintilla")) {
        constexpr LRESULT SCI_GETSELECTIONSTART = 2143;
        constexpr LRESULT SCI_GETSELECTIONEND = 2145;
        constexpr LRESULT SCI_SETSEL = 2160;
        constexpr LRESULT SCI_REPLACESEL = 2170;
        constexpr LRESULT SCI_GETTEXTRANGE = 2162;
        struct SciCharacterRange {
            LONG_PTR cpMin;
            LONG_PTR cpMax;
        };
        struct SciTextRange {
            SciCharacterRange chrg;
            char* lpstrText;
        };

        LRESULT sel_start = ::SendMessageW(hwnd, SCI_GETSELECTIONSTART, 0, 0);
        LRESULT sel_end = ::SendMessageW(hwnd, SCI_GETSELECTIONEND, 0, 0);
        if (sel_start >= 0 && sel_start == sel_end) {
            size_t caret = static_cast<size_t>(sel_start);
            const size_t expected_caret = last_commit_undo_->expected_caret_offset;
            const bool has_trailing_space = resume_after_boundary &&
                                            caret > expected_caret && caret - expected_caret == 1;
            if (caret == expected_caret || has_trailing_space) {
                std::string display_utf8;
                if (!ConvertWideToUtf8(last_commit_undo_->display_text, display_utf8)) {
                    SecureEraseStringUtf8(display_utf8);
                    ClearLastCommitUndo();
                    return false;
                }

                const size_t span_length = display_utf8.length() + (has_trailing_space ? 1 : 0);
                if (display_utf8.empty() || caret < span_length) {
                    SecureEraseStringUtf8(display_utf8);
                    ClearLastCommitUndo();
                    return false;
                }

                const size_t start_pos = caret - span_length;
                std::vector<char> current_bytes(span_length + 1, '\0');
                SciTextRange text_range{
                    {static_cast<LONG_PTR>(start_pos), static_cast<LONG_PTR>(caret)},
                    current_bytes.data()
                };
                ::SendMessageW(hwnd, SCI_GETTEXTRANGE, 0, reinterpret_cast<LPARAM>(&text_range));
                std::string current_text(current_bytes.data(), std::strlen(current_bytes.data()));
                std::optional<VerifiedTextSpan> verified_span;
                if (has_trailing_space) {
                    verified_span = FindVerifiedBytesBeforeCaretWithOptionalTrailingSpace(
                        current_text, current_text.length(), display_utf8);
                    if (!verified_span || !verified_span->has_trailing_space) {
                        SecureEraseVector(current_bytes);
                        SecureEraseStringUtf8(display_utf8);
                        SecureEraseStringUtf8(current_text);
                        ClearLastCommitUndo();
                        return false;
                    }
                } else {
                    verified_span = FindVerifiedBytesBeforeCaret(current_text, current_text.length(), display_utf8);
                    if (!verified_span) {
                        SecureEraseVector(current_bytes);
                        SecureEraseStringUtf8(display_utf8);
                        SecureEraseStringUtf8(current_text);
                        ClearLastCommitUndo();
                        return false;
                    }
                }

                if (has_trailing_space) {
                    engine_.Clear();
                    for (wchar_t key : last_commit_undo_->raw_keys) {
                        engine_.ProcessKey(key);
                    }
                    std::wstring restored_display = engine_.GetDisplayString();
                    std::string restored_utf8;
                    if (!ConvertWideToUtf8(restored_display, restored_utf8) ||
                        restored_display.empty() || restored_utf8.empty()) {
                        engine_.SecureClear();
                        SecureEraseString(restored_display);
                        SecureEraseStringUtf8(restored_utf8);
                        SecureEraseVector(current_bytes);
                        SecureEraseStringUtf8(display_utf8);
                        SecureEraseStringUtf8(current_text);
                        ClearLastCommitUndo();
                        return false;
                    }

                    ::SendMessageW(hwnd, SCI_SETSEL, start_pos, caret);
                    logger::LogFormat(logger::Level::Info,
                                      L"TryRestoreLastCommittedRawDirectInline (Scintilla) boundary match: display_len=%zu, raw_len=%zu",
                                      restored_display.length(), last_commit_undo_->raw_keys.length());
                    ::SendMessageW(hwnd, SCI_REPLACESEL, 0, reinterpret_cast<LPARAM>(restored_utf8.c_str()));
                    scintilla_direct_inline_start_ = start_pos;
                    scintilla_direct_inline_byte_length_ = restored_utf8.length();
                    direct_inline_display_length_ = restored_display.length();

                    SecureEraseString(restored_display);
                    SecureEraseStringUtf8(restored_utf8);
                    SecureEraseVector(current_bytes);
                    SecureEraseStringUtf8(display_utf8);
                    SecureEraseStringUtf8(current_text);
                    ClearLastCommitUndo();
                    return true;
                }

                std::string raw_utf8;
                if (!ConvertWideToUtf8(last_commit_undo_->raw_keys, raw_utf8) || raw_utf8.empty()) {
                    SecureEraseVector(current_bytes);
                    SecureEraseStringUtf8(display_utf8);
                    SecureEraseStringUtf8(current_text);
                    SecureEraseStringUtf8(raw_utf8);
                    ClearLastCommitUndo();
                    return false;
                }

                ::SendMessageW(hwnd, SCI_SETSEL, start_pos, caret);
                logger::LogFormat(logger::Level::Info, L"TryRestoreLastCommittedRawDirectInline (Scintilla) match: replacing with raw (len: %zu)", raw_utf8.length());
                ::SendMessageW(hwnd, SCI_REPLACESEL, 0, reinterpret_cast<LPARAM>(raw_utf8.c_str()));
                SecureEraseVector(current_bytes);
                SecureEraseStringUtf8(display_utf8);
                SecureEraseStringUtf8(current_text);
                SecureEraseStringUtf8(raw_utf8);
                ClearLastCommitUndo();
                return true;
            }
        }
    } else if (ClassNameEquals(hwnd, L"Edit")) {
        DWORD sel_start = 0;
        DWORD sel_end = 0;
        ::SendMessageW(hwnd, EM_GETSEL, reinterpret_cast<WPARAM>(&sel_start), reinterpret_cast<LPARAM>(&sel_end));
        if (sel_start == sel_end) {
            size_t caret = static_cast<size_t>(sel_end);
            const size_t expected_caret = last_commit_undo_->expected_caret_offset;
            const bool has_trailing_space = resume_after_boundary &&
                                            caret > expected_caret && caret - expected_caret == 1;
            if (caret == expected_caret || has_trailing_space) {
                const int text_len = ::GetWindowTextLengthW(hwnd);
                if (text_len < 0) {
                    ClearLastCommitUndo();
                    return false;
                }
                std::vector<wchar_t> text_buf(static_cast<size_t>(text_len) + 1, L'\0');
                ::GetWindowTextW(hwnd, text_buf.data(), static_cast<int>(text_buf.size()));
                std::wstring current_text(text_buf.data(), static_cast<size_t>(text_len));
                std::optional<VerifiedTextSpan> verified_span;
                if (has_trailing_space) {
                    verified_span = FindVerifiedTextBeforeCaretWithOptionalTrailingSpace(
                        current_text, caret, last_commit_undo_->display_text);
                    if (!verified_span || !verified_span->has_trailing_space) {
                        SecureEraseVector(text_buf);
                        SecureEraseString(current_text);
                        ClearLastCommitUndo();
                        return false;
                    }
                } else {
                    verified_span = FindVerifiedTextBeforeCaret(current_text, caret, last_commit_undo_->display_text);
                    if (!verified_span) {
                        SecureEraseVector(text_buf);
                        SecureEraseString(current_text);
                        ClearLastCommitUndo();
                        return false;
                    }
                }

                if (has_trailing_space) {
                    engine_.Clear();
                    for (wchar_t key : last_commit_undo_->raw_keys) {
                        engine_.ProcessKey(key);
                    }
                    std::wstring restored_display = engine_.GetDisplayString();
                    if (restored_display.empty()) {
                        engine_.SecureClear();
                        SecureEraseString(restored_display);
                        SecureEraseVector(text_buf);
                        SecureEraseString(current_text);
                        ClearLastCommitUndo();
                        return false;
                    }

                    ::SendMessageW(hwnd, EM_SETSEL, verified_span->start, verified_span->end);
                    logger::LogFormat(logger::Level::Info,
                                      L"TryRestoreLastCommittedRawDirectInline (Edit) boundary match: display_len=%zu, raw_len=%zu",
                                      restored_display.length(), last_commit_undo_->raw_keys.length());
                    ::SendMessageW(hwnd, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(restored_display.c_str()));
                    direct_inline_display_length_ = restored_display.length();

                    SecureEraseString(restored_display);
                    SecureEraseVector(text_buf);
                    SecureEraseString(current_text);
                    ClearLastCommitUndo();
                    return true;
                }

                if (!verified_span) {
                    SecureEraseVector(text_buf);
                    SecureEraseString(current_text);
                    ClearLastCommitUndo();
                    return false;
                }

                ::SendMessageW(hwnd, EM_SETSEL, verified_span->start, verified_span->end);
                logger::LogFormat(logger::Level::Info, L"TryRestoreLastCommittedRawDirectInline (Edit) match: replacing with raw (len: %zu)", last_commit_undo_->raw_keys.length());
                ::SendMessageW(hwnd, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(last_commit_undo_->raw_keys.c_str()));
                SecureEraseVector(text_buf);
                SecureEraseString(current_text);
                ClearLastCommitUndo();
                return true;
            }
        }
    }

    ClearLastCommitUndo();
    return false;
}

bool VietnameseIME::TryProcessDirectCommitEsc(ITfContext* pic) {
    if (!HasDirectInlineState()) return false;
    
    std::wstring raw_keys = engine_.GetRawString();
    std::wstring display_str = engine_.GetDisplayString();
    
    HWND hwnd = GetBestFocusWindow();
    if (hwnd) {
        if (IsNotepadPlusPlusDirectInlineFocused()) {
            engine_.SecureClear();
            for (size_t i = 0; i < display_str.length(); ++i) {
                ProcessNotepadPlusPlusDirectBackspace();
            }
            for (wchar_t ch : raw_keys) {
                ProcessNotepadPlusPlusDirectChar(ch);
            }
            ResetDirectInlineState();
        } else if (IsDirectCommitApp()) {
            engine_.SecureClear();
            for (size_t i = 0; i < display_str.length(); ++i) {
                ProcessExplorerEditBackspace();
            }
            for (wchar_t ch : raw_keys) {
                ProcessExplorerEditChar(ch);
            }
            ResetDirectInlineState();
        } else if (IsFakeBackspaceApp()) {
            engine_.SecureClear();
            for (size_t i = 0; i < display_str.length(); ++i) {
                ProcessFakeBackspaceEditBackspace();
            }
            for (wchar_t ch : raw_keys) {
                ProcessFakeBackspaceEditChar(ch);
            }
            ResetDirectInlineState();
        } else if (IsWordTsfInlineApp() && pic) {
            ComPtr<EditSession> session;
            session.Attach(new (std::nothrow) EditSession(this, pic, EditAction::DirectRevertRaw, raw_keys));
            if (session) {
                HRESULT hr = 0;
                HRESULT hrReq = pic->RequestEditSession(client_id_, session.Get(), TF_ES_SYNC | TF_ES_READWRITE, &hr);
                logger::LogFormat(logger::Level::Info, L"RequestEditSession (DirectRevertRaw) returned hrReq = 0x%08X, hr = 0x%08X", hrReq, hr);
            }
        } else {
            ResetDirectInlineState();
        }
    } else {
        ResetDirectInlineState();
    }
    
    return true;
}

bool VietnameseIME::ShouldClaimHotkeyTestEvent(
    WPARAM wParam, bool is_key_down) const noexcept {
    if (hotkey_mode_ > static_cast<DWORD>(HotkeyMode::AltZ)) {
        return false;
    }
    return hotkey_toggle_state_.ShouldClaimTestEvent(
        static_cast<HotkeyMode>(hotkey_mode_),
        ClassifyHotkeyKey(wParam), is_key_down,
        ReadHotkeyModifiers());
}

bool VietnameseIME::DispatchHotkeyEvent(
    WPARAM wParam, LPARAM lParam, bool is_key_down, BOOL* pfEaten) {
    if (pfEaten) {
        *pfEaten = FALSE;
    }
    if (hotkey_mode_ > static_cast<DWORD>(HotkeyMode::AltZ)) {
        return false;
    }

    const HotkeyMode mode = static_cast<HotkeyMode>(hotkey_mode_);
    const HotkeyKey key = ClassifyHotkeyKey(wParam);
    const HotkeyModifiers modifiers = ReadHotkeyModifiers();
    const bool was_key_down =
        (static_cast<ULONG_PTR>(lParam) & (ULONG_PTR{1} << 30)) != 0;
    const bool alt_z_event = mode == HotkeyMode::AltZ &&
        hotkey_toggle_state_.ShouldClaimTestEvent(
            mode, key, is_key_down, modifiers);
    const bool should_toggle = hotkey_toggle_state_.DispatchEvent(
        mode, key, is_key_down, was_key_down, modifiers);

    if (should_toggle) {
        ToggleTypingMode();
    }
    if (alt_z_event) {
        if (pfEaten) {
            *pfEaten = TRUE;
        }
        return true;
    }

    return mode == HotkeyMode::CtrlShift &&
           (key == HotkeyKey::Control || key == HotkeyKey::Shift);
}

void VietnameseIME::ToggleTypingMode() {
    IMEConfig config = LoadConfigFromRegistry();
    const std::wstring process_name = host_process_name_.empty()
        ? GetFocusedProcessName()
        : host_process_name_;
    const AppInputUpdateResult result = ToggleUserInputMode(
        config, process_name);
    if (!result.changed) {
        return;
    }

    if (!SaveConfigToRegistry(config)) {
        logger::Log(
            logger::Level::Warning,
            L"ToggleTypingMode: failed to persist the requested mode");
        return;
    }
    ReloadConfig();
    logger::LogFormat(
        logger::Level::Info,
        L"ToggleTypingMode: target=%d, effective_mode=%u",
        static_cast<int>(result.target), typing_mode_);
}

} // namespace vn_ime
