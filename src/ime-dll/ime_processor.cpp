#include "ime_processor.hpp"
#include "logger.hpp"
#include "rules.hpp"
#include "speller.hpp"
#include "config.hpp"
#include <inputscope.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <vector>
#include <commctrl.h>
#pragma comment(lib, "comctl32.lib")

extern HINSTANCE g_hInst;

namespace vn_ime {

namespace {

thread_local HHOOK g_msg_hook = nullptr;
thread_local HHOOK g_call_wnd_hook = nullptr;
thread_local HHOOK g_mouse_hook = nullptr;
thread_local VietnameseIME* g_ime_instance = nullptr;
thread_local bool g_in_hook = false;

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
            
            logger::LogFormat(logger::Level::Info, L"MouseHookProc: Mouse message %u detected, committing composition", uMsg);
            g_ime_instance->CommitActiveCompositionFromHook();
        }
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
            
            logger::LogFormat(logger::Level::Info, L"CallWndProc: Message %u detected, committing composition", uMsg);
            g_ime_instance->CommitActiveCompositionFromHook();
        }
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

LRESULT CALLBACK GetMessageHookProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0 && g_ime_instance && !g_in_hook) {
        HookGuard guard;
        MSG* msg = reinterpret_cast<MSG*>(lParam);
        UINT uMsg = msg->message;
        if (uMsg == WM_LBUTTONDOWN || uMsg == WM_RBUTTONDOWN || uMsg == WM_MBUTTONDOWN ||
            uMsg == 0x0246 || // WM_POINTERDOWN
            uMsg == WM_NCLBUTTONDOWN || uMsg == WM_NCRBUTTONDOWN ||
            uMsg == WM_LBUTTONUP || uMsg == 0x0247 || uMsg == WM_NCLBUTTONUP) {
            
            logger::LogFormat(logger::Level::Info, L"GetMessageHookProc: Message %u detected, committing composition", uMsg);
            g_ime_instance->CommitActiveCompositionFromHook();
        }
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

LRESULT CALLBACK InkscapeSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
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

void SecureEraseStringUtf8(std::string& value) {
    if (!value.empty()) {
        SecureZeroMemory(value.data(), value.size() * sizeof(char));
        value.clear();
    }
}

template <typename T>
void SecureEraseVector(std::vector<T>& value) {
    if (!value.empty()) {
        SecureZeroMemory(value.data(), value.size() * sizeof(T));
        value.clear();
    }
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
    VietnameseDisplayAttributeInfo() noexcept : ref_count_(1) {}
    virtual ~VietnameseDisplayAttributeInfo() noexcept = default;

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

    STDMETHODIMP_(ULONG) AddRef() override { return ++ref_count_; }
    STDMETHODIMP_(ULONG) Release() override {
        ULONG count = --ref_count_;
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
    ULONG ref_count_;
};

class VietnameseEnumDisplayAttributeInfo : public IEnumTfDisplayAttributeInfo {
public:
    VietnameseEnumDisplayAttributeInfo() noexcept : ref_count_(1) {}
    virtual ~VietnameseEnumDisplayAttributeInfo() noexcept = default;

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

    STDMETHODIMP_(ULONG) AddRef() override { return ++ref_count_; }
    STDMETHODIMP_(ULONG) Release() override {
        ULONG count = --ref_count_;
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
    ULONG ref_count_;
    ULONG index_ = 0;
};

constexpr size_t kReconversionContextChars = 32;
constexpr size_t kReconversionMaxSelectedChars = 64;

struct ResolvedReconversionTarget {
    ComPtr<ITfRange> range;
    std::wstring word;
    core::rules::ReconversionSpan span;
};

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

    ComPtr<ITfRange> restore_range;
    HRESULT hr = replacement_range->Clone(restore_range.GetAddressOf());
    if (FAILED(hr)) return hr;

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

bool IsReconvertableWord(std::wstring_view word, core::InputMethod method) {
    if (word.empty()) return false;
    std::wstring raw = core::rules::ReconstructRawKeys(word, method);
    core::Engine engine(method);
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
    explicit ReconversionCandidateString(std::wstring text) : text_(std::move(text)) {}
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
    STDMETHODIMP_(ULONG) AddRef() override { return ++ref_count_; }
    STDMETHODIMP_(ULONG) Release() override {
        ULONG count = --ref_count_;
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
    ~ReconversionCandidateString() noexcept { SecureEraseString(text_); }
    ULONG ref_count_ = 1;
    std::wstring text_;
};

class ReconversionCandidateEnumerator final : public IEnumTfCandidates {
public:
    ReconversionCandidateEnumerator(std::wstring text, bool returned = false)
        : text_(std::move(text)), returned_(returned) {}
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
    STDMETHODIMP_(ULONG) AddRef() override { return ++ref_count_; }
    STDMETHODIMP_(ULONG) Release() override {
        ULONG count = --ref_count_;
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
    ~ReconversionCandidateEnumerator() noexcept { SecureEraseString(text_); }
    ULONG ref_count_ = 1;
    std::wstring text_;
    bool returned_ = false;
};

class ReconversionCandidateList final : public ITfCandidateList {
public:
    explicit ReconversionCandidateList(std::wstring text) : text_(std::move(text)) {}
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
    STDMETHODIMP_(ULONG) AddRef() override { return ++ref_count_; }
    STDMETHODIMP_(ULONG) Release() override {
        ULONG count = --ref_count_;
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
    ~ReconversionCandidateList() noexcept { SecureEraseString(text_); }
    ULONG ref_count_ = 1;
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
    DetectTextInputScope,
    DetectEnterReplayScope,
    SelectionIsNonEmpty,
    ReadExcelFormulaPrefix,
    RestoreRaw,
    CommitEscRaw,
};

class EditSession : public ITfEditSession {
public:
    EditSession(VietnameseIME* ime, ITfContext* pic, EditAction action, wchar_t ch = 0, ITfRange* requested_range = nullptr) noexcept
        : ime_(ime), pic_(pic), action_(action), ch_(ch), requested_range_(requested_range), ref_count_(1) {
        if (ime_) ime_->AddRef();
        if (pic_) pic_->AddRef();
    }

    EditSession(VietnameseIME* ime, ITfContext* pic, EditAction action, const std::wstring& str) noexcept
        : ime_(ime), pic_(pic), action_(action), ch_(0), requested_range_(nullptr), ref_count_(1), str_(str) {
        if (ime_) ime_->AddRef();
        if (pic_) pic_->AddRef();
    }

    virtual ~EditSession() noexcept {
        SecureEraseString(result_text_);
        SecureEraseString(str_);
        ch_ = 0;
        if (ime_) ime_->Release();
        if (pic_) pic_->Release();
    }

    bool is_convertible() const noexcept { return is_convertible_; }
    bool action_succeeded() const noexcept { return action_succeeded_; }
    const std::wstring& get_result_text() const noexcept { return result_text_; }
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

    STDMETHODIMP_(ULONG) AddRef() override { return ++ref_count_; }
    STDMETHODIMP_(ULONG) Release() override {
        ULONG count = --ref_count_;
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
                                        if (text_buf == ime_->GetEngine().GetDisplayString()) {
                                            inline_state_valid = true;
                                        } else {
                                            logger::LogFormat(logger::Level::Info, L"Inline validation: text mismatch (len: %zu vs %zu)", text_buf.length(), ime_->GetEngine().GetDisplayString().length());
                                        }
                                    } else {
                                        logger::LogFormat(logger::Level::Info, L"Inline validation: GetText failed or fetched incorrect length %u", fetched_chars);
                                    }
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
            HRESULT hrDirect = ime_->ReplaceDirectInlineText(ec, pic_, range.Get(), disp, old_disp, ch_);
            SecureEraseString(disp);
            SecureEraseString(old_disp);
            action_succeeded_ = SUCCEEDED(hrDirect);
            if (FAILED(hrDirect)) return hrDirect;
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
                                        if (text_buf == ime_->GetEngine().GetDisplayString()) {
                                            inline_state_valid = true;
                                        } else {
                                            logger::LogFormat(logger::Level::Info, L"Inline validation: text mismatch (len: %zu vs %zu) (backspace)", text_buf.length(), ime_->GetEngine().GetDisplayString().length());
                                        }
                                    } else {
                                        logger::LogFormat(logger::Level::Info, L"Inline validation: GetText failed or fetched incorrect length %u (backspace)", fetched_chars);
                                    }
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
                HRESULT hrDirect = ime_->ReplaceDirectInlineText(ec, pic_, range.Get(), disp);
                logger::LogFormat(logger::Level::Info, L"Direct backspace replace returned hr = 0x%08X", hrDirect);
                action_succeeded_ = SUCCEEDED(hrDirect);
                if (raw.empty()) {
                    ime_->ResetDirectInlineState();
                }
                SecureEraseString(raw);
                SecureEraseString(disp);
                if (FAILED(hrDirect)) return hrDirect;
            }
        }
        else if (action_ == EditAction::DirectCommit) {
            logger::LogFormat(logger::Level::Info, L"EditAction::DirectCommit: has_delimiter = %s", ch_ != 0 ? L"TRUE" : L"FALSE");
            ime_->ResetDirectInlineState();
            if (ch_ != 0) {
                wchar_t delim[2] = { ch_, L'\0' };
                HRESULT hrText = range->SetText(ec, 0, delim, 1);
                logger::LogFormat(logger::Level::Info, L"Direct commit SetText returned hr = 0x%08X", hrText);

                range->Collapse(ec, TF_ANCHOR_END);
                sel.range = range.Get();
                sel.style.ase = TF_AE_NONE;
                sel.style.fInterimChar = FALSE;
                HRESULT hrSetSel = pic_->SetSelection(ec, 1, &sel);
                logger::LogFormat(logger::Level::Info, L"Direct commit SetSelection returned hr = 0x%08X", hrSetSel);
            }
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
                
                if (!is_selection_at_composition_end) {
                    logger::Log(logger::Level::Info, L"ProcessChar: Selection is not at composition end, committing active composition first");
                    ime_->EndComposition(ec);
                }
            }

            if (!ime_->HasActiveComposition()) {
                logger::Log(logger::Level::Info, L"No active composition, starting new one");
                ime_->ResetDirectInlineState();
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
                
                if (!is_selection_at_composition_end) {
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
            if (ime_->HasActiveComposition()) {
                ime_->CaptureCommitUndo(ec, pic_);
                HRESULT hrEnd = ime_->EndComposition(ec);
                logger::LogFormat(logger::Level::Info, L"EndComposition returned hr = 0x%08X", hrEnd);
            }
            if (ch_ != 0) {
                ComPtr<ITfRange> current_range;
                TF_SELECTION current_sel;
                ULONG current_fetched = 0;
                HRESULT hrSel = pic_->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &current_sel, &current_fetched);
                logger::LogFormat(logger::Level::Info, L"Commit GetSelection returned hr = 0x%08X, fetched = %u", hrSel, current_fetched);
                if (SUCCEEDED(hrSel) && current_fetched > 0) {
                    current_range.Attach(current_sel.range);
                    wchar_t delim[2] = { ch_, L'\0' };
                    HRESULT hrText = current_range->SetText(ec, 0, delim, 1);
                    logger::LogFormat(logger::Level::Info, L"Commit SetText returned hr = 0x%08X", hrText);
                    
                    current_range->Collapse(ec, TF_ANCHOR_END);
                    current_sel.range = current_range.Get();
                    current_sel.style.ase = TF_AE_NONE;
                    current_sel.style.fInterimChar = FALSE;
                    HRESULT hrSetSel = pic_->SetSelection(ec, 1, &current_sel);
                    logger::LogFormat(logger::Level::Info, L"Commit SetSelection returned hr = 0x%08X", hrSetSel);
                }
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
                        HRESULT hrSet = target.range->SetText(ec, 0, new_word.c_str(), static_cast<LONG>(new_word.length()));
                        HRESULT hrSelection = SUCCEEDED(hrSet)
                            ? RestoreReconversionSelectionAt(ec, pic_, target.range.Get(),
                                                             candidate->selection_start,
                                                             candidate->selection_end)
                            : hrSet;
                        is_convertible_ = SUCCEEDED(hrSet) && SUCCEEDED(hrSelection);
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
                        }
                    }
                }
            }
        }
        else if (action_ == EditAction::RestoreRaw) {
            action_succeeded_ = ime_->TryRestoreLastCommittedRaw(ec, pic_);
        }
        else if (action_ == EditAction::CommitEscRaw) {
            if (ime_->HasActiveComposition()) {
                ComPtr<ITfRange> range;
                if (SUCCEEDED(ime_->active_composition_->GetRange(range.GetAddressOf())) && range) {
                    ime_->UpdateCompositionText(ec, pic_, range.Get(), str_);
                }
                ime_->EndComposition(ec);
            }
            action_succeeded_ = true;
        }
        
        return S_OK;
    }

