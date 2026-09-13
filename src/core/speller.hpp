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
    // A dictionary neighbour one edit away, found by the Experimental
    // Damerau-Levenshtein scan. Kept distinct from AdjacentKeySwap so undo,
    // logging and any future commit policy can tell a rewrite of the word
    // apart from a single mistyped tone key.
    EditDistance,
    // One key struck twice where the doubled key means nothing in the active
    // method - a bouncing keyboard, not a spelling choice.
    KeyBounce,
    // Two keys that arrived in the wrong order, anywhere in the word.
    TransposedKeys,
};

// How long a token may be before the adjacent-key tone sweep stops looking at
// it. See TryAdjacentKeyToneCorrection for why sixteen, and for the measured
// cost of not having a bound at all.
inline constexpr size_t kMaxAdjacentKeySweepKeys = 16;

// How short a token may be before the sweep stops looking at it. Two-key
// tokens are commands and abbreviations far more often than mistyped
// syllables, and the English lexicon cannot protect them because they are not
// English words. See TryAdjacentKeyToneCorrection for the measurement.
inline constexpr size_t kMinAdjacentKeySweepKeys = 3;

// How common a syllable is, as floor(log2(occurrences + 1)) over a Vietnamese
// Wikipedia sample - one tier is one doubling, and 0 means the corpus never
// showed it. Zero is also what an unknown word gets, so a caller cannot tell
// "rare" from "not a syllable" and must not try to.
uint8_t SyllableFrequencyTier(std::wstring_view word) noexcept;

// The gap that lets frequency decide between two spellings that are both real
// words. Chosen from the pairs it has to get right and the pairs it must leave
// alone, measured on that sample:
//
//   separate:  được/đợc 19,  đường/đườn 14,  của/cưa 11
//   leave:     làm/lam 4,    hướng/hương 2,  tôi/trời 1
//
// Eight sits in the gap between those two groups with room either side. It is
// about a 256-fold difference, which is the point: this is only ever allowed to
// answer "one of these is not seriously a candidate", never "this one looks a
// bit more likely".
inline constexpr int kFrequencyTieBreakTiers = 8;

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

enum class EnglishLexiconTier : uint8_t {
    None = 0,
    Common = 1,
    Extended = 2,
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

// Builds the Experimental edit-distance index ahead of the first correction
// that needs it. Safe to call repeatedly and from any level.
void WarmUpEditDistanceIndex() noexcept;

std::span<const std::wstring_view> CommonEnglishWords() noexcept;
std::span<const std::wstring_view> StrongEnglishProtectionWords() noexcept;
bool CommonEnglishWordsAreSorted() noexcept;
bool IsCommonEnglishWord(std::wstring_view word);
bool IsStrongEnglishProtectionWord(std::wstring_view word);
EnglishLexiconTier LookupBilingualEnglishWord(std::wstring_view word) noexcept;
size_t BilingualEnglishWordCount() noexcept;
size_t BilingualEnglishCommonWordCount() noexcept;
size_t BilingualEnglishExtendedWordCount() noexcept;
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
std::span<const std::wstring_view> CuratedVietnameseBigrams() noexcept;
// Indices into CuratedVietnameseBigrams() whose SECOND token equals `second`
// case-insensitively; empty when none does. Both callers - Experimental
// segmentation and commit-time Fuzzy Input - used to walk all ~2000 phrases for
// every boundary they tried, which is where most of their cost was.
std::span<const uint16_t> CuratedVietnameseBigramsWithSecond(
    std::wstring_view second);
bool HasCuratedVietnameseBigram(std::wstring_view phrase) noexcept;
size_t CuratedVietnameseBigramCount() noexcept;

// Attempts to correct tone-placement or spelling typos.
// Returns the corrected word, maintaining the original casing if possible.
std::wstring CorrectWord(std::wstring_view word, std::wstring_view raw_keys);

// Detailed spelling correction returning candidate kinds and scoring.
//
// at_commit says the delimiter has been struck, so the token is a finished
// word rather than one on its way somewhere. Only the adjacent-key sweep reads
// it, and only to stop treating a valid prefix as a reason to stand down. It
// must never be set on the per-keystroke path - see CorrectCommittedWord.
CorrectionResult CorrectWordEx(
    std::wstring_view word,
    std::wstring_view raw_keys,
    CorrectionLevel level,
    InputMethod method,
    EnglishProtectionLevel english_protection_level = EnglishProtectionLevel::Balanced,
    bool at_commit = false);

// The same correction, for a word the user has just finished with: run once
// from the commit path when Space or punctuation arrives, never per keystroke.
// A word being typed is allowed to be an incomplete spelling of a real one -
// "bie" on its way to "biếm" - and reading those as finished rewrites them
// under the cursor. Once the delimiter has been struck that reasoning has
// expired, and the stricter reading repairs slips the live path has to let
// through. Words the dictionary knows are returned untouched either way.
CorrectionResult CorrectCommittedWord(
    std::wstring_view word,
    std::wstring_view raw_keys,
    CorrectionLevel level,
    InputMethod method,
    EnglishProtectionLevel english_protection_level =
        EnglishProtectionLevel::Balanced);

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
