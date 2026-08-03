#pragma once
#include <optional>
#include <limits>
#include <string>
#include <string_view>
#include <windows.h>
#include <msctf.h>
#include "com_ptr.hpp"
#include "types.hpp"

namespace vn_ime {

inline constexpr ULONGLONG kCommitUndoRestoreWindowMs = 10000;

struct CommitUndoEntry {
    std::wstring raw_keys;
    std::wstring display_text;
    core::InputMethod method = core::InputMethod::Telex;
    bool was_auto_corrected = false;
    bool was_reconversion = false;
    unsigned long long selection_generation = 0;
    ULONGLONG committed_tick = 0;
    HWND hwnd = nullptr;
    ComPtr<ITfRange> expected_caret_range;
    size_t expected_caret_offset = 0;
    bool is_tsf = false;
};

struct VerifiedTextSpan {
    size_t start = 0;
    size_t end = 0;
    bool has_trailing_space = false;
};

inline void SecureEraseCommitUndoString(std::wstring& value) noexcept {
    if (!value.empty()) {
        SecureZeroMemory(value.data(), value.size() * sizeof(wchar_t));
        value.clear();
    }
}

inline void SecureClearCommitUndoEntry(CommitUndoEntry& entry) noexcept {
    SecureEraseCommitUndoString(entry.raw_keys);
    SecureEraseCommitUndoString(entry.display_text);
    entry.method = core::InputMethod::Telex;
    entry.was_auto_corrected = false;
    entry.was_reconversion = false;
    entry.selection_generation = 0;
    entry.committed_tick = 0;
    entry.hwnd = nullptr;
    entry.expected_caret_range.Reset();
    entry.expected_caret_offset = 0;
    entry.is_tsf = false;
}

inline bool ShouldCaptureCommitUndo(const std::wstring& raw, const std::wstring& display) {
    if (raw.empty() || display.empty()) {
        return false;
    }
    if (raw.length() > 128) {
        return false;
    }
    return true;
}

inline bool IsCommitUndoRestoreWindowValid(
    ULONGLONG now,
    ULONGLONG committed_tick) noexcept {
    return committed_tick != 0 &&
           now >= committed_tick &&
           now - committed_tick <= kCommitUndoRestoreWindowMs;
}

inline bool ShouldRouteCommitUndoBackspace(
    const CommitUndoEntry& entry,
    ULONGLONG now,
    bool has_active_composition,
    bool no_modifier,
    bool focus_matches,
    bool host_supported) noexcept {
    return !has_active_composition &&
           no_modifier &&
           focus_matches &&
           host_supported &&
           ShouldCaptureCommitUndo(entry.raw_keys, entry.display_text) &&
           IsCommitUndoRestoreWindowValid(now, entry.committed_tick);
}

inline std::optional<VerifiedTextSpan> FindVerifiedTextBeforeCaret(
    std::wstring_view text,
    size_t caret,
    std::wstring_view expected_display) {
    if (expected_display.empty() || caret > text.length() || caret < expected_display.length()) {
        return std::nullopt;
    }

    const size_t start = caret - expected_display.length();
    if (text.compare(start, expected_display.length(), expected_display) != 0) {
        return std::nullopt;
    }
    return VerifiedTextSpan{start, caret};
}

inline std::optional<VerifiedTextSpan> FindVerifiedTextBeforeCaretWithOptionalTrailingSpace(
    std::wstring_view text,
    size_t caret,
    std::wstring_view expected_display) {
    if (expected_display.empty() || caret > text.length()) {
        return std::nullopt;
    }

    const size_t display_length = expected_display.length();
    if (display_length < (std::numeric_limits<size_t>::max)() &&
        caret >= display_length + 1) {
        const size_t start = caret - display_length - 1;
        if (text[start + display_length] == L' ' &&
            text.compare(start, display_length, expected_display) == 0) {
            return VerifiedTextSpan{start, caret, true};
        }
    }

    if (caret < display_length) {
        return std::nullopt;
    }

    const size_t start = caret - display_length;
    if (text.compare(start, display_length, expected_display) != 0) {
        return std::nullopt;
    }
    return VerifiedTextSpan{start, caret, false};
}

inline std::optional<VerifiedTextSpan> FindVerifiedBytesBeforeCaret(
    std::string_view text,
    size_t caret,
    std::string_view expected_display) {
    if (expected_display.empty() || caret > text.length() || caret < expected_display.length()) {
        return std::nullopt;
    }

    const size_t start = caret - expected_display.length();
    if (text.compare(start, expected_display.length(), expected_display) != 0) {
        return std::nullopt;
    }
    return VerifiedTextSpan{start, caret};
}

inline std::optional<VerifiedTextSpan> FindVerifiedBytesBeforeCaretWithOptionalTrailingSpace(
    std::string_view text,
    size_t caret,
    std::string_view expected_display) {
    if (expected_display.empty() || caret > text.length()) {
        return std::nullopt;
    }

    const size_t display_length = expected_display.length();
    if (display_length < (std::numeric_limits<size_t>::max)() &&
        caret >= display_length + 1) {
        const size_t start = caret - display_length - 1;
        if (text[start + display_length] == ' ' &&
            text.compare(start, display_length, expected_display) == 0) {
            return VerifiedTextSpan{start, caret, true};
        }
    }

    if (caret < display_length) {
        return std::nullopt;
    }

    const size_t start = caret - display_length;
    if (text.compare(start, display_length, expected_display) != 0) {
        return std::nullopt;
    }
    return VerifiedTextSpan{start, caret, false};
}

} // namespace vn_ime
