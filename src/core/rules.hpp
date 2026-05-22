#pragma once
#include <string>
#include <string_view>
#include "types.hpp"

namespace vn_ime::core::rules {

struct VowelData {
    wchar_t base;     // 'a', 'e', 'i', 'o', 'u', 'y'
    wchar_t raw;      // 'a', 'ă', 'â', 'e', 'ê', 'i', 'o', 'ô', 'ơ', 'u', 'ư', 'y'
    ToneMark tone;
    bool is_upper;
};

// Returns true if c is a Vietnamese vowel (plain or accented)
bool GetVowelData(wchar_t c, VowelData& data);

// Constructs a vowel character from its raw base, tone mark, and casing
wchar_t MakeVowel(wchar_t raw, ToneMark tone, bool is_upper);

// Case-insensitive helpers for character classification
bool IsVowel(wchar_t c);
bool IsConsonant(wchar_t c);

// Lowercases and uppercases Unicode Vietnamese characters
wchar_t ToLower(wchar_t c);
wchar_t ToUpper(wchar_t c);

// Checks if a word is a valid Vietnamese syllable (spelling check)
bool IsValidVietnamese(std::wstring_view word);

// Finds the index in the word where the tone mark should be placed (modern rule).
// Returns -1 if no vowels found.
int FindTonePosition(std::wstring_view word);

// Applies a tone mark to a word, replacing the old tone.
// Returns the updated word.
std::wstring ApplyTone(std::wstring_view word, ToneMark tone);

// Checks if a character matches a modification key for Telex/VNI
// e.g. for Telex: 'w', 'a', 'e', 'o', 'd'
// e.g. for VNI: '6', '7', '8', '9'
bool IsModificationKey(wchar_t ch, InputMethod method);

// Applies vowel modification in-place (Telex/VNI rules)
// Returns true if a modification was applied, false otherwise.
bool ApplyModification(std::wstring& word, wchar_t modKey, InputMethod method);

} // namespace vn_ime::core::rules
