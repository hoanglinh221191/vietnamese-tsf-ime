#pragma once
#include <string>
#include <string_view>
#include "types.hpp"

namespace vn_ime::core {

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

} // namespace vn_ime::core
