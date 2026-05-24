#pragma once
#include <optional>
#include <string>
#include <string_view>
#include "types.hpp"

namespace vn_ime::core {

struct ReconversionEdit {
    size_t start = 0;
    size_t end = 0;
    size_t selection_start = 0;
    size_t selection_end = 0;
    std::wstring replacement;
};

class Engine {
public:
    explicit Engine(InputMethod method = InputMethod::Telex);

    // Process a new character. Returns true if the key is part of the composition.
    bool ProcessKey(wchar_t ch);

    // Handles backspace. Returns true if a character was removed.
    bool Backspace();
    bool BackspaceDisplayChar();

    // Clears the buffer (commits or discards the current word).
    void Clear();
    void SecureClear();

    // Returns the current string to display on the screen
    std::wstring GetDisplayString() const;

    // Returns the raw keystroke sequence
    std::wstring GetRawString() const;
    bool HasPendingRaw() const noexcept { return !raw_keys_.empty(); }

    // Sets the active input method
    void SetInputMethod(InputMethod method);

    // Returns the active input method
    InputMethod GetInputMethod() const { return method_; }

    // Sets whether auto-correction (speller) is enabled
    void SetAutoCorrect(bool enable) { enable_auto_correct_ = enable; }

    // Gets whether auto-correction (speller) is enabled
    bool GetAutoCorrect() const { return enable_auto_correct_; }

private:
    InputMethod method_;
    std::wstring raw_keys_;
    std::wstring processed_word_;
    bool enable_auto_correct_ = true;
    bool suppress_auto_correct_ = false;
};

std::optional<std::wstring> BuildReconversionCandidate(
    std::wstring_view committed_word,
    wchar_t key,
    InputMethod method);

std::optional<ReconversionEdit> BuildReconversionEdit(
    std::wstring_view text,
    size_t selection_start,
    size_t selection_end,
    wchar_t key,
    InputMethod method,
    bool truncated_left = false,
    bool truncated_right = false);

} // namespace vn_ime::core