private:
    VietnameseIME* ime_;
    ITfContext* pic_;
    EditAction action_;
    wchar_t ch_;
    ComPtr<ITfRange> requested_range_;
    ULONG ref_count_;
    bool is_convertible_ = false;
    bool action_succeeded_ = false;
    std::wstring result_text_;
    ComPtr<ITfRange> result_range_;
    std::wstring str_;
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
}

VietnameseIME::~VietnameseIME() noexcept {
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
    return ++ref_count_;
}

STDMETHODIMP_(ULONG) VietnameseIME::Release() {
    ULONG count = --ref_count_;
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
    ClearLastCommitUndo();
    if (!is_active_) return S_OK;
    
    // Check for auto-exclude on layout switch to ENG
    if (enable_auto_exclude_) {
        HWND fg_hwnd = ::GetForegroundWindow();
        if (fg_hwnd) {
            DWORD fg_pid = 0;
            DWORD fg_tid = ::GetWindowThreadProcessId(fg_hwnd, &fg_pid);
            if (fg_pid == ::GetCurrentProcessId() && fg_tid == ::GetCurrentThreadId()) {
                wchar_t path[MAX_PATH] = {0};
                if (::GetModuleFileNameW(nullptr, path, MAX_PATH) != 0) {
                    std::wstring process_name = NormalizeProcessName(path);
                    if (!process_name.empty()) {
                        bool already_blocked = false;
                        for (const auto& app : blocked_apps_) {
                            if (app == process_name) {
                                already_blocked = true;
                                break;
                            }
                        }
                        if (!already_blocked) {
                            logger::LogFormat(logger::Level::Info, L"Auto-excluding app on layout switch: %s", process_name.c_str());
                            IMEConfig config = LoadConfigFromRegistry();
                            std::wstring norm_name = NormalizeProcessName(process_name);
                            bool in_config_blocked = false;
                            for (const auto& app : config.blocked_apps) {
                                if (app == norm_name) {
                                    in_config_blocked = true;
                                    break;
                                }
                            }
                            if (!in_config_blocked) {
                                config.blocked_apps.push_back(norm_name);
                                config.auto_blocked_apps.push_back(norm_name);
                                SaveConfigToRegistry(config);
                            }
                        }
                    }
                }
            }
        }
    }
    
    // Shut down registry watcher thread
    if (registry_thread_) {
        if (registry_shutdown_event_) {
            SetEvent(registry_shutdown_event_);
        }
        // Wait up to 2 seconds for the thread to exit cleanly
        WaitForSingleObject(registry_thread_, 2000);
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

    if (g_msg_hook) {
        ::UnhookWindowsHookEx(g_msg_hook);
        g_msg_hook = nullptr;
        logger::Log(logger::Level::Info, L"Deactivate: Removed Thread-local GetMessage hook");
    }
    if (g_call_wnd_hook) {
        ::UnhookWindowsHookEx(g_call_wnd_hook);
        g_call_wnd_hook = nullptr;
        logger::Log(logger::Level::Info, L"Deactivate: Removed Thread-local CallWndProc hook");
    }
    if (g_mouse_hook) {
        ::UnhookWindowsHookEx(g_mouse_hook);
        g_mouse_hook = nullptr;
        logger::Log(logger::Level::Info, L"Deactivate: Removed Thread-local Mouse hook");
    }
    g_ime_instance = nullptr;

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

    // Check for auto-include on layout switch back to Vietnamese
    IMEConfig initial_config = LoadConfigFromRegistry();
    if (initial_config.enable_auto_exclude) {
        HWND fg_hwnd = ::GetForegroundWindow();
        if (fg_hwnd) {
            DWORD fg_pid = 0;
            DWORD fg_tid = ::GetWindowThreadProcessId(fg_hwnd, &fg_pid);
            if (fg_pid == ::GetCurrentProcessId() && fg_tid == ::GetCurrentThreadId()) {
                wchar_t path[MAX_PATH] = {0};
                if (::GetModuleFileNameW(nullptr, path, MAX_PATH) != 0) {
                    std::wstring process_name = NormalizeProcessName(path);
                    if (!process_name.empty()) {
                        bool in_auto_blocked = false;
                        for (auto it = initial_config.auto_blocked_apps.begin(); it != initial_config.auto_blocked_apps.end(); ++it) {
                            if (*it == process_name) {
                                in_auto_blocked = true;
                                initial_config.auto_blocked_apps.erase(it);
                                break;
                            }
                        }
                        if (in_auto_blocked) {
                            logger::LogFormat(logger::Level::Info, L"Removing auto-excluded app on layout switch back to VIE: %s", process_name.c_str());
                            for (auto it = initial_config.blocked_apps.begin(); it != initial_config.blocked_apps.end(); ++it) {
                                if (*it == process_name) {
                                    initial_config.blocked_apps.erase(it);
                                    break;
                                }
                            }
                            SaveConfigToRegistry(initial_config);
                        }
                    }
                }
            }
        }
    }

    // Load initial config
    ReloadConfig();

    // Set up registry monitoring (skip on Secure Desktop)
    if (!logger::IsSecureDesktop()) {
        registry_shutdown_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        registry_watch_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (registry_shutdown_event_ && registry_watch_event_) {
            registry_thread_ = CreateThread(nullptr, 0, RegistryWatchThreadProc, this, 0, nullptr);
            if (!registry_thread_) {
                logger::Log(logger::Level::Error, L"ActivateEx: Failed to create Registry watch thread");
            }
        } else {
            logger::Log(logger::Level::Error, L"ActivateEx: Failed to create Registry watch events");
        }
    }

    logger::Log(logger::Level::Info, L"VietnameseIME::ActivateEx succeeded.");
    EnsureInkscapeSubclassed();
    return S_OK;
}

// ITfKeyEventSink Implementation
STDMETHODIMP VietnameseIME::OnSetFocus(BOOL fForeground) {
    logger::LogFormat(logger::Level::Info, L"OnSetFocus called: fForeground = %s", fForeground ? L"TRUE" : L"FALSE");
    if (fForeground) {
        EnsureInkscapeSubclassed();
    }
    if (!fForeground && thread_mgr_) {
        ComPtr<ITfDocumentMgr> doc_mgr;
        if (SUCCEEDED(thread_mgr_->GetFocus(doc_mgr.GetAddressOf())) && doc_mgr) {
            ComPtr<ITfContext> context;
            if (SUCCEEDED(doc_mgr->GetTop(context.GetAddressOf())) && context) {
                CommitCompositionSync(context.Get());
            }
        }
        ClearSensitiveState(false);
    }
    return S_OK;
}

STDMETHODIMP VietnameseIME::OnTestKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) {
    if (!pfEaten) return E_INVALIDARG;

    if (HasTextShortcutModifier()) {
        if (active_composition_) {
            logger::Log(logger::Level::Info, L"OnTestKeyDown: Shortcut modifier detected, committing active composition");
            CommitCompositionSync(pic);
        }
        *pfEaten = FALSE;
        return S_OK;
    }

    CheckAndReloadConfig();
    EnsureInkscapeSubclassed();

    if (::GetMessageExtraInfo() == 0xDEADC0DE || (lParam & (1 << 28)) != 0) {
        *pfEaten = FALSE;
        return S_OK;
    }

    BOOL hotkeyEaten = FALSE;
    TrackHotkey(wParam, lParam, true, &hotkeyEaten);
    if (hotkeyEaten) {
        *pfEaten = TRUE;
        return S_OK;
    }

    if (IsInkscapeApp()) {
        if (wParam == VK_SPACE || wParam == VK_RETURN) {
            if (active_composition_) {
                last_inkscape_commit_vk_ = wParam;
                last_inkscape_commit_time_ = ::GetTickCount();
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
            logger::Log(logger::Level::Info, L"OnKeyDown: Esc detected with direct inline state, committing raw keys");
            TryProcessDirectCommitEsc();
            *pfEaten = TRUE;
            return S_OK;
        } else if (last_commit_undo_) {
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
                restored = TryRestoreLastCommittedRawDirectInline(last_commit_undo_->hwnd);
            }

            if (restored) {
                *pfEaten = TRUE;
                return S_OK;
            } else {
                ClearLastCommitUndo();
            }
        }
    } else {
        if (!IsModifierKey(wParam)) {
            ClearLastCommitUndo();
        }
    }

    CheckAndReloadConfig();
    EnsureInkscapeSubclassed();

    if (composition_commit_pending_ && active_composition_) {
        logger::Log(logger::Level::Info, L"OnKeyDown: Pending selection commit detected, committing composition synchronously first");
        pending_commit_caret_policy_ = CommitCaretPolicy::PreserveHostSelection;
        CommitCompositionSync(pic);
        composition_commit_pending_ = false;
    }

    if (::GetMessageExtraInfo() == 0xDEADC0DE || (lParam & (1 << 28)) != 0) {
        *pfEaten = FALSE;
        return S_OK;
    }

    BOOL hotkeyEaten = FALSE;
    TrackHotkey(wParam, lParam, true, &hotkeyEaten);
    if (hotkeyEaten) {
        *pfEaten = TRUE;
        return S_OK;
    }

    if (IsInkscapeApp()) {
        if (wParam == VK_SPACE || wParam == VK_RETURN) {
            if (active_composition_) {
                last_inkscape_commit_vk_ = wParam;
                last_inkscape_commit_time_ = ::GetTickCount();
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
        if (decision.replay_native_after_commit) {
            pending_commit_caret_policy_ = CommitCaretPolicy::MoveToCompositionEnd;
        }
        CommitCompositionSync(pic);
    }

    if (decision.clear_sensitive_before_host) {
        ClearSensitiveState(false);
    }

    if (decision.replay_native_after_commit && decision.replay_vk != 0) {
        SendSyntheticNativeKey(decision.replay_vk);
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
                    HRESULT hr = 0;
                    HRESULT hrReq = pic->RequestEditSession(client_id_, session.Get(), TF_ES_SYNC | TF_ES_READWRITE, &hr);
                    logger::LogFormat(logger::Level::Info, L"RequestEditSession (Direct Backspace) returned hrReq = 0x%08X, hr = 0x%08X", hrReq, hr);
                    if (FAILED(hrReq) || FAILED(hr) || !session->action_succeeded()) {
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
                ComPtr<ITfEditSession> session;
                session.Attach(new (std::nothrow) EditSession(this, pic, EditAction::DirectCommit, L' '));
                if (session) {
                    HRESULT hr = 0;
                    HRESULT hrReq = pic->RequestEditSession(client_id_, session.Get(), TF_ES_SYNC | TF_ES_READWRITE, &hr);
                    logger::LogFormat(logger::Level::Info, L"RequestEditSession (Direct Commit Space) returned hrReq = 0x%08X, hr = 0x%08X", hrReq, hr);
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
                        HRESULT hr = 0;
                        HRESULT hrReq = pic->RequestEditSession(client_id_, session.Get(), TF_ES_SYNC | TF_ES_READWRITE, &hr);
                        logger::LogFormat(logger::Level::Info, L"RequestEditSession (Direct Process Char) returned hrReq = 0x%08X, hr = 0x%08X", hrReq, hr);
                        if (FAILED(hrReq) || FAILED(hr) || !session->action_succeeded()) {
                            ResetDirectInlineState();
                            *pfEaten = FALSE;
                        }
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

    return S_OK;
}

STDMETHODIMP VietnameseIME::OnTestKeyUp([[maybe_unused]] ITfContext* pic, [[maybe_unused]] WPARAM wParam, [[maybe_unused]] LPARAM lParam, BOOL* pfEaten) {
    if (!pfEaten) return E_INVALIDARG;
    CheckAndReloadConfig();
    if (::GetMessageExtraInfo() == 0xDEADC0DE || (lParam & (1 << 28)) != 0) {
        *pfEaten = FALSE;
        return S_OK;
    }
    BOOL hotkeyEaten = FALSE;
    TrackHotkey(wParam, lParam, false, &hotkeyEaten);
    *pfEaten = hotkeyEaten ? TRUE : FALSE;
    return S_OK;
}

STDMETHODIMP VietnameseIME::OnKeyUp([[maybe_unused]] ITfContext* pic, [[maybe_unused]] WPARAM wParam, [[maybe_unused]] LPARAM lParam, BOOL* pfEaten) {
    if (!pfEaten) return E_INVALIDARG;
    CheckAndReloadConfig();
    if (::GetMessageExtraInfo() == 0xDEADC0DE || (lParam & (1 << 28)) != 0) {
        *pfEaten = FALSE;
        return S_OK;
    }
    BOOL hotkeyEaten = FALSE;
    TrackHotkey(wParam, lParam, false, &hotkeyEaten);
    *pfEaten = hotkeyEaten ? TRUE : FALSE;
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
                if (IsValidCompositionKey(wParam, engine_.GetInputMethod())) {
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
            if (IsValidCompositionKey(wParam, engine_.GetInputMethod())) {
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
        if (has_composition) {
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
            if (IsValidCompositionKey(wParam, engine_.GetInputMethod())) {
                decision.ch = TranslateKey(wParam, lParam);
                if (decision.ch != 0) {
                    if (!has_word_inline) {
                        decision.action = KeyAction::Reconvert;
                        decision.fallback_to_direct_process_char = true;
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

        if (!HasTextShortcutModifier() && IsValidCompositionKey(wParam, engine_.GetInputMethod())) {
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
            if (IsValidCompositionKey(wParam, engine_.GetInputMethod())) {
                decision.ch = TranslateKey(wParam, lParam);
                if (decision.ch != 0) {
                    if (!has_direct_inline) {
                        decision.action = KeyAction::ExplorerEditReconvert;
                        decision.fallback_to_direct_process_char = true;
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
        decision.action = KeyAction::Reconvert;
        decision.fallback_to_process_char = true;
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

    return SUCCEEDED(hrReq) && SUCCEEDED(hr) && session->is_convertible();
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

bool VietnameseIME::IsCurrentAppBlocked(ITfContext* pic) const {
    if (!enable_app_blocklist_ || blocked_apps_.empty()) {
        return false;
    }

    std::wstring process_name = GetFocusedProcessName();
    if (process_name.empty()) {
        return false;
    }

    for (const auto& blocked_app : blocked_apps_) {
        if (blocked_app == process_name) {
            return true;
        }
    }
    return false;
}

bool VietnameseIME::IsDirectCommitApp() const {
    return IsExplorerWin32EditFocused();
}

bool VietnameseIME::IsTerminalApp() const {
    std::wstring process_name = GetFocusedProcessName();
    return (process_name == L"windowsterminal.exe" ||
            process_name == L"openconsole.exe" ||
            process_name == L"powershell.exe" ||
            process_name == L"pwsh.exe" ||
            process_name == L"cmd.exe" ||
            process_name == L"conhost.exe" ||
            process_name == L"anydesk.exe" ||
            process_name == L"pymol.exe" ||
            process_name == L"mintty.exe");
}

bool VietnameseIME::IsFakeBackspaceApp() const {
    if (IsVisualStudioProcess() || IsTerminalApp()) {
        return true;
    }
    return false;
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
    return GetFocusedProcessName() == L"telegram.exe";
}

bool VietnameseIME::IsVisualStudioProcess() const {
    return GetFocusedProcessName() == L"devenv.exe";
}

bool VietnameseIME::IsExcelApp() const {
    return GetFocusedProcessName() == L"excel.exe";
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
    ComPtr<IUnknown> identity;
    if (SUCCEEDED(pic->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(identity.GetAddressOf())))) {
        if (identity.Get() == excel_formula_context_identity_.Get()) {
            return excel_formula_state_;
        }
    }
    return core::ExcelFormulaSessionState::Idle;
}

void VietnameseIME::PrepareExcelFormulaSession(ITfContext* pic, WPARAM wParam, LPARAM lParam) {
    if (!pic || !IsExcelApp() || HasTextShortcutModifier()) {
        return;
    }

    const auto probe = GetExcelFormulaInputKind(pic);
    if (probe.has_value()) {
        core::ExcelFormulaSessionState next_state = core::ExcelFormulaSessionState::Idle;
        if (probe.value() == core::ExcelFormulaInputKind::FormulaSyntax) {
            next_state = core::ExcelFormulaSessionState::FormulaSyntax;
        } else if (probe.value() == core::ExcelFormulaInputKind::QuotedText) {
            next_state = core::ExcelFormulaSessionState::QuotedText;
        } else if (probe.value() == core::ExcelFormulaInputKind::NotFormula) {
            if (excel_formula_state_ == core::ExcelFormulaSessionState::PendingFormulaStart) {
                next_state = core::ExcelFormulaSessionState::PendingFormulaStart;
            } else {
                next_state = core::ExcelFormulaSessionState::Idle;
            }
        }
        
        if (next_state != excel_formula_state_) {
            SetExcelFormulaSessionState(pic, next_state, L"probe_sync");
        }
    }
}

bool VietnameseIME::TryAdoptPendingExcelFormulaContext(ITfContext* pic) {
    if (!pic ||
        !IsExcelApp() ||
        excel_formula_state_ != core::ExcelFormulaSessionState::PendingFormulaStart) {
        return false;
    }

    const core::ExcelFormulaSessionState adopted =
        core::AdoptPendingExcelFormulaSession(excel_formula_state_);
    SetExcelFormulaSessionState(pic, adopted, L"context_handoff");
    return GetExcelFormulaSessionState(pic) == core::ExcelFormulaSessionState::FormulaSyntax;
}

void VietnameseIME::ObserveExcelNativeChar(
    ITfContext* pic,
    WPARAM wParam,
    LPARAM lParam,
    const wchar_t* source) {
    if (!pic || !IsExcelApp() || HasTextShortcutModifier()) {
        return;
    }

    const wchar_t ch = TranslateKey(wParam, lParam);
    if (ch != L'=' && ch != L'"') {
        return;
    }

    ObserveExcelNativeChar(pic, ch, source);
}

void VietnameseIME::ObserveExcelNativeChar(
    ITfContext* pic,
    wchar_t ch,
    const wchar_t* source) {
    if (!pic || !IsExcelApp() || (ch != L'=' && ch != L'"')) {
        return;
    }

    const core::ExcelFormulaSessionState current = GetExcelFormulaSessionState(pic);
    const core::ExcelFormulaSessionState next =
        core::AdvanceExcelFormulaSessionState(current, ch);
    if (next != current) {
        SetExcelFormulaSessionState(pic, next, source);
    }
}

void VietnameseIME::SetExcelFormulaSessionState(ITfContext* pic, core::ExcelFormulaSessionState state, const wchar_t* source) {
    if (pic && IsExcelApp()) {
        logger::LogFormat(logger::Level::Info, L"SetExcelFormulaSessionState: old_state=%d, new_state=%d, source=%s",
                          static_cast<int>(excel_formula_state_), static_cast<int>(state), source);
    }
    excel_formula_state_ = state;
    if (pic && state != core::ExcelFormulaSessionState::Idle) {
        pic->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(excel_formula_context_identity_.ReleaseAndGetAddressOf()));
    } else {
        excel_formula_context_identity_.Reset();
    }
}

void VietnameseIME::ResetExcelFormulaSession(const wchar_t* reason) noexcept {
    if (excel_formula_state_ != core::ExcelFormulaSessionState::Idle) {
        logger::LogFormat(logger::Level::Info, L"ResetExcelFormulaSession: old_state=%d -> Idle, reason=%s",
                          static_cast<int>(excel_formula_state_), reason);
    }
    excel_formula_state_ = core::ExcelFormulaSessionState::Idle;
    excel_formula_context_identity_.Reset();
}

bool VietnameseIME::IsWordTsfInlineApp() const {
    return GetFocusedProcessName() == L"winword.exe";
}

bool VietnameseIME::IsInkscapeApp() const {
    return GetFocusedProcessName() == L"inkscape.exe";
}

bool VietnameseIME::IsInkscapeKeySuppressed(WPARAM wParam) const {
    if (wParam == last_inkscape_commit_vk_) {
        if (::GetTickCount() - last_inkscape_commit_time_ < 100) {
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
    return GetFocusedProcessName() == L"explorer.exe";
}

bool VietnameseIME::IsExplorerWin32EditFocused() const {
    std::wstring process_name = GetFocusedProcessName();
    if (process_name != L"explorer.exe" && process_name != L"filezilla.exe") {
        return false;
    }

    HWND hwnd = GetBestFocusWindow();
    if (!hwnd) {
        return false;
    }

    std::wstring class_name = GetClassNameOrEmpty(hwnd);
    return _wcsicmp(class_name.c_str(), L"Edit") == 0;
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
    return ShouldUseNotepadPlusPlusDirectInline(GetFocusedProcessName(), GetClassNameOrEmpty(GetBestFocusWindow()));
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

    CaptureCommitUndoDirectInline(hwnd, false);
    ResetDirectInlineState();
    wchar_t text[2] = { ch, L'\0' };
    ::SendMessageW(hwnd, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(text));
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
        engine_.Clear();
        direct_inline_display_length_ = 0;
        scintilla_direct_inline_byte_length_ = 0;
        scintilla_direct_inline_start_ = 0;
    }

    engine_.ProcessKey(ch);
    std::wstring display = engine_.GetDisplayString();

    std::string display_utf8;
    if (!display.empty()) {
        int len = WideCharToMultiByte(CP_UTF8, 0, display.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (len > 0) {
            display_utf8.resize(len - 1);
            WideCharToMultiByte(CP_UTF8, 0, display.c_str(), -1, display_utf8.data(), len, nullptr, nullptr);
        }
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
    if (!display.empty()) {
        int len = WideCharToMultiByte(CP_UTF8, 0, display.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (len > 0) {
            display_utf8.resize(len - 1);
            WideCharToMultiByte(CP_UTF8, 0, display.c_str(), -1, display_utf8.data(), len, nullptr, nullptr);
        }
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

    constexpr LRESULT SCI_REPLACESEL = 2170;

    CaptureCommitUndoDirectInline(hwnd, true);
    ResetDirectInlineState();
    std::wstring text_ws(1, ch);
    std::string text_utf8;
    int len = WideCharToMultiByte(CP_UTF8, 0, text_ws.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (len > 0) {
        text_utf8.resize(len - 1);
        WideCharToMultiByte(CP_UTF8, 0, text_ws.c_str(), -1, text_utf8.data(), len, nullptr, nullptr);
    }
    ::SendMessageW(hwnd, SCI_REPLACESEL, 0, reinterpret_cast<LPARAM>(text_utf8.c_str()));
    SecureEraseString(text_ws);
    SecureEraseStringUtf8(text_utf8);
    return true;
}

bool VietnameseIME::ProcessFakeBackspaceEditChar(wchar_t ch) {
    if (ch == 0 || IsSecureInputContext()) {
        return false;
    }

    std::wstring old_display = engine_.GetDisplayString();

    const bool can_replace_previous =
        direct_inline_display_length_ > 0;

    if (!can_replace_previous) {
        engine_.Clear();
        direct_inline_display_length_ = 0;
        old_display.clear();
    }

    engine_.ProcessKey(ch);
    std::wstring display = engine_.GetDisplayString();
    if (display.empty()) {
        SecureEraseString(display);
        SecureEraseString(old_display);
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
    for (size_t i = 0; i < backspaces_to_send; ++i) {
        SendSyntheticNativeKey(VK_BACK);
    }

    std::wstring new_chars = display.substr(common_len);
    for (wchar_t wch : new_chars) {
        SendSyntheticUnicodeChar(wch);
    }

    direct_inline_display_length_ = display.length();
    SecureEraseString(display);
    SecureEraseString(old_display);
    return true;
}

bool VietnameseIME::ProcessFakeBackspaceEditBackspace() {
    if (IsSecureInputContext() || !HasDirectInlineState()) {
        return false;
    }

    std::wstring old_display = engine_.GetDisplayString();

    engine_.BackspaceDisplayChar();
    std::wstring raw = engine_.GetRawString();
    std::wstring display = engine_.GetDisplayString();

    size_t common_len = 0;
    size_t max_len = (std::min)(old_display.length(), display.length());
    while (common_len < max_len && old_display[common_len] == display[common_len]) {
        common_len++;
    }

    size_t backspaces_to_send = old_display.length() - common_len;
    for (size_t i = 0; i < backspaces_to_send; ++i) {
        SendSyntheticNativeKey(VK_BACK);
    }

    std::wstring new_chars = display.substr(common_len);
    for (wchar_t wch : new_chars) {
        SendSyntheticUnicodeChar(wch);
    }

    if (raw.empty() || display.empty()) {
        ResetDirectInlineState();
    } else {
        direct_inline_display_length_ = display.length();
    }

    SecureEraseString(old_display);
    SecureEraseString(raw);
    SecureEraseString(display);
    return true;
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
    engine_.SecureClear();
    direct_inline_display_length_ = 0;
    scintilla_direct_inline_byte_length_ = 0;
    scintilla_direct_inline_start_ = 0;
    pending_commit_caret_policy_ = CommitCaretPolicy::MoveToCompositionEnd;
    mouse_commit_pending_ = false;
    composition_commit_pending_ = false;
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
        is_updating_selection_ = false;
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
                if (!comp_text.empty() && comp_text != current_display && comp_text.length() == current_display.length()) {
                    bool casing_only = true;
                    for (size_t i = 0; i < comp_text.length(); ++i) {
                        if (towlower(comp_text[i]) != towlower(current_display[i])) {
                            casing_only = false;
                            break;
                        }
                    }
                    if (casing_only) {
                        logger::LogFormat(logger::Level::Info,
                                          L"OnEndEdit: Casing change detected. host_len=%zu, current_len=%zu",
                                          comp_text.length(), current_display.length());
                        engine_.UpdateCasingFromHost(comp_text);
                    }
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

void VietnameseIME::ResetDirectInlineState() noexcept {
    engine_.SecureClear();
    direct_inline_display_length_ = 0;
    scintilla_direct_inline_byte_length_ = 0;
    scintilla_direct_inline_start_ = 0;
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
    if (IsInkscapeApp()) {
        HWND hwnd = GetBestFocusWindow();
        if (hwnd) {
            SHORT vkState = ::VkKeyScanW(ch);
            UINT scanCode = 0;
            if (vkState != -1) {
                scanCode = ::MapVirtualKeyW(LOBYTE(vkState), MAPVK_VK_TO_VSC);
            }
            LPARAM charLParam = 1 | (scanCode << 16) | (1 << 28);
            ::PostMessageW(hwnd, WM_CHAR, ch, charLParam);
            return;
        }
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


HRESULT VietnameseIME::ReplaceDirectInlineText(TfEditCookie ec, ITfContext* pic, ITfRange* caret_range, const std::wstring& text, const std::wstring& old_text, wchar_t ch) {
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
        return S_OK;
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

    replace_range->Collapse(ec, TF_ANCHOR_END);
    TF_SELECTION new_sel;
    new_sel.range = replace_range.Get();
    new_sel.style.ase = TF_AE_NONE;
    new_sel.style.fInterimChar = FALSE;
    hr = pic->SetSelection(ec, 1, &new_sel);
    if (SUCCEEDED(hr)) {
        direct_inline_display_length_ = text.length();
    }
    return hr;
}

bool VietnameseIME::IsSecureInputContext() const noexcept {
    if (logger::IsSecureDesktop()) {
        return true;
    }

    if (is_password_field_) {
        return true;
    }

    HWND hwnd = GetBestFocusWindow();
    if (!hwnd) {
        return true;
    }

    LONG_PTR style = ::GetWindowLongPtrW(hwnd, GWL_STYLE);
    if ((style & ES_PASSWORD) != 0) {
        return true;
    }

    return ::SendMessageW(hwnd, EM_GETPASSWORDCHAR, 0, 0) != 0;
}

bool VietnameseIME::IsKeyFiltered(WPARAM wParam, [[maybe_unused]] LPARAM lParam) const noexcept {
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
    ClearLastCommitUndo();
    is_password_field_ = false;

    if (pdmPrevFocus) {
        ComPtr<ITfContext> context;
        if (SUCCEEDED(pdmPrevFocus->GetTop(context.GetAddressOf())) && context) {
            CommitCompositionSync(context.Get());
        }
        ClearSensitiveState(false);
    }

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
    ClearSensitiveState(false);
    if (pic && IsExcelApp()) {
        ComPtr<IUnknown> identity;
        if (SUCCEEDED(pic->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(identity.GetAddressOf())))) {
            if (identity.Get() == excel_formula_context_identity_.Get()) {
                ResetExcelFormulaSession(L"context_popped");
            }
        }
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

    if (g_msg_hook) {
        ::UnhookWindowsHookEx(g_msg_hook);
        g_msg_hook = nullptr;
        logger::Log(logger::Level::Info, L"OnCompositionTerminated: Removed Thread-local GetMessage hook");
    }
    if (g_call_wnd_hook) {
        ::UnhookWindowsHookEx(g_call_wnd_hook);
        g_call_wnd_hook = nullptr;
        logger::Log(logger::Level::Info, L"OnCompositionTerminated: Removed Thread-local CallWndProc hook");
    }
    if (g_mouse_hook) {
        ::UnhookWindowsHookEx(g_mouse_hook);
        g_mouse_hook = nullptr;
        logger::Log(logger::Level::Info, L"OnCompositionTerminated: Removed Thread-local Mouse hook");
    }
    g_ime_instance = nullptr;

    UnadviseSelectionSink();

    ClearSensitiveState(true);
    return S_OK;
}

// Composition management helper methods
HRESULT VietnameseIME::StartComposition(TfEditCookie ec, ITfContext* pic, ITfRange* range) {
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
    if (FAILED(hr)) return hr;

    hr = context_comp->StartComposition(ec, cloned_range.Get(), static_cast<ITfCompositionSink*>(this), active_composition_.ReleaseAndGetAddressOf());
    logger::LogFormat(logger::Level::Info, L"StartComposition: context_comp->StartComposition returned hr = 0x%08X", hr);
    
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

HRESULT VietnameseIME::EndComposition(TfEditCookie ec) {
    if (!active_composition_) return S_OK;

    const bool secure_input = IsSecureInputContext();
    CommitCaretPolicy caret_policy = pending_commit_caret_policy_;

    if (caret_policy == CommitCaretPolicy::MoveToCompositionEnd) {
        // If the current selection (caret) is not at the end of the composition,
        // it means the user has moved the selection (e.g. by clicking or using arrow keys).
        // In this case, we must preserve the host's selection and not force it to the composition end.
        bool selection_at_end = false;
        ComPtr<ITfRange> comp_range;
        if (SUCCEEDED(active_composition_->GetRange(comp_range.GetAddressOf())) && comp_range) {
            ComPtr<ITfContext> context;
            if (SUCCEEDED(comp_range->GetContext(context.GetAddressOf())) && context) {
                TF_SELECTION sel{};
                ULONG fetched = 0;
                if (SUCCEEDED(context->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &sel, &fetched)) && fetched > 0) {
                    ComPtr<ITfRange> sel_range;
                    sel_range.Attach(sel.range);
                    BOOL sel_empty = TRUE;
                    if (SUCCEEDED(sel_range->IsEmpty(ec, &sel_empty)) && sel_empty) {
                        LONG comparison = 0;
                        if (SUCCEEDED(sel_range->CompareStart(ec, comp_range.Get(), TF_ANCHOR_END, &comparison)) && comparison == 0) {
                            selection_at_end = true;
                        }
                    }
                }
            }
        }
        if (!selection_at_end) {
            caret_policy = CommitCaretPolicy::PreserveHostSelection;
        }
    }

    // Apply shorthand expansion if enabled
    IMEConfig config = LoadConfigFromRegistry();
    if (!secure_input && config.enable_shorthand && !shorthand_map_.empty()) {
        ComPtr<ITfRange> comp_range;
        if (SUCCEEDED(active_composition_->GetRange(comp_range.GetAddressOf())) && comp_range) {
            wchar_t buf[256] = {0};
            ULONG fetched_chars = 0;
            comp_range->GetText(ec, 0, buf, 255, &fetched_chars);
            if (fetched_chars > 0) {
                std::wstring comp_text(buf, fetched_chars);
                std::wstring expanded = LookUpShorthand(comp_text);
                if (expanded != comp_text) {
                    comp_range->SetText(ec, 0, expanded.c_str(), static_cast<LONG>(expanded.length()));
                }
                SecureEraseString(comp_text);
                SecureEraseString(expanded);
            }
            SecureEraseBuffer(buf, 256);
        }
    }

    // Apply auto-capitalization if enabled
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
                            comp_text[0] = core::rules::ToUpper(comp_text[0]);
                            comp_range->SetText(ec, 0, comp_text.c_str(), static_cast<LONG>(comp_text.length()));
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

    if (caret_policy == CommitCaretPolicy::MoveToCompositionEnd) {
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

    if (g_msg_hook) {
        ::UnhookWindowsHookEx(g_msg_hook);
        g_msg_hook = nullptr;
        logger::Log(logger::Level::Info, L"EndComposition: Removed Thread-local GetMessage hook");
    }
    if (g_call_wnd_hook) {
        ::UnhookWindowsHookEx(g_call_wnd_hook);
        g_call_wnd_hook = nullptr;
        logger::Log(logger::Level::Info, L"EndComposition: Removed Thread-local CallWndProc hook");
    }
    if (g_mouse_hook) {
        ::UnhookWindowsHookEx(g_mouse_hook);
        g_mouse_hook = nullptr;
        logger::Log(logger::Level::Info, L"EndComposition: Removed Thread-local Mouse hook");
    }
    g_ime_instance = nullptr;

    UnadviseSelectionSink();

    HRESULT hr = active_composition_->EndComposition(ec);
    active_composition_.Reset();
    ClearSensitiveState(false);
    return hr;
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

void VietnameseIME::CommitCompositionAsync(ITfContext* pic) {
    if (!active_composition_) return;
    
    ComPtr<ITfEditSession> session;
    session.Attach(new (std::nothrow) EditSession(this, pic, EditAction::Commit));
    if (session) {
        HRESULT hr = 0;
        pic->RequestEditSession(client_id_, session.Get(), TF_ES_ASYNCDONTCARE | TF_ES_READWRITE, &hr);
    }
}

void VietnameseIME::CommitCompositionSync(ITfContext* pic) {
    if (!active_composition_) return;
    
    ComPtr<ITfEditSession> session;
    session.Attach(new (std::nothrow) EditSession(this, pic, EditAction::Commit));
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
    
    // If a button click occurred (left, right, or middle mouse button)
    // We should commit the active composition.
    if ((dwBtnStatus & (MK_LBUTTON | MK_RBUTTON | MK_MBUTTON)) != 0) {
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
    BYTE keyboardState[256];
    if (GetKeyboardState(keyboardState)) {
        wchar_t buf[4] = {0};
        UINT scanCode = (lParam >> 16) & 0xFF;
        int count = ToUnicode(static_cast<UINT>(wParam), scanCode, keyboardState, buf, 4, 0);
        if (count > 0) {
            ch = buf[0];
        }
    }
    if (ch == 0 && wParam >= 0x60 && wParam <= 0x69) {
        if ((::GetKeyState(VK_NUMLOCK) & 0x0001) != 0) {
            ch = L'0' + (static_cast<wchar_t>(wParam) - 0x60);
        }
    }
    return ch;
}

void VietnameseIME::ReloadConfig() {
    IMEConfig config = LoadConfigFromRegistry();
    logger::SetEnabled(config.enable_log);
    logger::Log(logger::Level::Info, L"VietnameseIME::ReloadConfig loading configuration...");
    engine_.SetInputMethod(config.input_method);
    engine_.SetCorrectionLevel(config.auto_correct_level);
    enable_app_blocklist_ = config.enable_app_blocklist;
    blocked_apps_ = NormalizeProcessList(config.blocked_apps);
    enable_auto_exclude_ = config.enable_auto_exclude;
    auto_blocked_apps_ = NormalizeProcessList(config.auto_blocked_apps);
    cached_process_id_ = 0;
    cached_process_name_.clear();

    DWORD old_typing_mode = typing_mode_;
    
    // Determine typing mode for current process
    std::wstring process_name = GetFocusedProcessName();
    DWORD mode = config.typing_mode;
    if (!process_name.empty()) {
        HKEY hKey;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_KEY_PATH, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            std::wstring val_name = L"AppTypingMode_" + process_name;
            DWORD dwValue = 0;
            DWORD cbData = sizeof(dwValue);
            if (RegQueryValueExW(hKey, val_name.c_str(), nullptr, nullptr, reinterpret_cast<BYTE*>(&dwValue), &cbData) == ERROR_SUCCESS) {
                mode = dwValue;
            }
            RegCloseKey(hKey);
        }
    }
    typing_mode_ = mode;
    hotkey_mode_ = config.hotkey_mode;

    bool is_transition = config_loaded_once_ && (old_typing_mode != typing_mode_);
    config_loaded_once_ = true;

    if (enable_auto_exclude_ && is_transition) {
        std::wstring process_name = GetFocusedProcessName();
        if (!process_name.empty()) {
            bool changed = false;
            if (typing_mode_ == 1) { // Transition to English
                changed = AutoExcludeApp(config, process_name);
            } else { // Transition to Vietnamese
                changed = AutoIncludeApp(config, process_name);
            }
            if (changed) {
                SaveConfigToRegistry(config);
                // Update local lists immediately
                blocked_apps_ = NormalizeProcessList(config.blocked_apps);
                auto_blocked_apps_ = NormalizeProcessList(config.auto_blocked_apps);
                TouchConfigRevision();
            }
        }
    }

    // Load shorthand rules
    LoadShorthandRules();

    logger::LogFormat(logger::Level::Info, L"Config loaded: input_method = %d, auto_correct_level = %d, enable_log = %s, enable_shorthand = %s, enable_app_blocklist = %s, blocked_apps = %zu, enable_auto_exclude = %s, auto_blocked_apps = %zu, typing_mode = %u, hotkey_mode = %u",
                      static_cast<int>(config.input_method), static_cast<int>(config.auto_correct_level),
                      config.enable_log ? L"true" : L"false", config.enable_shorthand ? L"true" : L"false",
                      config.enable_app_blocklist ? L"true" : L"false", blocked_apps_.size(),
                      config.enable_auto_exclude ? L"true" : L"false", auto_blocked_apps_.size(),
                      typing_mode_, hotkey_mode_);
}

std::wstring VietnameseIME::LookUpShorthand(const std::wstring& shortcut) {
    if (shorthand_map_.empty() || shortcut.empty()) {
        return shortcut;
    }

    // Normalize shortcut to lower case for map lookup
    std::wstring lower_shortcut;
    lower_shortcut.reserve(shortcut.length());
    for (wchar_t c : shortcut) {
        lower_shortcut.push_back(core::rules::ToLower(c));
    }

    auto it = shorthand_map_.find(lower_shortcut);
    if (it == shorthand_map_.end()) {
        return shortcut;
    }

    const std::wstring& expansion = it->second;
    if (expansion.empty()) {
        return shortcut;
    }

    // Casing checks
    bool all_upper = true;
    for (wchar_t c : shortcut) {
        if (IsLowerChar(c)) all_upper = false;
    }

    // Case preservation logic
    if (all_upper) {
        std::wstring result;
        result.reserve(expansion.length());
        for (wchar_t c : expansion) {
            result.push_back(core::rules::ToUpper(c));
        }
        return result;
    }

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
        std::wstring result = expansion;
        result[0] = core::rules::ToUpper(result[0]);
        return result;
    }

    // Otherwise, return expansion exactly as defined in rules file
    return expansion;
}

void VietnameseIME::LoadShorthandRules() {
    shorthand_map_.clear();

    IMEConfig config = LoadConfigFromRegistry();
    if (!config.enable_shorthand) {
        return;
    }

    std::wstring filePath = GetShorthandFilePath(g_hInst);
    if (filePath.empty()) return;

    std::wstring utf16Content;
    if (!ReadUtf8TextFile(filePath, utf16Content)) {
        logger::Log(logger::Level::Warning, L"Shorthand file not found or cannot be opened");
        return;
    }

    ShorthandParseResult parsed = ParseShorthandRules(utf16Content);
    for (const auto& rule : parsed.rules) {
        shorthand_map_[rule.key] = rule.value;
    }

    logger::LogFormat(logger::Level::Info, L"Loaded %zu shorthand rules, invalid_lines = %zu, duplicate_lines = %zu",
                      shorthand_map_.size(), parsed.invalid_lines, parsed.duplicate_lines);
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
                logger::LogFormat(logger::Level::Info, L"MouseHookSubclassProc: Message %s detected, committing composition", msg_name);
                ComPtr<ITfDocumentMgr> doc_mgr;
                if (SUCCEEDED(ime->thread_mgr_->GetFocus(doc_mgr.GetAddressOf())) && doc_mgr) {
                    ComPtr<ITfContext> context;
                    if (SUCCEEDED(doc_mgr->GetTop(context.GetAddressOf())) && context) {
                        ime->pending_commit_caret_policy_ = CommitCaretPolicy::PreserveHostSelection;
                        ime->CommitCompositionSync(context.Get());
                    }
                }
            }
        }
    }
    return ::DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

void VietnameseIME::CommitActiveCompositionFromHook() {
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

void VietnameseIME::ClearLastCommitUndo() noexcept {
    if (last_commit_undo_) {
        SecureClearCommitUndoEntry(*last_commit_undo_);
    }
    last_commit_undo_.reset();
}

void VietnameseIME::CaptureCommitUndo(TfEditCookie ec, ITfContext* pic) {
    if (!active_composition_) return;

    ComPtr<ITfRange> comp_range;
    if (FAILED(active_composition_->GetRange(comp_range.GetAddressOf())) || !comp_range) {
        return;
    }

    wchar_t buf[256] = {0};
    ULONG fetched_chars = 0;
    comp_range->GetText(ec, 0, buf, 255, &fetched_chars);
    std::wstring display(buf, fetched_chars);
    std::wstring raw = engine_.GetRawString();

    if (!ShouldCaptureCommitUndo(raw, display)) {
        SecureEraseString(raw);
        SecureEraseString(display);
        SecureEraseBuffer(buf, std::size(buf));
        return;
    }

    CommitUndoEntry entry;
    entry.raw_keys = raw;
    entry.display_text = display;
    entry.method = engine_.GetInputMethod();
    entry.committed_tick = GetTickCount64();
    entry.hwnd = GetBestFocusWindow();
    entry.is_tsf = true;
    
    ComPtr<ITfRange> caret_range;
    if (SUCCEEDED(comp_range->Clone(caret_range.GetAddressOf())) && caret_range) {
        caret_range->Collapse(ec, TF_ANCHOR_END);
        entry.expected_caret_range = caret_range;
    }

    ClearLastCommitUndo();
    last_commit_undo_ = entry;
    logger::LogFormat(logger::Level::Info,
                      L"CaptureCommitUndo (TSF): raw_len=%zu, display_len=%zu",
                      raw.length(), display.length());
    SecureClearCommitUndoEntry(entry);
    SecureEraseString(raw);
    SecureEraseString(display);
    SecureEraseBuffer(buf, std::size(buf));
}

void VietnameseIME::CaptureCommitUndoDirectInline(HWND hwnd, bool is_scintilla) {
    std::wstring display = engine_.GetDisplayString();
    std::wstring raw = engine_.GetRawString();

    if (!ShouldCaptureCommitUndo(raw, display)) {
        SecureEraseString(raw);
        SecureEraseString(display);
        return;
    }

    CommitUndoEntry entry;
    entry.raw_keys = raw;
    entry.display_text = display;
    entry.method = engine_.GetInputMethod();
    entry.committed_tick = GetTickCount64();
    entry.hwnd = hwnd;
    entry.is_tsf = false;

    if (is_scintilla) {
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
    SecureEraseString(raw);
    SecureEraseString(display);
}

bool VietnameseIME::TryRestoreLastCommittedRaw(TfEditCookie ec, ITfContext* pic) {
    if (!last_commit_undo_ || !last_commit_undo_->is_tsf) {
        return false;
    }

    HWND focus_hwnd = GetBestFocusWindow();
    if (focus_hwnd != last_commit_undo_->hwnd) {
        ClearLastCommitUndo();
        return false;
    }

    ComPtr<ITfRange> range;
    TF_SELECTION sel;
    ULONG fetched = 0;
    if (SUCCEEDED(pic->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &sel, &fetched)) && fetched > 0) {
        range.Attach(sel.range);
        BOOL empty = TRUE;
        if (SUCCEEDED(range->IsEmpty(ec, &empty)) && empty) {
            ComPtr<ITfRange> verify_range;
            if (SUCCEEDED(range->Clone(verify_range.GetAddressOf())) && verify_range) {
                LONG shifted = 0;
                LONG to_shift = -static_cast<LONG>(last_commit_undo_->display_text.length());
                if (SUCCEEDED(verify_range->ShiftStart(ec, to_shift, &shifted, nullptr)) && (shifted == to_shift || shifted == -to_shift)) {
                    std::wstring text_buf(last_commit_undo_->display_text.length(), L'\0');
                    ULONG fetched_chars = 0;
                    if (SUCCEEDED(verify_range->GetText(ec, 0, &text_buf[0], static_cast<ULONG>(text_buf.size()), &fetched_chars)) && fetched_chars == text_buf.size()) {
                        if (text_buf == last_commit_undo_->display_text) {
                            logger::LogFormat(logger::Level::Info, L"TryRestoreLastCommittedRaw (TSF) match: replacing word (len: %zu) with raw (len: %zu)",
                                              last_commit_undo_->display_text.length(), last_commit_undo_->raw_keys.length());
                            is_updating_selection_ = true;
                            verify_range->SetText(ec, 0, last_commit_undo_->raw_keys.c_str(), static_cast<LONG>(last_commit_undo_->raw_keys.length()));
                            verify_range->Collapse(ec, TF_ANCHOR_END);
                            sel.range = verify_range.Get();
                            sel.style.ase = TF_AE_NONE;
                            sel.style.fInterimChar = FALSE;
                            pic->SetSelection(ec, 1, &sel);
                            is_updating_selection_ = false;
                            SecureEraseString(text_buf);
                            ClearLastCommitUndo();
                            return true;
                        }
                    }
                    SecureEraseString(text_buf);
                }
            }
        }
    }
    ClearLastCommitUndo();
    return false;
}

bool VietnameseIME::TryRestoreLastCommittedRawDirectInline(HWND hwnd) {
    if (!last_commit_undo_ || last_commit_undo_->is_tsf) {
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
            if (caret == last_commit_undo_->expected_caret_offset) {
                std::string display_utf8;
                int len = WideCharToMultiByte(CP_UTF8, 0, last_commit_undo_->display_text.c_str(), -1, nullptr, 0, nullptr, nullptr);
                if (len > 0) {
                    display_utf8.resize(len - 1);
                    WideCharToMultiByte(CP_UTF8, 0, last_commit_undo_->display_text.c_str(), -1, display_utf8.data(), len, nullptr, nullptr);
                }

                if (display_utf8.empty() || caret < display_utf8.length()) {
                    SecureEraseStringUtf8(display_utf8);
                    ClearLastCommitUndo();
                    return false;
                }

                const size_t start_pos = caret - display_utf8.length();
                std::vector<char> current_bytes(display_utf8.length() + 1, '\0');
                SciTextRange text_range{
                    {static_cast<LONG_PTR>(start_pos), static_cast<LONG_PTR>(caret)},
                    current_bytes.data()
                };
                ::SendMessageW(hwnd, SCI_GETTEXTRANGE, 0, reinterpret_cast<LPARAM>(&text_range));
                std::string current_text(current_bytes.data(), std::strlen(current_bytes.data()));
                auto verified_span = FindVerifiedBytesBeforeCaret(current_text, current_text.length(), display_utf8);
                if (!verified_span) {
                    SecureEraseVector(current_bytes);
                    SecureEraseStringUtf8(display_utf8);
                    SecureEraseStringUtf8(current_text);
                    ClearLastCommitUndo();
                    return false;
                }

                ::SendMessageW(hwnd, SCI_SETSEL, start_pos, caret);

                std::string raw_utf8;
                int raw_len = WideCharToMultiByte(CP_UTF8, 0, last_commit_undo_->raw_keys.c_str(), -1, nullptr, 0, nullptr, nullptr);
                if (raw_len > 0) {
                    raw_utf8.resize(raw_len - 1);
                    WideCharToMultiByte(CP_UTF8, 0, last_commit_undo_->raw_keys.c_str(), -1, raw_utf8.data(), raw_len, nullptr, nullptr);
                }

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
            if (caret == last_commit_undo_->expected_caret_offset) {
                const int text_len = ::GetWindowTextLengthW(hwnd);
                if (text_len < 0) {
                    ClearLastCommitUndo();
                    return false;
                }
                std::vector<wchar_t> text_buf(static_cast<size_t>(text_len) + 1, L'\0');
                ::GetWindowTextW(hwnd, text_buf.data(), static_cast<int>(text_buf.size()));
                std::wstring current_text(text_buf.data(), static_cast<size_t>(text_len));
                auto verified_span = FindVerifiedTextBeforeCaret(current_text, caret, last_commit_undo_->display_text);
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

bool VietnameseIME::TryProcessDirectCommitEsc() {
    if (!HasDirectInlineState()) return false;
    
    std::wstring raw_keys = engine_.GetRawString();
    std::wstring display_str = engine_.GetDisplayString();
    engine_.SecureClear();
    
    HWND hwnd = GetBestFocusWindow();
    if (hwnd) {
        if (IsNotepadPlusPlusDirectInlineFocused()) {
            for (size_t i = 0; i < display_str.length(); ++i) {
                ProcessNotepadPlusPlusDirectBackspace();
            }
            for (wchar_t ch : raw_keys) {
                ProcessNotepadPlusPlusDirectChar(ch);
            }
        } else if (IsDirectCommitApp()) {
            for (size_t i = 0; i < display_str.length(); ++i) {
                ProcessExplorerEditBackspace();
            }
            for (wchar_t ch : raw_keys) {
                ProcessExplorerEditChar(ch);
            }
        } else if (IsFakeBackspaceApp()) {
            for (size_t i = 0; i < display_str.length(); ++i) {
                ProcessFakeBackspaceEditBackspace();
            }
            for (wchar_t ch : raw_keys) {
                ProcessFakeBackspaceEditChar(ch);
            }
        }
    }
    
    ResetDirectInlineState();
    return true;
}

void VietnameseIME::TrackHotkey(WPARAM wParam, LPARAM lParam, bool is_key_down, BOOL* pfEaten) {
    if (hotkey_mode_ == 1) { // Alt + Z
        if (is_key_down && wParam == 'Z') {
            bool altDown = (::GetKeyState(VK_MENU) & 0x8000) != 0;
            bool ctrlDown = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
            bool shiftDown = (::GetKeyState(VK_SHIFT) & 0x8000) != 0;
            if (altDown && !ctrlDown && !shiftDown) {
                ToggleTypingMode();
                if (pfEaten) *pfEaten = TRUE;
            }
        }
    }
    else if (hotkey_mode_ == 0) { // Ctrl + Shift
        if (is_key_down) {
            if (wParam == VK_CONTROL || wParam == VK_LCONTROL || wParam == VK_RCONTROL) {
                ctrl_pressed_ = true;
                other_key_pressed_ = false;
            } else if (wParam == VK_SHIFT || wParam == VK_LSHIFT || wParam == VK_RSHIFT) {
                shift_pressed_ = true;
                other_key_pressed_ = false;
            } else {
                other_key_pressed_ = true;
            }
        } else { // Key up
            if (wParam == VK_CONTROL || wParam == VK_LCONTROL || wParam == VK_RCONTROL) {
                if (ctrl_pressed_ && shift_pressed_ && !other_key_pressed_) {
                    ToggleTypingMode();
                }
                ctrl_pressed_ = false;
            } else if (wParam == VK_SHIFT || wParam == VK_LSHIFT || wParam == VK_RSHIFT) {
                if (ctrl_pressed_ && shift_pressed_ && !other_key_pressed_) {
                    ToggleTypingMode();
                }
                shift_pressed_ = false;
            }
        }
    }
}

void VietnameseIME::ToggleTypingMode() {
    typing_mode_ = (typing_mode_ == 0) ? 1 : 0;
    
    std::wstring process_name = GetFocusedProcessName();
    if (!process_name.empty()) {
        HKEY hKey;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, REG_KEY_PATH, 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
            std::wstring val_name = L"AppTypingMode_" + process_name;
            RegSetValueExW(hKey, val_name.c_str(), 0, REG_DWORD, reinterpret_cast<const BYTE*>(&typing_mode_), sizeof(DWORD));
            RegCloseKey(hKey);
        }
    }
    
    // Notify all active instances
    TouchConfigRevision();
    
    logger::LogFormat(logger::Level::Info, L"ToggleTypingMode: Toggled typing mode of %s to %d", process_name.c_str(), typing_mode_);
}

} // namespace vn_ime

