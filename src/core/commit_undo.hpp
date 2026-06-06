#pragma once
#include <string>
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

inline bool ShouldCaptureCommitUndo(const std::wstring& raw, const std::wstring& display) {
    if (raw.empty() || display.empty()) {
        return false;
    }
    if (raw == display) {
        return false;
    }
    if (raw.length() > 128) {
        return false;
    }
    return true;
}

} // namespace vn_ime
