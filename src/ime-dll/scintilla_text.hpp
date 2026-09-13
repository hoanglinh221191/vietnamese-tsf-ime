#pragma once

// Scintilla keeps its document in UTF-8 and answers every question about it in
// byte offsets, while the engine and the reconversion rules work in UTF-16.
// Vietnamese is where that difference stops being academic: every letter with a
// mark on it is two or three bytes, so a caret nine characters into a word is
// nowhere near nine bytes into it, and an edit placed at the wrong offset lands
// in the middle of a character.
//
// These two conversions are the whole of that translation, kept here on their
// own so they can be tested against real Vietnamese text rather than trusted.

#include <windows.h>

#include <optional>
#include <string>
#include <string_view>

namespace vn_ime::scintilla {

// UTF-8 bytes -> UTF-16. Returns nothing rather than a replacement character
// when the bytes are not valid UTF-8: a window that cannot be read exactly is a
// window whose offsets cannot be trusted, and the caller must leave the
// document alone instead of editing at a guess.
inline std::optional<std::wstring> DecodeUtf8(std::string_view bytes) {
    if (bytes.empty()) {
        return std::wstring();
    }
    if (bytes.size() > static_cast<size_t>(INT_MAX)) {
        return std::nullopt;
    }
    const int wide_length = ::MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(),
        static_cast<int>(bytes.size()), nullptr, 0);
    if (wide_length <= 0) {
        return std::nullopt;
    }
    std::wstring text(static_cast<size_t>(wide_length), L'\0');
    if (::MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(),
            static_cast<int>(bytes.size()), text.data(),
            wide_length) != wide_length) {
        return std::nullopt;
    }
    return text;
}

// UTF-16 -> UTF-8 bytes. Nothing when the text cannot be encoded, which for
// well-formed UTF-16 means only a lone surrogate.
inline std::optional<std::string> EncodeUtf8(std::wstring_view text) {
    if (text.empty()) {
        return std::string();
    }
    if (text.size() > static_cast<size_t>(INT_MAX)) {
        return std::nullopt;
    }
    const int byte_length = ::WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
        static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (byte_length <= 0) {
        return std::nullopt;
    }
    std::string bytes(static_cast<size_t>(byte_length), '\0');
    if (::WideCharToMultiByte(
            CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
            static_cast<int>(text.size()), bytes.data(), byte_length, nullptr,
            nullptr) != byte_length) {
        return std::nullopt;
    }
    return bytes;
}

// How many bytes the first `char_count` UTF-16 units occupy once encoded. This
// is the one direction the caller actually needs: the rules hand back offsets
// counted in characters, and Scintilla has to be told where that is in bytes.
inline std::optional<size_t> Utf8ByteLengthOfPrefix(
    std::wstring_view text, size_t char_count) {
    if (char_count > text.size()) {
        return std::nullopt;
    }
    const auto encoded = EncodeUtf8(text.substr(0, char_count));
    if (!encoded.has_value()) {
        return std::nullopt;
    }
    return encoded->size();
}

// The reverse, for turning Scintilla's caret position into an index the rules
// understand. `byte_count` must fall on a character boundary; a position inside
// a character means the window was cut badly and nothing should be edited.
inline std::optional<size_t> Utf16LengthOfPrefix(
    std::string_view bytes, size_t byte_count) {
    if (byte_count > bytes.size()) {
        return std::nullopt;
    }
    const auto decoded = DecodeUtf8(bytes.substr(0, byte_count));
    if (!decoded.has_value()) {
        return std::nullopt;
    }
    return decoded->size();
}

} // namespace vn_ime::scintilla
