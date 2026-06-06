#pragma once
#include <cstddef>
#include <optional>
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

struct ReconversionSpan {
    size_t start = 0;
    size_t end = 0;
    size_t selection_start = 0;
    size_t selection_end = 0;
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

enum class SyllableValidity : uint8_t {
    Invalid,
    ValidPrefix,
    Valid,
};

// Checks if a word is a valid Vietnamese syllable (spelling check)
SyllableValidity ValidateVietnameseSyllable(std::wstring_view word);
bool IsValidVietnamese(std::wstring_view word, bool in_progress = false);

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

// Checks if a character matches a tone key for Telex/VNI
bool IsToneKey(wchar_t ch, InputMethod method);

// Checks if a character is a valid word character for Vietnamese
bool IsWordChar(wchar_t c);

// Finds one complete word eligible for reconversion in a bounded context window.
// The truncated flags mean the first/last window character may continue outside
// the window and prevent accepting a word that touches that edge.
std::optional<ReconversionSpan> ResolveReconversionSpan(
    std::wstring_view text,
    size_t selection_start,
    size_t selection_end,
    bool truncated_left = false,
    bool truncated_right = false,
    size_t max_word_length = 0);

// Reconstructs raw input keys from a processed Vietnamese word
std::wstring ReconstructRawKeys(std::wstring_view word, InputMethod method);

// Applies vowel modification in-place (Telex/VNI rules)
// Returns true if a modification was applied, false otherwise.
bool ApplyModification(std::wstring& word, wchar_t modKey, InputMethod method);

} // namespace vn_ime::core::rules
