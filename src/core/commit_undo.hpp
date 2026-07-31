#pragma once
#include <optional>
#include <string>
#include <string_view>
#include <windows.h>
#include <msctf.h>
#include "com_ptr.hpp"
#include "types.hpp"

namespace vn_ime {

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

inline std::optional<VerifiedTextSpan> FindVerifiedTextBeforeCaret(
    std::wstring_view text,
    size_t caret,
    std::wstring_view expected_display) {
    if (expected_display.empty() || caret > text.length() || caret < expected_display.length()) {
        return std::nullopt;
    }

    const size_t start = caret - expected_display.length();
    if (text.substr(start, expected_display.length()) != expected_display) {
        return std::nullopt;
    }
    return VerifiedTextSpan{start, caret};
}

inline std::optional<VerifiedTextSpan> FindVerifiedBytesBeforeCaret(
    std::string_view text,
    size_t caret,
    std::string_view expected_display) {
    if (expected_display.empty() || caret > text.length() || caret < expected_display.length()) {
        return std::nullopt;
    }

    const size_t start = caret - expected_display.length();
    if (text.substr(start, expected_display.length()) != expected_display) {
        return std::nullopt;
    }
    return VerifiedTextSpan{start, caret};
}

} // namespace vn_ime
