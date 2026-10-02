#pragma once

// Zeroing what the engine copies of the typed text, before the memory is
// freed or reused. Every module's eraser comes here, so there is one way of
// doing it and one place to measure.

#include <array>
#include <cstddef>
#include <string>

namespace vn_ime::core {

// One wchar_t per volatile store. The compiler may not drop these, as it may a
// plain loop or a memset over memory about to be freed. Not SecureZeroMemory:
// that compiles here to a store per byte, and over the copies the speller
// makes for every probe it cost ordinary typing 9% (f7f301d).
inline void ZeroChars(wchar_t* chars, size_t count) noexcept {
    volatile wchar_t* const zeroed = chars;
    for (size_t i = 0; i < count; ++i) {
        zeroed[i] = 0;
    }
}

// Zeroes the characters a string holds and empties it.
inline void ZeroText(std::wstring& text) noexcept {
    ZeroChars(text.data(), text.size());
    text.clear();
}

// Zeroes the strings it was given when it goes out of scope, whichever return
// leaves the function: for copies a function holds across early returns.
//
//     std::wstring onset, coda;
//     TextsZeroedOnExit erased(onset, coda);
template <typename... Texts>
class TextsZeroedOnExit {
public:
    explicit TextsZeroedOnExit(Texts&... texts) noexcept : texts_{&texts...} {}
    ~TextsZeroedOnExit() {
        for (std::wstring* text : texts_) {
            ZeroText(*text);
        }
    }
    TextsZeroedOnExit(const TextsZeroedOnExit&) = delete;
    TextsZeroedOnExit& operator=(const TextsZeroedOnExit&) = delete;

private:
    std::array<std::wstring*, sizeof...(Texts)> texts_;
};

}  // namespace vn_ime::core
