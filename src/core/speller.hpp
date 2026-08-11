#pragma once
#include <optional>
#include <span>
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
    StaleModifierOverride,
    ContextualPhrase,
};

struct CorrectionResult {
    std::wstring word;
    CorrectionKind kind = CorrectionKind::None;
    int score = 0;
    bool changed = false;
    bool high_confidence = false;
};

enum class EnglishProtectionDecision : uint8_t {
    None,
    PreserveRaw,
    AmbiguousVietnamese,
};

inline constexpr size_t kMaxAutoWordSegmentationRawLength = 24;

struct WordSegmentationCandidate {
    std::wstring text;
    int score = 0;
    int runner_up_score = 0;
    bool high_confidence = false;
};

// Returns true if the lowercase word is found in the static constexpr dictionary.
bool IsInDictionary(std::wstring_view word);

std::span<const std::wstring_view> CommonEnglishWords() noexcept;
std::span<const std::wstring_view> StrongEnglishProtectionWords() noexcept;
bool CommonEnglishWordsAreSorted() noexcept;
bool IsCommonEnglishWord(std::wstring_view word);
bool IsStrongEnglishProtectionWord(std::wstring_view word);
bool HasProtectedEnglishBigramSplit(std::wstring_view raw_token);
EnglishProtectionDecision ClassifyEnglishProtection(
    std::wstring_view raw_keys,
    std::wstring_view processed_word,
    InputMethod method,
    EnglishProtectionLevel level);

// Builds a commit-time two-syllable candidate without changing live Engine
// state. Callers retain ownership of context, shorthand, and security gates.
std::optional<WordSegmentationCandidate> BuildAutoWordSegmentationCandidate(
    std::wstring_view raw_token,
    std::wstring_view display_token,
    InputMethod method,
    CorrectionLevel level);
bool HasCuratedWordSegmentationPhrase(std::wstring_view phrase) noexcept;
size_t CuratedWordSegmentationBigramCount() noexcept;

// Attempts to correct tone-placement or spelling typos.
// Returns the corrected word, maintaining the original casing if possible.
std::wstring CorrectWord(std::wstring_view word, std::wstring_view raw_keys);

// Detailed spelling correction returning candidate kinds and scoring
CorrectionResult CorrectWordEx(
    std::wstring_view word,
    std::wstring_view raw_keys,
    CorrectionLevel level,
    InputMethod method,
    EnglishProtectionLevel english_protection_level = EnglishProtectionLevel::Balanced);

CorrectionResult CorrectWordEx(
    std::wstring_view word,
    std::wstring_view raw_keys,
    CorrectionLevel level,
    InputMethod method,
    bool enable_english_protection);

CorrectionResult CorrectWordEx(
    std::wstring_view word,
    std::wstring_view raw_keys,
    CorrectionLevel level);

// Helper to preserve the casing pattern of original word onto the corrected word.
std::wstring PreserveCasing(std::wstring_view original, std::wstring_view corrected);

} // namespace vn_ime::core::speller
