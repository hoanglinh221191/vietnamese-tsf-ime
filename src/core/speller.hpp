#pragma once
#include <string>
#include <string_view>

namespace vn_ime::core::speller {

// Returns true if the lowercase word is found in the static constexpr dictionary.
bool IsInDictionary(std::wstring_view word);

// Attempts to correct tone-placement or spelling typos.
// Returns the corrected word, maintaining the original casing if possible.
std::wstring CorrectWord(std::wstring_view word, std::wstring_view raw_keys);

// Helper to preserve the casing pattern of original word onto the corrected word.
std::wstring PreserveCasing(std::wstring_view original, std::wstring_view corrected);

} // namespace vn_ime::core::speller
