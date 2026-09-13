#pragma once

// Reading the focused control through UI Automation, for hosts whose TSF
// document hands back nothing at all.
//
// Telegram is the one this was measured on: 752 reconversion attempts, 752
// empty answers, because its text store grants an edit session and then
// describes no text. Nothing could be read, so a mark could only ever be put on
// the word Neokey had just typed and still remembered - never on a word that
// was finished, or clicked into.
//
// Its input field does answer UI Automation. Measured with a chat open: of 663
// elements in the window exactly two offer TextPattern, both the Qt class
// Ui::InputField::Inner - the search box and the message box - each reporting a
// selection range and accepting word-granularity movement. The message history
// offers none, so this channel can only ever see what is being typed.
//
// UI Automation is COM and far slower than a window message, and calling it
// from the host's own UI thread can re-enter the message loop. Both are why the
// caller asks for this only when a tone key arrives with no composition in
// flight and the host has already been measured as handing back nothing.

#include <windows.h>
#include <uiautomation.h>

#include <optional>
#include <string>

namespace vn_ime::uia {

// Declared here rather than taken from a library so that this header carries no
// link-time dependency of its own.
inline const CLSID kCUIAutomation = {
    0xff48dba4, 0x60ef, 0x4201,
    {0xaa, 0x87, 0x54, 0x10, 0x3e, 0xef, 0x59, 0x4e}};
inline const IID kIUIAutomation = {
    0x30cbe57d, 0xd9d0, 0x452a,
    {0xab, 0x13, 0x7a, 0xc5, 0xac, 0x48, 0x25, 0xee}};
inline const IID kIUIAutomationTextPattern = {
    0x32eba289, 0x3583, 0x42c9,
    {0x9c, 0x59, 0x3b, 0x6d, 0x9a, 0x1e, 0x9b, 0x6a}};

struct FocusedText {
    std::wstring text;
    size_t selection_start = 0;
    size_t selection_end = 0;
};

namespace detail {

template <typename T>
struct Released {
    T* ptr = nullptr;
    ~Released() {
        if (ptr) ptr->Release();
    }
    T* operator->() const noexcept { return ptr; }
    explicit operator bool() const noexcept { return ptr != nullptr; }
};

struct FreedString {
    BSTR value = nullptr;
    ~FreedString() {
        if (value) ::SysFreeString(value);
    }
};

// One per thread and kept, because creating it costs far more than using it and
// the service is asked repeatedly on the same thread. An apartment is already
// running here - TSF requires one - so none is started.
inline IUIAutomation* ThreadAutomation() {
    static thread_local IUIAutomation* automation = nullptr;
    static thread_local bool attempted = false;
    if (!attempted) {
        attempted = true;
        if (FAILED(::CoCreateInstance(
                kCUIAutomation, nullptr, CLSCTX_INPROC_SERVER, kIUIAutomation,
                reinterpret_cast<void**>(&automation)))) {
            automation = nullptr;
        }
    }
    return automation;
}

// How many characters lie between the start of the document and an endpoint of
// the selection. UI Automation has no notion of an offset, so the distance is
// measured by taking the document from its start to that endpoint and asking
// how long it is.
inline std::optional<size_t> EndpointOffset(
    IUIAutomationTextRange* document,
    IUIAutomationTextRange* selection,
    TextPatternRangeEndpoint which) {
    Released<IUIAutomationTextRange> prefix;
    if (FAILED(document->Clone(&prefix.ptr)) || !prefix) {
        return std::nullopt;
    }
    if (FAILED(prefix->MoveEndpointByRange(
            TextPatternRangeEndpoint_End, selection, which))) {
        return std::nullopt;
    }
    FreedString text;
    if (FAILED(prefix->GetText(-1, &text.value))) {
        return std::nullopt;
    }
    return static_cast<size_t>(::SysStringLen(text.value));
}

} // namespace detail

// The focused control's text and where the caret is in it. Nothing when the
// control does not answer UI Automation, when it answers with more text than
// the caller asked for, or when any step of the walk fails - all of which mean
// the same thing to the caller: leave the document alone.
inline std::optional<FocusedText> ReadFocusedText(size_t max_chars) {
    IUIAutomation* automation = detail::ThreadAutomation();
    if (!automation) {
        return std::nullopt;
    }

    detail::Released<IUIAutomationElement> element;
    if (FAILED(automation->GetFocusedElement(&element.ptr)) || !element) {
        return std::nullopt;
    }

    detail::Released<IUnknown> pattern_unknown;
    if (FAILED(element->GetCurrentPattern(UIA_TextPatternId,
                                          &pattern_unknown.ptr)) ||
        !pattern_unknown) {
        return std::nullopt;
    }
    detail::Released<IUIAutomationTextPattern> pattern;
    if (FAILED(pattern_unknown->QueryInterface(
            kIUIAutomationTextPattern,
            reinterpret_cast<void**>(&pattern.ptr))) ||
        !pattern) {
        return std::nullopt;
    }

    detail::Released<IUIAutomationTextRange> document;
    if (FAILED(pattern->get_DocumentRange(&document.ptr)) || !document) {
        return std::nullopt;
    }

    detail::Released<IUIAutomationTextRangeArray> selection;
    if (FAILED(pattern->GetSelection(&selection.ptr)) || !selection) {
        return std::nullopt;
    }
    int range_count = 0;
    if (FAILED(selection->get_Length(&range_count)) || range_count < 1) {
        return std::nullopt;
    }
    detail::Released<IUIAutomationTextRange> caret;
    if (FAILED(selection->GetElement(0, &caret.ptr)) || !caret) {
        return std::nullopt;
    }

    // Bounded before it is read, not after: a document the size of a chat log
    // is not something to pull across a COM boundary on a keystroke.
    detail::FreedString content;
    if (FAILED(document->GetText(static_cast<int>(max_chars) + 1,
                                 &content.value))) {
        return std::nullopt;
    }
    const size_t length = ::SysStringLen(content.value);
    if (length > max_chars) {
        return std::nullopt;
    }

    const auto start = detail::EndpointOffset(
        document.ptr, caret.ptr, TextPatternRangeEndpoint_Start);
    const auto end = detail::EndpointOffset(
        document.ptr, caret.ptr, TextPatternRangeEndpoint_End);
    if (!start.has_value() || !end.has_value() || *start > *end ||
        *end > length) {
        return std::nullopt;
    }

    FocusedText result;
    result.text.assign(content.value ? content.value : L"", length);
    result.selection_start = *start;
    result.selection_end = *end;
    return result;
}

} // namespace vn_ime::uia
