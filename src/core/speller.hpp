#pragma once
#include <string>
#include <string_view>
#include <cstdint>
#include "types.hpp"

namespace vn_ime::core::speller {

enum class CorrectionKind : uint8_t {
    None,
    ToneRelocation,
    UoVowelSubstitution,
    MissingFinalT,
    SwappedFinalKeys,
    MissingModifier,
    MissingTone,
    AdjacentKeySwap,
    ContextualPhrase,
};

struct CorrectionResult {
    std::wstring word;
    CorrectionKind kind = CorrectionKind::None;
    int score = 0;
    bool changed = false;
    bool high_confidence = false;
};

// Returns true if the lowercase word is found in the static constexpr dictionary.
bool IsInDictionary(std::wstring_view word);

// Attempts to correct tone-placement or spelling typos.
// Returns the corrected word, maintaining the original casing if possible.
std::wstring CorrectWord(std::wstring_view word, std::wstring_view raw_keys);

// Detailed spelling correction returning candidate kinds and scoring
CorrectionResult CorrectWordEx(
    std::wstring_view word,
    std::wstring_view raw_keys,
    CorrectionLevel level,
    InputMethod method);

CorrectionResult CorrectWordEx(
    std::wstring_view word,
    std::wstring_view raw_keys,
    CorrectionLevel level);

// Helper to preserve the casing pattern of original word onto the corrected word.
std::wstring PreserveCasing(std::wstring_view original, std::wstring_view corrected);

} // namespace vn_ime::core::speller
