#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace vn_ime {

// How a hand-listed application should be typed into. The list is the
// "Direct inline/commit" box in the settings window; each line is either
// "app.exe" or "app.exe:mode".
enum class DirectAppMode : uint8_t {
    // Write straight into the focused Win32 Edit or Scintilla control, and put
    // the raw keys back when Esc is pressed.
    Inline = 0,
    // The same, except Esc is left to the host.
    Commit = 1,
    // No TSF composition at all: the key is eaten, the engine works out the
    // edit, and the result is replayed with SendInput.
    //
    // This is the only one of the three that reaches an application drawing its
    // own text - Photoshop's type tool, CorelDRAW's canvas - because those have
    // no Edit or Scintilla control to write into. It is also what stops such a
    // host from popping up its own composition box and only revealing the word
    // on commit: with nothing composing, there is nothing for it to show.
    SendKey = 2,
};

struct DirectAppEntry {
    // Exactly as typed. The caller normalizes it - this header stays free of
    // any dependency so it can be tested on its own.
    std::wstring process_name;
    DirectAppMode mode = DirectAppMode::Inline;
};

// Unknown text means Inline, which is what an older build did with a mode it
// did not recognise. A list edited on a newer build therefore still works after
// a downgrade, just without the newer behaviour.
inline DirectAppMode ParseDirectAppMode(std::wstring_view text) noexcept {
    std::wstring mode;
    mode.reserve(text.size());
    for (wchar_t c : text) {
        if (c == L' ' || c == L'\t' || c == L'\r' || c == L'\n') {
            continue;
        }
        mode.push_back(
            c >= L'A' && c <= L'Z'
                ? static_cast<wchar_t>(c - L'A' + L'a')
                : c);
    }
    if (mode == L"commit") {
        return DirectAppMode::Commit;
    }
    if (mode == L"sendkey") {
        return DirectAppMode::SendKey;
    }
    return DirectAppMode::Inline;
}

inline DirectAppEntry ParseDirectAppEntry(std::wstring_view line) {
    DirectAppEntry entry;
    const size_t colon = line.find_last_of(L':');
    // Position 1 is a drive letter, not a mode separator, so a line given as a
    // full path ("c:\\apps\\tool.exe") keeps its colon.
    if (colon != std::wstring_view::npos && colon > 1) {
        entry.mode = ParseDirectAppMode(line.substr(colon + 1));
        entry.process_name.assign(line.substr(0, colon));
    } else {
        entry.process_name.assign(line);
    }
    return entry;
}

}  // namespace vn_ime
