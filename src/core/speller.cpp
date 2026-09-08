#include "speller.hpp"
#include "speller_data.hpp"
#include "english_protection_words.hpp"
#include "english_lexicon_generated.hpp"
#include "segmentation_bigrams.hpp"
#include "rules.hpp"
#include "engine.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>
#include <cwctype>
#include <windows.h>

namespace vn_ime::core::speller {

namespace {

inline constexpr size_t kMaxShapeModifierEventsForCorrection = 4;

// Helper to strip the tone from a word and return the flat word + tone mark
std::wstring StripTone(std::wstring_view word, ToneMark& tone) {
    std::wstring flat;
    flat.reserve(word.length());
    tone = ToneMark::None;
    
    for (wchar_t c : word) {
        rules::VowelData vd;
        if (rules::GetVowelData(c, vd)) {
            flat.push_back(rules::MakeVowel(vd.raw, ToneMark::None, vd.is_upper));
            if (vd.tone != ToneMark::None) {
                tone = vd.tone;
            }
        } else {
            flat.push_back(c);
        }
    }
    return flat;
}

// Replaces a substring in a wstring
std::wstring ReplaceAll(std::wstring str, std::wstring_view from, std::wstring_view to) {
    size_t start_pos = 0;
    while ((start_pos = str.find(from, start_pos)) != std::wstring::npos) {
        str.replace(start_pos, from.length(), to);
        start_pos += to.length();
    }
    return str;
}

bool EndsWith(std::wstring_view value, std::wstring_view suffix) {
    return value.length() >= suffix.length() &&
           value.substr(value.length() - suffix.length()) == suffix;
}

bool ShouldTryMissingFinalTCorrection(std::wstring_view flat_word, ToneMark active_tone) {
    if (active_tone == ToneMark::None || flat_word.empty()) {
        return false;
    }

    // Keep this typo correction intentionally narrow. It exists for common
    // iê/uyê cases such as vies -> viết, tie61 -> tiết, thuyes -> thuyết;
    // broad open-syllable correction turns valid words like khoá into khoát.
    return EndsWith(flat_word, L"ie") ||
           EndsWith(flat_word, L"iê") ||
           EndsWith(flat_word, L"uye") ||
           EndsWith(flat_word, L"uyê");
}

bool IsAllowedMissingFinalTRawKeys(std::wstring_view raw_lower) {
    if (raw_lower.length() < 3) return false;
    return EndsWith(raw_lower, L"ees") ||
           EndsWith(raw_lower, L"e61") ||
           EndsWith(raw_lower, L"uyes") ||
           EndsWith(raw_lower, L"uye61") ||
           EndsWith(raw_lower, L"uye1") ||
           EndsWith(raw_lower, L"ies") ||
           EndsWith(raw_lower, L"i61") ||
           EndsWith(raw_lower, L"i1");
}

} // namespace

bool IsInDictionary(std::wstring_view word) {
    // Binary search on the constexpr DICTIONARY array
    return std::binary_search(DICTIONARY, DICTIONARY + DICTIONARY_SIZE, word);
}

// A letter for casing purposes: one that has a distinct upper and lower form in
// rules::ToUpper/ToLower. This deliberately does NOT use std::iswalpha, which
// classifies by the process locale - and Neokey runs inside whatever host
// process loaded it, so that locale is not ours to set. Under locale "C",
// iswalpha(L'\u0110') is false and PreserveCasing("\u0110\u01B0\u1EDDgn",
// "\u0111\u01B0\u1EDDng") dropped the capital, silently lowercasing the first
// letter of any corrected word that starts with a Vietnamese letter.
inline bool IsCasedLetter(wchar_t character) noexcept {
    return rules::ToLower(character) != rules::ToUpper(character);
}

std::wstring PreserveCasing(std::wstring_view original, std::wstring_view corrected) {
    if (original.empty() || corrected.empty()) {
        return std::wstring(corrected);
    }
    
    bool all_upper = true;
    bool has_alpha = false;
    for (wchar_t c : original) {
        if (IsCasedLetter(c)) {
            has_alpha = true;
            if (c != rules::ToUpper(c)) {
                all_upper = false;
                break;
            }
        }
    }
    
    if (has_alpha && all_upper) {
        std::wstring result;
        result.reserve(corrected.length());
        for (wchar_t c : corrected) {
            result.push_back(rules::ToUpper(c));
        }
        return result;
    }
    
    if (IsCasedLetter(original[0]) && original[0] == rules::ToUpper(original[0])) {
        std::wstring result(corrected);
        result[0] = rules::ToUpper(result[0]);
        return result;
    }
    
    return std::wstring(corrected);
}

namespace {

std::optional<ToneMark> ToneFromTelexKey(wchar_t key) {
    switch (key) {
        case L's': return ToneMark::Sacute;
        case L'f': return ToneMark::Grave;
        case L'r': return ToneMark::Hook;
        case L'x': return ToneMark::Tilde;
        case L'j': return ToneMark::Dot;
        case L'z': return std::nullopt;
        default: return std::nullopt;
    }
}

std::vector<wchar_t> NearbyTelexToneKeys(wchar_t typo_key) {
    switch (typo_key) {
        case L't': return {L'f'};
        default: return {};
    }
}

std::optional<std::wstring> BuildKnownIeyueFinalToneCandidate(
    std::wstring_view raw_stem,
    wchar_t final_consonant,
    ToneMark tone) {
    std::wstring flat(raw_stem);
    if (EndsWith(flat, L"uye")) {
        flat.replace(flat.length() - 3, 3, L"uy\u00EA");
    } else if (EndsWith(flat, L"ie")) {
        flat.replace(flat.length() - 2, 2, L"i\u00EA");
    } else {
        return std::nullopt;
    }

    flat.push_back(final_consonant);
    std::wstring candidate = rules::ApplyTone(flat, tone);
    if (IsInDictionary(candidate)) {
        return candidate;
    }
    return std::nullopt;
}

bool IsKnownIeyueTnTypo(std::wstring_view raw_lower) {
    return raw_lower == L"tuyetn" ||
           raw_lower == L"vietn" ||
           raw_lower == L"thietn";
}

std::optional<CorrectionResult> TryTelexToneKeyAdjacencyCorrection(
    std::wstring_view word,
    std::wstring_view raw_lower,
    CorrectionLevel level) {
    if (level == CorrectionLevel::Off || raw_lower.length() < 4) {
        return std::nullopt;
    }

    // Keep the first Normal-level Telex adjacency rule scoped to the observed
    // typo family until broader corpus gates are available.
    if (!IsKnownIeyueTnTypo(raw_lower)) {
        return std::nullopt;
    }

    const wchar_t typo_key = raw_lower[raw_lower.length() - 2];
    const wchar_t final_consonant = raw_lower.back();
    const std::wstring_view raw_stem = raw_lower.substr(0, raw_lower.length() - 2);

    std::vector<std::wstring> matched_candidates;
    for (wchar_t nearby_key : NearbyTelexToneKeys(typo_key)) {
        auto tone = ToneFromTelexKey(nearby_key);
        if (!tone) {
            continue;
        }
        auto candidate = BuildKnownIeyueFinalToneCandidate(raw_stem, final_consonant, *tone);
        if (candidate &&
            std::find(matched_candidates.begin(), matched_candidates.end(), *candidate) == matched_candidates.end()) {
            matched_candidates.push_back(*candidate);
        }
    }

    if (matched_candidates.size() != 1) {
        return std::nullopt;
    }

    CorrectionResult result;
    result.word = PreserveCasing(word, matched_candidates[0]);
    result.kind = CorrectionKind::AdjacentKeySwap;
    result.score = 900;
    result.changed = true;
    result.high_confidence = true;
    return result;
}

std::optional<CorrectionResult> TryVniKnownIeyueTnCorrection(
    std::wstring_view word,
    std::wstring_view raw_lower,
    CorrectionLevel level) {
    if (level == CorrectionLevel::Off || raw_lower.length() < 4) {
        return std::nullopt;
    }

    // Keep this VNI interpretation as an explicit whitelist; do not generalize
    // arbitrary ...tn endings at Normal level.
    if (!IsKnownIeyueTnTypo(raw_lower)) {
        return std::nullopt;
    }

    const wchar_t final_consonant = raw_lower.back();
    const std::wstring_view raw_stem = raw_lower.substr(0, raw_lower.length() - 2);
    auto candidate = BuildKnownIeyueFinalToneCandidate(raw_stem, final_consonant, ToneMark::Grave);
    if (!candidate) {
        return std::nullopt;
    }

    CorrectionResult result;
    result.word = PreserveCasing(word, *candidate);
    result.kind = CorrectionKind::AdjacentKeySwap;
    result.score = 900;
    result.changed = true;
    result.high_confidence = true;
    return result;
}

// The tone/modifier keys a slipped finger most likely meant. Geometry is the
// standard QWERTY stagger: the number row sits half a key to the left of the
// letters below it, so each of q..o straddles exactly two digits, and row 3 is
// offset a quarter key from row 2.
//
// Only the number row is modelled. On a desktop many VNI typists reach for the
// numpad instead, where a slip lands on another DIGIT - 1 for 2, 4 for 5 - and
// that is deliberately not corrected here: a wrong tone almost always spells
// another real word (má/mà/mả/mã/mạ), so there is nothing to detect and
// guessing between them would be worse than leaving it.
std::vector<wchar_t> GetNearbyDauKeys(wchar_t key, InputMethod method) {
    std::vector<wchar_t> res;
    if (method == InputMethod::Telex || method == InputMethod::SimpleTelex) {
        switch (key) {
            case L'a': res = {L's', L'z', L'w'}; break;
            case L'd': res = {L's', L'f', L'r', L'x'}; break;
            case L'z': res = {L's', L'x', L'z'}; break;
            case L'w': res = {L's', L'w'}; break;
            case L'q': res = {L'w'}; break;
            // Not 's': 'e' is three quarters of a key from it, and offering it
            // makes "vae" ambiguous between vả and vá, which loses the fix.
            case L'e': res = {L'w', L'r'}; break;
            case L'g': res = {L'f', L'j'}; break;
            case L'r': res = {L'f', L'r'}; break;
            case L'v': res = {L'f'}; break;
            case L'c': res = {L'f', L'x'}; break;
            case L't': res = {L'r', L'f'}; break;
            case L's': res = {L'x', L'z', L'w', L's'}; break;
            case L'h': res = {L'j'}; break;
            case L'k': res = {L'j'}; break;
            case L'u': res = {L'j'}; break;
            case L'i': res = {L'j'}; break;
            case L'n': res = {L'j'}; break;
            case L'm': res = {L'j'}; break;
            default: break;
        }
    } else if (method == InputMethod::VNI) {
        switch (key) {
            case L'q': res = {L'1', L'2'}; break;
            case L'w': res = {L'2', L'3'}; break;
            case L'e': res = {L'3', L'4'}; break;
            case L'r': res = {L'4', L'5'}; break;
            case L't': res = {L'5', L'6'}; break;
            case L'y': res = {L'6', L'7'}; break;
            case L'u': res = {L'7', L'8'}; break;
            // 9 is the VNI key for d-stroke, so o and i straddle it too.
            case L'i': res = {L'8', L'9'}; break;
            case L'o': res = {L'9'}; break;
            default: break;
        }
    }
    return res;
}

void SecureEraseText(std::wstring& text) noexcept;

// Catches a finger that landed on the letter below the tone digit it was aiming
// for - on QWERTY '3' sits over 'e', '5' over 't', '7' over 'u' - and puts the
// digit back. Every one of those keys is also an ordinary Vietnamese letter, so
// the rule is only allowed to fire on a word that does NOT already read as
// Vietnamese. Without that guard it rewrote the 'e' of a legitimate "oe" rime
// into a tone: "nhoe" became "nhỏ", so "nhòe" reached the page as "nhò" with a
// letter silently gone, and the same happened to lòe, tòe, hòe, khòe, chòe,
// thòe, tròe, ngòe and the rest of that family.
std::optional<CorrectionResult> TryAdjacentKeyToneCorrection(
    std::wstring_view word,
    std::wstring_view raw_lower,
    CorrectionLevel level,
    InputMethod method) {
    
    if (level < CorrectionLevel::Advanced) {
        return std::nullopt;
    }

    if (raw_lower.empty()) {
        return std::nullopt;
    }

    std::wstring lower_typed;
    lower_typed.reserve(word.length());
    for (wchar_t c : word) {
        lower_typed.push_back(rules::ToLower(c));
    }
    // in_progress: this runs on a word the user is still typing, so a spelling
    // that is only a valid PREFIX still has to be left alone. "chet" cannot
    // carry no tone in finished Vietnamese, but it is on its way to "chết", and
    // reading its final t as a mistyped '5' turned it into "chê". The same for
    // dat, dit, dut, hat, hoc, hop, bac - every syllable that closes on p/t/c.
    const bool typed_reads_as_vietnamese =
        rules::IsValidVietnamese(lower_typed, /*in_progress=*/true);
    SecureEraseText(lower_typed);
    if (typed_reads_as_vietnamese) {
        return std::nullopt;
    }

    std::vector<std::wstring> matched_words;
    const bool is_vni = (method == InputMethod::VNI);

    // One engine for the whole sweep. On VNI this tries every position, so a
    // fresh Engine per candidate key meant O(n^2) constructions per call.
    // SecureClear() leaves the method and the disabled correction/protection
    // settings in place, so reuse is equivalent.
    Engine temp_engine(method);
    temp_engine.SetAutoCorrect(false);
    temp_engine.SetEnglishProtectionLevel(EnglishProtectionLevel::Off);
    temp_engine.SetSmartContextProtection(false);

    for (size_t i = 0; i < raw_lower.length(); ++i) {
        if (!is_vni && i != raw_lower.length() - 1) {
            continue;
        }

        wchar_t typo_key = raw_lower[i];
        std::vector<wchar_t> candidates = GetNearbyDauKeys(typo_key, method);
        if (candidates.empty()) {
            continue;
        }

        for (wchar_t correct_key : candidates) {
            if (correct_key == typo_key) {
                continue;
            }

            std::wstring candidate_raw(raw_lower);
            candidate_raw[i] = correct_key;

            temp_engine.SecureClear();
            for (wchar_t ch : candidate_raw) {
                temp_engine.ProcessKey(ch);
            }
            
            std::wstring candidate_word = temp_engine.GetDisplayString();
            std::wstring lower_candidate_word;
            lower_candidate_word.reserve(candidate_word.length());
            for (wchar_t c : candidate_word) {
                lower_candidate_word.push_back(rules::ToLower(c));
            }

            if (IsInDictionary(lower_candidate_word)) {
                if (std::find(matched_words.begin(), matched_words.end(), candidate_word) == matched_words.end()) {
                    matched_words.push_back(candidate_word);
                }
            }
        }
    }

    temp_engine.SecureClear();

    if (matched_words.size() == 1) {
        CorrectionResult result;
        result.word = PreserveCasing(word, matched_words[0]);
        result.kind = CorrectionKind::AdjacentKeySwap;
        result.score = 900;
        result.changed = true;
        result.high_confidence = true;
        return result;
    }

    return std::nullopt;
}

std::wstring NormalizeModifierBeforeVowel(std::wstring_view raw, InputMethod method) {
    std::wstring result(raw);
    bool is_vni = (method == InputMethod::VNI);

    auto is_vowel = [](wchar_t c) {
        wchar_t l = rules::ToLower(c);
        return l == L'a' || l == L'e' || l == L'o' || l == L'u' || l == L'i' || l == L'y';
    };

    auto can_modify = [](wchar_t mod, wchar_t vowel, bool is_vni) {
        if (is_vni) {
            if (mod == L'6') return vowel == L'a' || vowel == L'e' || vowel == L'o';
            if (mod == L'7') return vowel == L'u' || vowel == L'o';
            if (mod == L'8') return vowel == L'a';
            return false;
        } else {
            if (mod == L'w') return vowel == L'a' || vowel == L'o' || vowel == L'u';
            return false;
        }
    };

    auto is_tone = [](wchar_t c, bool is_vni) {
        if (is_vni) {
            return c >= L'1' && c <= L'5';
        } else {
            return c == L's' || c == L'f' || c == L'r' || c == L'x' || c == L'j';
        }
    };

    for (size_t i = 0; i < result.length(); ++i) {
        wchar_t m = result[i];
        bool is_mod = can_modify(m, L'a', is_vni) || can_modify(m, L'e', is_vni) || can_modify(m, L'o', is_vni) || can_modify(m, L'u', is_vni);
        bool is_t = is_tone(m, is_vni);

        if (is_mod || is_t) {
            // A word-initial r/s/x - and the r of a tr- onset - is a real
            // Vietnamese consonant, not a tone key that arrived too early.
            // Without this, raij was rewritten to airj and reached the page as
            // "ai" with the r gone, and traj became ta; 107 valid syllables
            // outside the dictionary lost their onset that way. Those two
            // positions are the only ones where a Telex tone letter can spell
            // an onset, so "chsa" is still relocated to "chas" -> cha.
            if (is_t && !is_vni &&
                (m == L'r' || m == L's' || m == L'x') &&
                (i == 0 ||
                 (i == 1 && m == L'r' && rules::ToLower(result[0]) == L't'))) {
                continue;
            }
            size_t target_vowel_pos = std::wstring::npos;
            for (size_t j = i + 1; j < result.length(); ++j) {
                wchar_t next_char = result[j];
                if (can_modify(next_char, L'a', is_vni) || can_modify(next_char, L'e', is_vni) || 
                    can_modify(next_char, L'o', is_vni) || can_modify(next_char, L'u', is_vni) || 
                    is_tone(next_char, is_vni)) {
                    break;
                }
                if (is_vowel(next_char)) {
                    if (is_t || can_modify(m, next_char, is_vni)) {
                        target_vowel_pos = j;
                        break;
                    }
                }
            }

            if (target_vowel_pos != std::wstring::npos) {
                result.erase(i, 1);
                result.insert(target_vowel_pos, 1, m);
                --i;
            }
        }
    }

    return result;
}

std::optional<CorrectionResult> TryModifierBeforeVowelCorrection(
    std::wstring_view word,
    std::wstring_view raw_lower,
    InputMethod method) {
    
    std::wstring normalized = NormalizeModifierBeforeVowel(raw_lower, method);
    if (normalized == raw_lower) {
        return std::nullopt;
    }

    // Process through the engine to see what it produces
    Engine temp_engine(method);
    temp_engine.SetAutoCorrect(false); // prevent infinite recursion
    temp_engine.SetEnglishProtectionLevel(EnglishProtectionLevel::Off);
    temp_engine.SetSmartContextProtection(false);
    
    for (wchar_t ch : normalized) {
        temp_engine.ProcessKey(ch);
    }

    std::wstring candidate = temp_engine.GetDisplayString();
    if (candidate.empty()) {
        return std::nullopt;
    }

    std::wstring lower_candidate;
    lower_candidate.reserve(candidate.length());
    for (wchar_t c : candidate) {
        lower_candidate.push_back(rules::ToLower(c));
    }

    if (IsInDictionary(lower_candidate)) {
        CorrectionResult result;
        result.word = PreserveCasing(word, candidate);
        result.kind = CorrectionKind::AdjacentKeySwap;
        result.score = 900;
        result.changed = true;
        result.high_confidence = true;
        return result;
    }

    return std::nullopt;
}

bool IsTelexMethod(InputMethod method) noexcept {
    return method == InputMethod::Telex ||
           method == InputMethod::SimpleTelex;
}

bool IsTelexCircumflexKey(wchar_t key) noexcept {
    return key == L'a' || key == L'e' || key == L'o';
}

std::optional<std::vector<size_t>> CollectShapeModifierPositions(
    std::wstring_view raw_lower,
    InputMethod method) {
    std::vector<size_t> positions;
    positions.reserve(4);

    if (method == InputMethod::VNI) {
        for (size_t i = 0; i < raw_lower.length(); ++i) {
            const wchar_t key = raw_lower[i];
            if (key < L'6' || key > L'8') {
                continue;
            }
            if (i > 0 && raw_lower[i - 1] == key) {
                return std::nullopt;
            }
            positions.push_back(i);
        }
        return positions;
    }

    if (!IsTelexMethod(method)) {
        return std::nullopt;
    }

    for (size_t i = 0; i < raw_lower.length(); ++i) {
        const wchar_t key = raw_lower[i];
        if (key == L'w') {
            if (i > 0 && raw_lower[i - 1] == L'w') {
                return std::nullopt;
            }
            positions.push_back(i);
            continue;
        }

        // The second key in aa/ee/oo is a shape modifier. Treat only the
        // first pair in a run as an event so aaa keeps its literal fallback.
        if (IsTelexCircumflexKey(key) && i > 0 &&
            raw_lower[i - 1] == key &&
            (i < 2 || raw_lower[i - 2] != key)) {
            positions.push_back(i);
        }
    }
    return positions;
}

std::wstring ReplayRawWithoutCorrection(
    std::wstring_view raw,
    InputMethod method) {
    Engine engine(method);
    engine.SetCorrectionLevel(CorrectionLevel::Off);
    engine.SetEnglishProtectionLevel(EnglishProtectionLevel::Off);
    engine.SetSmartContextProtection(false);
    for (const wchar_t key : raw) {
        engine.ProcessKey(key);
    }
    std::wstring display = engine.GetDisplayString();
    engine.SecureClear();
    return display;
}

std::optional<CorrectionResult> TryStaleModifierOverrideCorrection(
    std::wstring_view word,
    std::wstring_view raw_lower,
    InputMethod method) {
    if (rules::ValidateVietnameseSyllable(word) !=
        rules::SyllableValidity::Invalid) {
        return std::nullopt;
    }

    auto positions = CollectShapeModifierPositions(raw_lower, method);
    if (!positions || positions->size() < 2 ||
        positions->size() > kMaxShapeModifierEventsForCorrection) {
        return std::nullopt;
    }

    std::optional<std::wstring> unique_candidate;
    const size_t newest_modifier = positions->back();
    for (const size_t stale_modifier : *positions) {
        if (stale_modifier >= newest_modifier) {
            break;
        }

        std::wstring candidate_raw(raw_lower);
        candidate_raw.erase(stale_modifier, 1);
        std::wstring candidate = ReplayRawWithoutCorrection(
            candidate_raw, method);
        if (candidate == word ||
            rules::ValidateVietnameseSyllable(candidate) ==
                rules::SyllableValidity::Invalid) {
            continue;
        }

        if (unique_candidate && *unique_candidate != candidate) {
            return std::nullopt;
        }
        unique_candidate = std::move(candidate);
    }

    if (!unique_candidate) {
        return std::nullopt;
    }

    CorrectionResult result;
    result.word = PreserveCasing(word, *unique_candidate);
    result.kind = CorrectionKind::StaleModifierOverride;
    result.score = 950;
    result.changed = true;
    result.high_confidence = true;
    return result;
}

inline wchar_t StripAccentCharacter(wchar_t character) {
    rules::VowelData vowel{};
    if (rules::GetVowelData(character, vowel)) {
        return vowel.raw;
    }
    if (character == L'đ' || character == L'Đ') {
        return L'd';
    }
    return rules::ToLower(character);
}

inline constexpr size_t kMaxDamerauWordLength = 14;

struct FlatDictionaryWord {
    std::array<wchar_t, kMaxDamerauWordLength> characters{};
    unsigned char length = 0;
    // Which letters the word uses, ignoring order and repetition. Two strings
    // one edit apart differ by at most one letter on each side, so their masks
    // can differ by at most two bits - a two-instruction rejection that keeps
    // the scan off the dynamic-programming matrix for almost every entry.
    uint32_t letter_mask = 0;
};

inline uint32_t LetterMaskBit(wchar_t character) noexcept {
    if (character >= L'a' && character <= L'z') {
        return 1u << static_cast<unsigned>(character - L'a');
    }
    return 1u << 26;
}

const std::array<FlatDictionaryWord, DICTIONARY_SIZE>&
FlatDamerauDictionary() {
    static const auto flat_words =
        [] {
            auto result = std::make_unique<
                std::array<FlatDictionaryWord, DICTIONARY_SIZE>>();
            for (size_t word_index = 0; word_index < DICTIONARY_SIZE;
                 ++word_index) {
                const std::wstring_view word = DICTIONARY[word_index];
                if (word.length() > kMaxDamerauWordLength) {
                    continue;
                }
                (*result)[word_index].length =
                    static_cast<unsigned char>(word.length());
                uint32_t letter_mask = 0;
                for (size_t character_index = 0;
                     character_index < word.length(); ++character_index) {
                    const wchar_t flat_character =
                        StripAccentCharacter(word[character_index]);
                    (*result)[word_index].characters[character_index] =
                        flat_character;
                    letter_mask |= LetterMaskBit(flat_character);
                }
                (*result)[word_index].letter_mask = letter_mask;
            }
            return result;
        }();
    return *flat_words;
}

std::wstring StripAllAccents(std::wstring_view str) {
    std::wstring result;
    result.reserve(str.length());
    for (wchar_t character : str) {
        result.push_back(StripAccentCharacter(character));
    }
    return result;
}

size_t LengthDifference(size_t first, size_t second) noexcept {
    return first > second ? first - second : second - first;
}

size_t CalculateBoundedDamerauLevenshtein(
    std::wstring_view flat_input,
    std::wstring_view flat_dictionary_word,
    size_t max_distance) {
    const size_t input_length = flat_input.length();
    const size_t dictionary_length = flat_dictionary_word.length();
    const size_t rejected_distance = max_distance + 1;
    if (input_length > kMaxDamerauWordLength ||
        dictionary_length > kMaxDamerauWordLength ||
        LengthDifference(input_length, dictionary_length) > max_distance) {
        return rejected_distance;
    }

    constexpr size_t kMatrixExtent = kMaxDamerauWordLength + 2;
    std::array<unsigned char, kMatrixExtent * kMatrixExtent> distance{};
    distance.fill(static_cast<unsigned char>(rejected_distance));
    const auto cell = [&distance](size_t row, size_t column)
        -> unsigned char& {
        return distance[row * kMatrixExtent + column];
    };
    cell(0, 0) = 0;
    for (size_t index = 1;
         index <= input_length && index <= max_distance; ++index) {
        cell(index, 0) = static_cast<unsigned char>(index);
    }
    for (size_t index = 1;
         index <= dictionary_length && index <= max_distance; ++index) {
        cell(0, index) = static_cast<unsigned char>(index);
    }

    for (size_t input_index = 1; input_index <= input_length; ++input_index) {
        const size_t dictionary_begin = input_index > max_distance
            ? input_index - max_distance
            : 1;
        const size_t dictionary_end = (std::min)(
            dictionary_length, input_index + max_distance);
        for (size_t dictionary_index = dictionary_begin;
             dictionary_index <= dictionary_end; ++dictionary_index) {
            const wchar_t dictionary_character =
                flat_dictionary_word[dictionary_index - 1];
            const unsigned char substitution_cost =
                flat_input[input_index - 1] == dictionary_character ? 0 : 1;
            const unsigned char deletion = static_cast<unsigned char>(
                cell(input_index - 1, dictionary_index) + 1);
            const unsigned char insertion = static_cast<unsigned char>(
                cell(input_index, dictionary_index - 1) + 1);
            const unsigned char substitution = static_cast<unsigned char>(
                cell(input_index - 1, dictionary_index - 1) +
                substitution_cost);
            cell(input_index, dictionary_index) =
                (std::min)({deletion, insertion, substitution});

            if (input_index > 1 && dictionary_index > 1 &&
                flat_input[input_index - 1] ==
                    flat_dictionary_word[dictionary_index - 2] &&
                flat_input[input_index - 2] == dictionary_character) {
                const unsigned char transposition =
                    static_cast<unsigned char>(
                        cell(input_index - 2, dictionary_index - 2) +
                        substitution_cost);
                cell(input_index, dictionary_index) = (std::min)(
                    cell(input_index, dictionary_index), transposition);
            }
        }
    }
    return cell(input_length, dictionary_length);
}

std::optional<CorrectionResult> TryDamerauLevenshteinCorrection(
    std::wstring_view word,
    const std::wstring& lower_word,
    CorrectionLevel level) {
    if (level < CorrectionLevel::Experimental) return std::nullopt;
    std::wstring flat_lower = StripAllAccents(lower_word);
    if (flat_lower.length() > kMaxDamerauWordLength) {
        return std::nullopt;
    }
    if (flat_lower == lower_word) {
        bool has_tone_or_mod_key = false;
        for (wchar_t c : lower_word) {
            if (c == L'w' || c == L'f' || c == L'r' || c == L'x' || c == L'j' ||
                c == L'1' || c == L'2' || c == L'3' || c == L'4' || c == L'5' ||
                c == L'6' || c == L'7' || c == L'8' || c == L'9') {
                has_tone_or_mod_key = true;
                break;
            }
        }
        if (!has_tone_or_mod_key) {
            return std::nullopt;
        }
    }

    // One edit, at every length. Two edits let the rule delete the first two
    // letters of a long word: "bathroom" arrives here as the Telex-processed
    // "bathom", which is two deletions from "thom", and reached the page as
    // "thom". A Vietnamese syllable is at most seven letters, so a second edit
    // buys almost nothing - measured over the dictionary typo corpus it
    // recovered 9 extra words - while opening that class of rewrite.
    const size_t max_allowed_dist = 1;
    uint32_t input_letter_mask = 0;
    for (const wchar_t character : flat_lower) {
        input_letter_mask |= LetterMaskBit(character);
    }
    size_t min_dist = max_allowed_dist + 1;
    std::wstring_view best_match;
    size_t match_count = 0;
    const auto& flat_dictionary = FlatDamerauDictionary();

    for (size_t i = 0; i < DICTIONARY_SIZE; ++i) {
        std::wstring_view dict_word(DICTIONARY[i]);
        if (dict_word.length() > kMaxDamerauWordLength ||
            LengthDifference(dict_word.length(), flat_lower.length()) >
            max_allowed_dist) {
            continue;
        }

        const FlatDictionaryWord& flat_dict_word = flat_dictionary[i];
        if (std::popcount(
                input_letter_mask ^ flat_dict_word.letter_mask) >
            2 * static_cast<int>(max_allowed_dist)) {
            continue;
        }
        const size_t distance_limit =
            (std::min)(max_allowed_dist, min_dist);
        const size_t dist = CalculateBoundedDamerauLevenshtein(
            flat_lower,
            std::wstring_view(
                flat_dict_word.characters.data(), flat_dict_word.length),
            distance_limit);
        if (dist > max_allowed_dist) continue;
        if (dist == 0 && rules::IsValidVietnamese(lower_word, true)) {
            // The typed word already spells this entry. Stop, rather than drop
            // it from the count: skipping it let a neighbour one edit away
            // become the "only" candidate and overwrite a correctly spelled
            // word.
            return std::nullopt;
        }

        if (dist < min_dist) {
            min_dist = dist;
            best_match = dict_word;
            match_count = 1;
        } else if (dist == min_dist) {
            match_count++;
        }
    }

    if (match_count == 1 && !best_match.empty()) {
        CorrectionResult result;
        result.word = PreserveCasing(word, best_match);
        result.kind = CorrectionKind::EditDistance;
        result.score = 850;
        result.changed = true;
        // Not high confidence: this is the one rule that reaches a word the
        // keystrokes do not spell, so a caller weighing whether to rewrite
        // committed text should treat it differently from the adjacency rules.
        result.high_confidence = false;
        return result;
    }

    return std::nullopt;
}

bool IsEnglishConsonantClusterSuffix(std::wstring_view word) {
    if (word.length() < 3) return false;
    std::wstring_view suffix = word.substr(word.length() - 2);
    static const std::wstring_view english_suffixes[] = {
        L"st", L"ct", L"ld", L"nd", L"ft", L"mp", L"lt",
        L"sk", L"sp", L"pt", L"ll", L"ss", L"ff", L"zz",
        L"de", L"ce", L"ge", L"se", L"te", L"me", L"ne", L"ke", L"pe", L"re", L"ve", L"le"
    };
    for (const auto& s : english_suffixes) {
        if (suffix == s) return true;
    }
    return false;
}

inline constexpr std::wstring_view COMMON_ENGLISH_WORDS[] = {
        L"about", L"accept", L"access", L"action", L"active", L"add", L"admin", L"after", L"all", L"allow",
        L"also", L"am", L"an", L"and", L"api", L"app", L"apps", L"array", L"as", L"at", L"auto",
        L"back", L"bad", L"bag", L"bar", L"base", L"bat", L"be", L"best", L"beta", L"big",
        L"bit", L"blog", L"body", L"book", L"bool", L"bot", L"box", L"boy", L"bug", L"build", L"bus",
        L"but", L"buy", L"by", L"byte", L"call", L"can", L"cancel", L"car", L"card", L"cat",
        L"cell", L"chat", L"check", L"city", L"class", L"clean", L"clear", L"click", L"close", L"cmd",
        L"code", L"const", L"copy", L"core", L"cpu", L"css", L"custom", L"cut", L"data", L"date",
        L"db", L"debug", L"def", L"del", L"delete", L"demo", L"desk", L"dev", L"dir", L"disk",
        L"dl", L"dll", L"do", L"doc", L"docs", L"dog", L"done", L"dos", L"dot", L"download",
        L"draw", L"drive", L"drop", L"each", L"edit", L"else", L"em", L"email", L"end", L"env",
        L"err", L"error", L"etc", L"event", L"ex", L"exe", L"exec", L"exit", L"export", L"fact",
        L"fail", L"false", L"fan", L"fast", L"file", L"files", L"find", L"fix", L"flag", L"flow",
        L"font", L"foo", L"for", L"form", L"free", L"from", L"full", L"func", L"game", L"get", L"git",
        L"go", L"good", L"gpu", L"graph", L"group", L"had", L"has", L"have", L"he", L"head",
        L"help", L"her", L"his", L"home", L"host", L"hot", L"how", L"html", L"icon", L"id", L"if", L"image", L"img",
        L"import", L"in", L"index", L"info", L"init", L"input", L"int", L"into", L"ip", L"is",
        L"it", L"item", L"items", L"its", L"job", L"join", L"js", L"json", L"key", L"keys", L"kill",
        L"kind", L"lang", L"last", L"left", L"len", L"let", L"level", L"lib", L"like", L"line",
        L"link", L"list", L"load", L"lock", L"log", L"login", L"logs", L"long", L"look", L"loop", L"mac",
        L"main", L"make", L"map", L"maps", L"math", L"max", L"me", L"media", L"menu", L"min",
        L"mode", L"model", L"most", L"msg", L"my", L"name", L"net", L"new", L"next", L"no", L"node",
        L"none", L"not", L"now", L"null", L"num", L"of", L"off", L"ok", L"old", L"on", L"one",
        L"open", L"opt", L"option", L"or", L"order", L"org", L"os", L"out", L"output", L"pack",
        L"page", L"param", L"parse", L"pass", L"password", L"path", L"pdf", L"pen", L"ping", L"pipe", L"pkg",
        L"plan", L"play", L"plugin", L"png", L"point", L"port", L"post", L"print", L"pub", L"push",
        L"put", L"query", L"quit", L"ram", L"rank", L"raw", L"rd", L"read", L"real", L"ref",
        L"reg", L"remove", L"req", L"res", L"reset", L"result", L"root", L"row", L"run", L"save",
        L"scan", L"scope", L"search", L"see", L"select", L"set", L"she", L"shift", L"show", L"sit", L"site", L"size",
        L"skip", L"so", L"socket", L"sort", L"source", L"sql", L"src", L"st", L"stat", L"state",
        L"std", L"step", L"stop", L"str", L"string", L"struct", L"sub", L"success", L"sun", L"sync", L"sys",
        L"tab", L"table", L"tag", L"task", L"tax", L"team", L"temp", L"test", L"text", L"th", L"that",
        L"the", L"theme", L"there", L"these", L"this", L"time", L"title", L"to", L"tool", L"top", L"true", L"try",
        L"ts", L"two", L"txt", L"type", L"ui", L"unit", L"up", L"update", L"upload", L"url", L"us",
        L"usage", L"use", L"user", L"ux", L"val", L"value", L"var", L"version", L"view", L"views",
        L"vs", L"we", L"web", L"who", L"win", L"window", L"word", L"work", L"write", L"xml", L"zip"
};

static_assert(std::is_sorted(std::begin(COMMON_ENGLISH_WORDS), std::end(COMMON_ENGLISH_WORDS)));
static_assert(std::is_sorted(
    std::begin(data::STRONG_ENGLISH_PROTECTION_WORDS),
    std::end(data::STRONG_ENGLISH_PROTECTION_WORDS)));

constexpr bool EnglishProtectionTablesAreDisjoint() noexcept {
    for (const std::wstring_view strong_word :
         data::STRONG_ENGLISH_PROTECTION_WORDS) {
        for (const std::wstring_view common_word : COMMON_ENGLISH_WORDS) {
            if (strong_word == common_word) {
                return false;
            }
        }
    }
    return true;
}

static_assert(EnglishProtectionTablesAreDisjoint());

inline constexpr std::wstring_view CERTAIN_CODE_TERMS[] = {
    L"api", L"cmd", L"css", L"db", L"dll", L"exe", L"exec", L"gpu",
    L"html", L"img", L"js", L"json", L"pdf", L"pkg", L"png", L"res",
    L"sql", L"src", L"std", L"struct", L"ts", L"txt", L"ui", L"url", L"xml",
};

static_assert(std::is_sorted(std::begin(CERTAIN_CODE_TERMS), std::end(CERTAIN_CODE_TERMS)));

int CompareCaseInsensitive(std::wstring_view left, std::wstring_view right) noexcept {
    const size_t shared_length = (std::min)(left.length(), right.length());
    for (size_t i = 0; i < shared_length; ++i) {
        const wchar_t left_char = rules::ToLower(left[i]);
        const wchar_t right_char = rules::ToLower(right[i]);
        if (left_char < right_char) return -1;
        if (left_char > right_char) return 1;
    }
    if (left.length() < right.length()) return -1;
    if (left.length() > right.length()) return 1;
    return 0;
}

template <size_t N>
bool ContainsCaseInsensitive(
    const std::wstring_view (&sorted_values)[N],
    std::wstring_view value) noexcept {
    const auto it = std::lower_bound(
        std::begin(sorted_values), std::end(sorted_values), value,
        [](std::wstring_view candidate, std::wstring_view target) {
            return CompareCaseInsensitive(candidate, target) < 0;
        });
    return it != std::end(sorted_values) && CompareCaseInsensitive(*it, value) == 0;
}

EnglishLexiconTier LookupGeneratedEnglishLexicon(
    std::wstring_view word) noexcept {
    if (word.length() < data::kEnglishLexiconMinWordLength ||
        word.length() > data::kEnglishLexiconMaxWordLength) {
        return EnglishLexiconTier::None;
    }

    std::array<char, data::kEnglishLexiconMaxWordLength> normalized{};
    for (size_t index = 0; index < word.length(); ++index) {
        const wchar_t character = word[index];
        if (character >= L'a' && character <= L'z') {
            normalized[index] = static_cast<char>(character);
        } else if (character >= L'A' && character <= L'Z') {
            normalized[index] = static_cast<char>(character - L'A' + L'a');
        } else {
            return EnglishLexiconTier::None;
        }
    }

    const size_t first_letter =
        static_cast<size_t>(normalized[0] - 'a');
    const size_t length_offset =
        word.length() - data::kEnglishLexiconMinWordLength;
    const size_t bucket = length_offset * 26 + first_letter;
    const size_t begin_index = data::kEnglishLexiconBucketStarts[bucket];
    const size_t end_index = data::kEnglishLexiconBucketStarts[bucket + 1];
    const std::string_view target(normalized.data(), word.length());

    const auto begin = data::kEnglishLexiconOffsets.begin() + begin_index;
    const auto end = data::kEnglishLexiconOffsets.begin() + end_index;
    const auto found = std::lower_bound(
        begin, end, target,
        [length = word.length()](uint32_t offset, std::string_view value) {
            return std::string_view(
                       data::kEnglishLexiconBlob + offset, length) < value;
        });
    if (found == end ||
        std::string_view(
            data::kEnglishLexiconBlob + *found, word.length()) != target) {
        return EnglishLexiconTier::None;
    }

    const size_t index = static_cast<size_t>(
        std::distance(data::kEnglishLexiconOffsets.begin(), found));
    return static_cast<EnglishLexiconTier>(data::kEnglishLexiconTiers[index]);
}

bool IsDictionaryWordCaseInsensitive(std::wstring_view word) noexcept {
    const auto it = std::lower_bound(
        DICTIONARY, DICTIONARY + DICTIONARY_SIZE, word,
        [](std::wstring_view candidate, std::wstring_view target) {
            return CompareCaseInsensitive(candidate, target) < 0;
        });
    return it != DICTIONARY + DICTIONARY_SIZE &&
           CompareCaseInsensitive(*it, word) == 0;
}

bool EqualsCaseInsensitive(std::wstring_view left, std::wstring_view right) noexcept {
    return CompareCaseInsensitive(left, right) == 0;
}

void SecureEraseText(std::wstring& text) noexcept {
    if (!text.empty()) {
        SecureZeroMemory(text.data(), text.size() * sizeof(wchar_t));
        text.clear();
    }
}

bool IsVietnameseCoda(std::wstring_view coda) noexcept {
    static constexpr std::wstring_view CODAS[] = {
        L"c", L"ch", L"m", L"n", L"ng", L"nh", L"p", L"t",
    };
    return std::binary_search(std::begin(CODAS), std::end(CODAS), coda);
}

bool MatchesToneBeforeCoda(
    std::wstring_view raw_keys,
    std::wstring_view canonical_keys,
    InputMethod method) noexcept {
    if (raw_keys.length() != canonical_keys.length() || canonical_keys.length() < 3 ||
        !rules::IsToneKey(canonical_keys.back(), method)) {
        return false;
    }

    for (const size_t coda_length : {size_t{1}, size_t{2}}) {
        if (canonical_keys.length() <= coda_length + 1) continue;
        const size_t prefix_length = canonical_keys.length() - coda_length - 1;
        const std::wstring_view coda = canonical_keys.substr(prefix_length, coda_length);
        if (!IsVietnameseCoda(coda)) continue;

        bool matches = true;
        for (size_t i = 0; i < prefix_length; ++i) {
            matches = matches &&
                rules::ToLower(raw_keys[i]) == rules::ToLower(canonical_keys[i]);
        }
        matches = matches && rules::ToLower(raw_keys[prefix_length]) ==
                               rules::ToLower(canonical_keys.back());
        for (size_t i = 0; i < coda_length; ++i) {
            matches = matches &&
                rules::ToLower(raw_keys[prefix_length + 1 + i]) ==
                rules::ToLower(canonical_keys[prefix_length + i]);
        }
        if (matches) return true;
    }
    return false;
}

bool MatchesCanonicalVietnameseRaw(
    std::wstring_view raw_keys,
    std::wstring_view processed_word,
    InputMethod method) {
    std::wstring canonical_keys = rules::ReconstructRawKeys(processed_word, method);
    const bool matches = EqualsCaseInsensitive(raw_keys, canonical_keys) ||
        MatchesToneBeforeCoda(raw_keys, canonical_keys, method);
    SecureEraseText(canonical_keys);
    return matches;
}

bool IsAsciiCodeToken(std::wstring_view token) {
    if (ContainsCaseInsensitive(CERTAIN_CODE_TERMS, token)) {
        return true;
    }

    const wchar_t first = token.empty() ? L'\0' : rules::ToLower(token.front());
    if (token.length() < 2 || first < L'a' || first > L'z') {
        return false;
    }

    const size_t digit_start = token.find_first_of(L"0123456789");
    if (digit_start == std::wstring_view::npos || digit_start == 0) {
        return false;
    }
    for (size_t i = digit_start; i < token.length(); ++i) {
        if (token[i] < L'0' || token[i] > L'9') {
            return false;
        }
    }

    static constexpr std::wstring_view CODE_PREFIXES[] = {
        L"arm", L"ipv", L"sha", L"utf", L"win", L"windows", L"x",
    };
    return ContainsCaseInsensitive(CODE_PREFIXES, token.substr(0, digit_start));
}

constexpr bool SegmentationBigramsAreValid() noexcept {
    for (const std::wstring_view phrase : data::COMMON_BIGRAMS) {
        const size_t separator = phrase.find(L' ');
        if (separator == std::wstring_view::npos || separator == 0 ||
            separator + 1 >= phrase.length() ||
            phrase.find(L' ', separator + 1) != std::wstring_view::npos) {
            return false;
        }
    }
    return true;
}

static_assert(SegmentationBigramsAreValid());

std::wstring BuildSegmentationShapeKey(std::wstring_view value) {
    std::wstring key;
    key.reserve(value.length());
    for (const wchar_t character : value) {
        if (character == L' ') {
            continue;
        }
        rules::VowelData vowel{};
        if (rules::GetVowelData(character, vowel)) {
            key.push_back(vowel.raw);
            continue;
        }
        const wchar_t lower = rules::ToLower(character);
        if (lower == L'\u0111') {
            key.push_back(L'd');
        } else if (lower >= L'a' && lower <= L'z') {
            key.push_back(lower);
        } else {
            SecureEraseText(key);
            return {};
        }
    }
    return key;
}

bool TryGetSingleTone(std::wstring_view value, ToneMark& tone) noexcept {
    tone = ToneMark::None;
    for (const wchar_t character : value) {
        rules::VowelData vowel{};
        if (!rules::GetVowelData(character, vowel) ||
            vowel.tone == ToneMark::None) {
            continue;
        }
        if (tone != ToneMark::None && tone != vowel.tone) {
            return false;
        }
        tone = vowel.tone;
    }
    return true;
}

wchar_t ToneKeyForSegmentation(ToneMark tone, InputMethod method) noexcept {
    if (method == InputMethod::VNI) {
        switch (tone) {
            case ToneMark::Sacute: return L'1';
            case ToneMark::Grave: return L'2';
            case ToneMark::Hook: return L'3';
            case ToneMark::Tilde: return L'4';
            case ToneMark::Dot: return L'5';
            default: return L'\0';
        }
    }
    switch (tone) {
        case ToneMark::Sacute: return L's';
        case ToneMark::Grave: return L'f';
        case ToneMark::Hook: return L'r';
        case ToneMark::Tilde: return L'x';
        case ToneMark::Dot: return L'j';
        default: return L'\0';
    }
}

std::optional<ToneMark> ToneFromSegmentationKey(
    wchar_t key,
    InputMethod method) noexcept {
    const wchar_t lower_key = rules::ToLower(key);
    for (const ToneMark tone : {
             ToneMark::Sacute, ToneMark::Grave, ToneMark::Hook,
             ToneMark::Tilde, ToneMark::Dot}) {
        if (ToneKeyForSegmentation(tone, method) == lower_key) {
            return tone;
        }
    }
    return std::nullopt;
}

bool IsBoundedSegmentationRawToken(std::wstring_view raw_token) noexcept {
    if (raw_token.empty() ||
        raw_token.length() > kMaxAutoWordSegmentationRawLength) {
        return false;
    }
    for (const wchar_t character : raw_token) {
        const wchar_t lower = rules::ToLower(character);
        if (!((lower >= L'a' && lower <= L'z') ||
              (lower >= L'0' && lower <= L'9'))) {
            return false;
        }
    }
    return true;
}

inline constexpr size_t kMaxTransposedKeysRawLength = 16;

// Telex gives a doubled key a meaning: aa/ee/oo are circumflexes, dd is d-stroke,
// ww is a literal w, and a doubled tone key escapes it. VNI gives one to a
// doubled digit. Every other doubled key spells nothing in Vietnamese.
bool IsMeaningfulWhenDoubled(wchar_t key, InputMethod method) noexcept {
    if (method == InputMethod::VNI) {
        return key >= L'0' && key <= L'9';
    }
    return key == L'a' || key == L'e' || key == L'o' || key == L'd' ||
           key == L'w' || key == L's' || key == L'f' || key == L'r' ||
           key == L'x' || key == L'j' || key == L'z';
}

bool HasBouncedKey(std::wstring_view raw_lower, InputMethod method) noexcept {
    for (size_t i = 0; i + 1 < raw_lower.length(); ++i) {
        if (raw_lower[i] == raw_lower[i + 1] &&
            !IsMeaningfulWhenDoubled(raw_lower[i], method)) {
            return true;
        }
    }
    return false;
}

// The word as typed still reads as a Vietnamese syllable in progress, so
// nothing is wrong with it yet. Both rules below rewrite the whole composition,
// and without this they do it while the user is still typing: measured over the
// dictionary, the transposition rule rewrote 2883 in-progress words without the
// check and 14 with it.
bool ReadsAsVietnameseInProgress(const std::wstring& lower_word) {
    return rules::IsValidVietnamese(lower_word, /*in_progress=*/true);
}

// Replays one raw variant with correction off and returns its lowercase
// surface, but only when that surface is a dictionary word.
std::optional<std::wstring> DictionarySurfaceForRaw(
    Engine& replay,
    std::wstring_view candidate_raw) {
    replay.SecureClear();
    for (const wchar_t key : candidate_raw) {
        replay.ProcessKey(key);
    }
    std::wstring surface = replay.GetDisplayString();
    std::wstring lower;
    lower.reserve(surface.length());
    for (const wchar_t character : surface) {
        lower.push_back(rules::ToLower(character));
    }
    SecureEraseText(surface);
    if (lower.empty() || !IsInDictionary(lower)) {
        SecureEraseText(lower);
        return std::nullopt;
    }
    return lower;
}

// Drops a bounced key. This is the one new rule cheap and safe enough for
// Normal: it does not choose between spellings, it removes a keystroke that
// could not have been meant, and it only runs when the raw actually holds such
// a double - measured at 0.0% of keystrokes while typing normally, with no
// in-progress rewrites at all.
std::optional<CorrectionResult> TryBouncedKeyCorrection(
    std::wstring_view word,
    const std::wstring& lower_word,
    std::wstring_view raw_lower,
    CorrectionLevel level,
    InputMethod method) {
    if (level < CorrectionLevel::Normal || raw_lower.length() < 3 ||
        !HasBouncedKey(raw_lower, method) ||
        ReadsAsVietnameseInProgress(lower_word)) {
        return std::nullopt;
    }

    Engine replay(method);
    replay.SetCorrectionLevel(CorrectionLevel::Off);
    replay.SetEnglishProtectionLevel(EnglishProtectionLevel::Off);
    replay.SetSmartContextProtection(false);

    std::optional<std::wstring> unique_candidate;
    bool ambiguous = false;
    for (size_t i = 0; i + 1 < raw_lower.length() && !ambiguous; ++i) {
        if (raw_lower[i] != raw_lower[i + 1] ||
            IsMeaningfulWhenDoubled(raw_lower[i], method)) {
            continue;
        }
        std::wstring candidate_raw(raw_lower);
        candidate_raw.erase(i, 1);
        auto candidate = DictionarySurfaceForRaw(replay, candidate_raw);
        SecureEraseText(candidate_raw);
        if (!candidate) {
            continue;
        }
        if (unique_candidate && *unique_candidate != *candidate) {
            ambiguous = true;
            break;
        }
        unique_candidate = std::move(candidate);
    }
    replay.SecureClear();
    if (ambiguous || !unique_candidate) {
        return std::nullopt;
    }

    CorrectionResult result;
    result.word = PreserveCasing(word, *unique_candidate);
    result.kind = CorrectionKind::KeyBounce;
    result.score = 900;
    result.changed = true;
    result.high_confidence = true;
    return result;
}

// Two keys that arrived in the wrong order, at any position. The Advanced rules
// already repair a swap of the first two or the last two letters of the
// DISPLAY word, and Engine::ShouldRepairRolledOnset repairs a rolled onset when
// the keystrokes were close enough in time; this covers the rest of the word,
// on the raw keys, with the same uniqueness gate. Experimental: it explores
// n-1 candidates, so it guesses more than anything at Normal.
std::optional<CorrectionResult> TryTransposedKeysCorrection(
    std::wstring_view word,
    const std::wstring& lower_word,
    std::wstring_view raw_lower,
    CorrectionLevel level,
    InputMethod method) {
    if (level < CorrectionLevel::Experimental || raw_lower.length() < 3 ||
        raw_lower.length() > kMaxTransposedKeysRawLength ||
        ReadsAsVietnameseInProgress(lower_word)) {
        return std::nullopt;
    }

    Engine replay(method);
    replay.SetCorrectionLevel(CorrectionLevel::Off);
    replay.SetEnglishProtectionLevel(EnglishProtectionLevel::Off);
    replay.SetSmartContextProtection(false);

    std::optional<std::wstring> unique_candidate;
    bool ambiguous = false;
    for (size_t i = 0; i + 1 < raw_lower.length() && !ambiguous; ++i) {
        if (raw_lower[i] == raw_lower[i + 1]) {
            continue;
        }
        std::wstring candidate_raw(raw_lower);
        std::swap(candidate_raw[i], candidate_raw[i + 1]);
        auto candidate = DictionarySurfaceForRaw(replay, candidate_raw);
        SecureEraseText(candidate_raw);
        if (!candidate) {
            continue;
        }
        if (unique_candidate && *unique_candidate != *candidate) {
            ambiguous = true;
            break;
        }
        unique_candidate = std::move(candidate);
    }
    replay.SecureClear();
    if (ambiguous || !unique_candidate) {
        return std::nullopt;
    }

    CorrectionResult result;
    result.word = PreserveCasing(word, *unique_candidate);
    result.kind = CorrectionKind::TransposedKeys;
    result.score = 850;
    // Same standing as the edit-distance rule: it reaches a word the keystrokes
    // do not spell.
    result.changed = true;
    result.high_confidence = false;
    return result;
}

// One engine, reset between boundaries. Building a fresh Engine for each of
// the three replays at each split meant 57 constructions for a 20-character
// token; SecureClear() leaves the method and the disabled correction/protection
// settings in place, so reuse is equivalent.
std::wstring ReplaySegmentationEvidence(
    Engine& replay,
    std::wstring_view raw_token) {
    replay.SecureClear();
    for (const wchar_t key : raw_token) {
        replay.ProcessKey(key);
    }
    return replay.GetDisplayString();
}

std::wstring ToLowerCopy(std::wstring_view value) {
    std::wstring lower;
    lower.reserve(value.length());
    for (const wchar_t character : value) {
        lower.push_back(rules::ToLower(character));
    }
    return lower;
}

const std::unordered_map<std::wstring, std::vector<uint16_t>>&
BigramsBySecondToken() {
    static const auto index = [] {
        std::unordered_map<std::wstring, std::vector<uint16_t>> map;
        map.reserve(data::COMMON_BIGRAMS_SIZE);
        for (size_t bigram_index = 0;
             bigram_index < data::COMMON_BIGRAMS_SIZE; ++bigram_index) {
            const std::wstring_view phrase = data::COMMON_BIGRAMS[bigram_index];
            const size_t separator = phrase.find(L' ');
            if (separator == std::wstring_view::npos) {
                continue;
            }
            map[ToLowerCopy(phrase.substr(separator + 1))].push_back(
                static_cast<uint16_t>(bigram_index));
        }
        return map;
    }();
    return index;
}

// The shape key of a phrase's first token never changes, so build the table
// once instead of once per phrase per boundary.
const std::vector<std::wstring>& BigramFirstTokenShapeKeys() {
    static const auto keys = [] {
        std::vector<std::wstring> result;
        result.reserve(data::COMMON_BIGRAMS_SIZE);
        for (const std::wstring_view phrase : data::COMMON_BIGRAMS) {
            result.push_back(
                BuildSegmentationShapeKey(phrase.substr(0, phrase.find(L' '))));
        }
        return result;
    }();
    return keys;
}

} // namespace

void WarmUpEditDistanceIndex() noexcept {
    // The flattened dictionary is a function-local static of about 220 KB, so
    // the first Experimental correction used to pay for building it - a visible
    // hitch on the first keystroke after switching to that level. Engine calls
    // this when the level is raised instead. Failure here is not fatal: the
    // table is simply built lazily on first use, as before.
    try {
        (void)FlatDamerauDictionary();
    } catch (...) {
    }
}

std::span<const uint16_t> CuratedVietnameseBigramsWithSecond(
    std::wstring_view second) {
    std::wstring key = ToLowerCopy(second);
    const auto& index = BigramsBySecondToken();
    const auto found = index.find(key);
    SecureEraseText(key);
    if (found == index.end()) {
        return {};
    }
    return found->second;
}

std::span<const std::wstring_view> CommonEnglishWords() noexcept {
    return COMMON_ENGLISH_WORDS;
}

std::span<const std::wstring_view> StrongEnglishProtectionWords() noexcept {
    return data::STRONG_ENGLISH_PROTECTION_WORDS;
}

bool CommonEnglishWordsAreSorted() noexcept {
    return std::is_sorted(
               std::begin(COMMON_ENGLISH_WORDS),
               std::end(COMMON_ENGLISH_WORDS)) &&
        std::is_sorted(
               std::begin(data::STRONG_ENGLISH_PROTECTION_WORDS),
               std::end(data::STRONG_ENGLISH_PROTECTION_WORDS));
}

bool IsCommonEnglishWord(std::wstring_view word) {
    return ContainsCaseInsensitive(COMMON_ENGLISH_WORDS, word) ||
        ContainsCaseInsensitive(
            data::STRONG_ENGLISH_PROTECTION_WORDS, word);
}

bool IsStrongEnglishProtectionWord(std::wstring_view word) {
    return ContainsCaseInsensitive(
        data::STRONG_ENGLISH_PROTECTION_WORDS, word);
}

// Spellings that are already what the typist meant, even though they sit one
// modifier key away from a Vietnamese word. "nguyen" is the romanised surname:
// the Missing Modifier rule finds "nguyên" in the dictionary and rewrites a
// word that was never wrong. These are not English, so they do not belong in
// the English lists, and they are not misspellings, so no rule should repair
// them.
//
// Sorted: ContainsCaseInsensitive binary-searches this.
inline constexpr std::wstring_view UNCORRECTED_SPELLINGS[] = {
    L"nguyen",
};

// Matched on the raw keys rather than the display string. Someone who wants
// "nguyên" presses the modifier - "nguyeen" in Telex, "nguye6n" in VNI - and so
// never produces this key sequence; nothing they meant is blocked.
bool IsUncorrectedSpelling(std::wstring_view raw_keys) {
    return ContainsCaseInsensitive(UNCORRECTED_SPELLINGS, raw_keys);
}

EnglishLexiconTier LookupBilingualEnglishWord(
    std::wstring_view word) noexcept {
    return LookupGeneratedEnglishLexicon(word);
}

size_t BilingualEnglishWordCount() noexcept {
    return data::kEnglishLexiconWordCount;
}

size_t BilingualEnglishCommonWordCount() noexcept {
    return data::kEnglishLexiconCommonCount;
}

size_t BilingualEnglishExtendedWordCount() noexcept {
    return data::kEnglishLexiconExtendedCount;
}

bool HasProtectedEnglishBigramSplit(std::wstring_view raw_token) {
    for (size_t split = 1; split < raw_token.length(); ++split) {
        if (IsCommonEnglishWord(raw_token.substr(0, split)) &&
            IsCommonEnglishWord(raw_token.substr(split))) {
            return true;
        }
    }
    return false;
}

EnglishProtectionDecision ClassifyEnglishProtection(
    std::wstring_view raw_keys,
    std::wstring_view processed_word,
    InputMethod method,
    EnglishProtectionLevel level) {
    if (level == EnglishProtectionLevel::Off || raw_keys.empty()) {
        return EnglishProtectionDecision::None;
    }

    const bool strong_english = IsStrongEnglishProtectionWord(raw_keys);
    const EnglishLexiconTier lexicon_tier =
        LookupGeneratedEnglishLexicon(raw_keys);
    const bool common_english = strong_english ||
        ContainsCaseInsensitive(COMMON_ENGLISH_WORDS, raw_keys) ||
        lexicon_tier == EnglishLexiconTier::Common;
    const bool extended_english = common_english ||
        lexicon_tier == EnglishLexiconTier::Extended;
    const bool code_token = IsAsciiCodeToken(raw_keys);
    if (strong_english) {
        return EnglishProtectionDecision::PreserveRaw;
    }
    if (level == EnglishProtectionLevel::EnglishFirst) {
        return (extended_english || code_token)
            ? EnglishProtectionDecision::PreserveRaw
            : EnglishProtectionDecision::None;
    }

    if (method == InputMethod::VNI) {
        return (common_english || code_token)
            ? EnglishProtectionDecision::PreserveRaw
            : EnglishProtectionDecision::None;
    }

    if (code_token) {
        return EnglishProtectionDecision::PreserveRaw;
    }

    if (!common_english) {
        return EnglishProtectionDecision::None;
    }
    const bool canonical_vietnamese =
        IsDictionaryWordCaseInsensitive(processed_word) &&
        MatchesCanonicalVietnameseRaw(raw_keys, processed_word, method);
    return canonical_vietnamese
        ? EnglishProtectionDecision::AmbiguousVietnamese
        : EnglishProtectionDecision::PreserveRaw;
}

std::optional<WordSegmentationCandidate> BuildAutoWordSegmentationCandidate(
    std::wstring_view raw_token,
    std::wstring_view display_token,
    InputMethod method,
    CorrectionLevel level) {
    if (level != CorrectionLevel::Experimental ||
        !IsBoundedSegmentationRawToken(raw_token) || display_token.empty() ||
        display_token.length() > kMaxAutoWordSegmentationRawLength ||
        display_token.find(L' ') != std::wstring_view::npos ||
        HasProtectedEnglishBigramSplit(raw_token)) {
        return std::nullopt;
    }

    constexpr int kMinimumScore = 1500;
    constexpr int kMinimumRunnerUpMargin = 150;
    constexpr int kBigramPriorScore = 1000;
    std::array<int, data::COMMON_BIGRAMS_SIZE> scores;
    scores.fill(-1);
    const auto trailing_tone =
        ToneFromSegmentationKey(raw_token.back(), method);
    const std::vector<std::wstring>& first_shape_keys =
        BigramFirstTokenShapeKeys();

    Engine replay(method);
    replay.SetCorrectionLevel(CorrectionLevel::Off);
    replay.SetEnglishProtectionLevel(EnglishProtectionLevel::Off);
    replay.SetSmartContextProtection(false);

    // Try every boundary. Direct evidence handles independently marked words;
    // shared-tone evidence also replays the trailing key on the first word,
    // so VNI tut+1/tat1 and Telex tut+s/tats are evaluated like normal input.
    // Only a phrase whose second token matches can score at all, so the index
    // gives exactly the phrases the old full sweep would have scored.
    for (size_t split = 1; split < raw_token.length(); ++split) {
        std::wstring first_surface = ReplaySegmentationEvidence(
            replay, raw_token.substr(0, split));
        std::wstring second_surface = ReplaySegmentationEvidence(
            replay, raw_token.substr(split));
        std::wstring shared_tone_first_surface;
        if (trailing_tone) {
            std::wstring first_raw_with_shared_tone;
            first_raw_with_shared_tone.reserve(split + 1);
            first_raw_with_shared_tone.append(raw_token.substr(0, split));
            first_raw_with_shared_tone.push_back(raw_token.back());
            shared_tone_first_surface = ReplaySegmentationEvidence(
                replay, first_raw_with_shared_tone);
            SecureEraseText(first_raw_with_shared_tone);
        }

        ToneMark first_tone = ToneMark::None;
        const bool can_infer_first_tone =
            TryGetSingleTone(first_surface, first_tone) &&
            first_tone == ToneMark::None;
        std::wstring first_shape_key;
        if (can_infer_first_tone) {
            first_shape_key = BuildSegmentationShapeKey(first_surface);
        }

        for (const uint16_t index :
             CuratedVietnameseBigramsWithSecond(second_surface)) {
            const std::wstring_view candidate = data::COMMON_BIGRAMS[index];
            const std::wstring_view candidate_first =
                candidate.substr(0, candidate.find(L' '));
            int score = -1;
            if (EqualsCaseInsensitive(first_surface, candidate_first)) {
                score = kBigramPriorScore + 700;
            }
            if (trailing_tone &&
                EqualsCaseInsensitive(
                    shared_tone_first_surface, candidate_first)) {
                score = (std::max)(score, kBigramPriorScore + 650);
            }
            if (can_infer_first_tone &&
                EqualsCaseInsensitive(
                    first_shape_key, first_shape_keys[index])) {
                score = (std::max)(score, kBigramPriorScore + 500);
            }
            scores[index] = (std::max)(scores[index], score);
        }

        SecureEraseText(first_surface);
        SecureEraseText(second_surface);
        SecureEraseText(shared_tone_first_surface);
        SecureEraseText(first_shape_key);
    }
    replay.SecureClear();

    int best_score = -1;
    int runner_up_score = -1;
    size_t best_index = data::COMMON_BIGRAMS_SIZE;
    for (size_t index = 0; index < scores.size(); ++index) {
        if (scores[index] > best_score) {
            runner_up_score = best_score;
            best_score = scores[index];
            best_index = index;
        } else if (scores[index] > runner_up_score) {
            runner_up_score = scores[index];
        }
    }

    if (best_index == data::COMMON_BIGRAMS_SIZE ||
        best_score < kMinimumScore ||
        (runner_up_score >= 0 &&
         best_score - runner_up_score < kMinimumRunnerUpMargin)) {
        return std::nullopt;
    }

    WordSegmentationCandidate result;
    result.text = PreserveCasing(
        display_token, data::COMMON_BIGRAMS[best_index]);
    result.score = best_score;
    result.runner_up_score = (std::max)(0, runner_up_score);
    result.high_confidence = true;
    return result;
}

std::span<const std::wstring_view> CuratedVietnameseBigrams() noexcept {
    return data::COMMON_BIGRAMS;
}

bool HasCuratedVietnameseBigram(std::wstring_view phrase) noexcept {
    const size_t separator = phrase.find(L' ');
    if (separator == std::wstring_view::npos || separator == 0 ||
        separator + 1 >= phrase.length() ||
        phrase.find(L' ', separator + 1) != std::wstring_view::npos) {
        return false;
    }
    return std::ranges::any_of(
        data::COMMON_BIGRAMS,
        [phrase](std::wstring_view candidate) {
            return candidate == phrase;
        });
}

size_t CuratedVietnameseBigramCount() noexcept {
    return data::COMMON_BIGRAMS_SIZE;
}

bool HasCuratedWordSegmentationPhrase(std::wstring_view phrase) noexcept {
    return HasCuratedVietnameseBigram(phrase);
}

size_t CuratedWordSegmentationBigramCount() noexcept {
    return CuratedVietnameseBigramCount();
}

std::wstring CorrectWord(std::wstring_view word, std::wstring_view raw_keys) {
    return CorrectWordEx(word, raw_keys, CorrectionLevel::Normal, InputMethod::Telex).word;
}

CorrectionResult CorrectWordEx(
    std::wstring_view word,
    std::wstring_view raw_keys,
    CorrectionLevel level,
    InputMethod method,
    EnglishProtectionLevel english_protection_level) {
    CorrectionResult result;
    result.word = std::wstring(word);
    result.kind = CorrectionKind::None;
    result.score = 0;
    result.changed = false;
    result.high_confidence = false;

    if (word.empty()) {
        return result;
    }

    if (std::iswdigit(word[0])) {
        return result;
    }

    if (level == CorrectionLevel::Off) {
        return result;
    }

    // 1. Convert word and raw_keys to lowercase
    std::wstring lower_word;
    lower_word.reserve(word.length());
    for (wchar_t c : word) {
        lower_word.push_back(rules::ToLower(c));
    }

    std::wstring raw_lower;
    raw_lower.reserve(raw_keys.length());
    for (wchar_t c : raw_keys) raw_lower.push_back(rules::ToLower(c));

    // Ahead of every rule, including the Normal ones: these spellings are the
    // finished word, not a word to be repaired.
    if (IsUncorrectedSpelling(raw_lower)) {
        return result;
    }

    const auto english_decision = ClassifyEnglishProtection(
        raw_keys, word, method, english_protection_level);
    if (english_decision == EnglishProtectionDecision::PreserveRaw) {
        result.word = std::wstring(raw_keys);
        result.changed = result.word != word;
        return result;
    }
    if (english_decision == EnglishProtectionDecision::AmbiguousVietnamese) {
        return result;
    }

    if (IsEnglishConsonantClusterSuffix(lower_word) || IsEnglishConsonantClusterSuffix(raw_lower)) {
        return result;
    }

    if (IsInDictionary(lower_word)) {
        result.score = 1000;
        result.high_confidence = true;
        return result;
    }

    // The Advanced and Experimental rules below guess: they replace a key, swap
    // two, or accept a one-edit dictionary neighbour. A raw token the bilingual
    // lexicon already knows as English must not be guessed at. Balanced - the
    // default - only preserves the Common tier, so without this gate the
    // Extended tier reached those rules and came back Vietnamese: bash -> ba,
    // hash -> ha, obj -> bo, arg -> ga, and at Experimental bathroom -> thom.
    // English protection set to Off is an explicit "always prefer Vietnamese",
    // so the gate stands down there.
    const bool raw_is_known_english =
        english_protection_level != EnglishProtectionLevel::Off &&
        (IsCommonEnglishWord(raw_lower) ||
         LookupGeneratedEnglishLexicon(raw_lower) !=
             EnglishLexiconTier::None);

    const bool is_valid_vietnamese = rules::IsValidVietnamese(lower_word, false);

    // 2. Extract tone and flat representation
    ToneMark active_tone = ToneMark::None;
    std::wstring flat_word = StripTone(lower_word, active_tone);

    // These two reorder or drop a keystroke, so the English gate covers them as
    // well even though they run from Normal upward. Reading the leading f or j
    // of an English word as a tone key typed early turned "fly" into "ly",
    // "fun" into "un" and "java" into "ava"; 187 f-/j- tokens were rewritten
    // that way. A Vietnamese typist who really does hit the tone key first
    // still gets "fa" -> "a" with a tone, since no Vietnamese word is in the
    // English lexicon.
    if (!raw_is_known_english) {
        // A later shape modifier may express a correction to an earlier one.
        // Keep the latest key, remove at most one older modifier, and accept
        // only one phonotactically valid replay candidate.
        if (auto stale_modifier_result =
                TryStaleModifierOverrideCorrection(word, raw_lower, method)) {
            return *stale_modifier_result;
        }

        // 2.5 Try Modifier/Tone Before Vowel Correction
        if (auto before_vowel_result =
                TryModifierBeforeVowelCorrection(word, raw_lower, method)) {
            return *before_vowel_result;
        }
    }

    // Input-method-specific rules. These must not leak across Telex and VNI.
    if (method == InputMethod::Telex || method == InputMethod::SimpleTelex) {
        if (auto telex_result = TryTelexToneKeyAdjacencyCorrection(word, raw_lower, level)) {
            return *telex_result;
        }
    } else if (method == InputMethod::VNI) {
        if (auto vni_result = TryVniKnownIeyueTnCorrection(word, raw_lower, level)) {
            return *vni_result;
        }
    }
    // 2.7 Try Advanced Keyboard Adjacent Tone/Modifier Correction
    if (!raw_is_known_english) {
        if (auto adj_result = TryAdjacentKeyToneCorrection(word, raw_lower, level, method)) {
            return *adj_result;
        }
    }

    // Baseline/common rules. These can run for every input method and enabled correction level.
    // 3. Try Vowel Substitution for uo -> uô / ươ (e.g. dduocj -> đuộc -> được)
    size_t uo_pos = flat_word.find(L"uo");
    if (uo_pos != std::wstring::npos) {
        // Specific overrides for common conflicts to prioritize uô/ươ correctly
        if (flat_word == L"muon" && active_tone == ToneMark::Sacute) {
            result.word = PreserveCasing(word, rules::ApplyTone(L"muôn", active_tone));
            result.kind = CorrectionKind::UoVowelSubstitution;
            result.score = 900;
            result.changed = true;
            result.high_confidence = true;
            return result;
        }
        if (flat_word == L"cuoc" && active_tone == ToneMark::Dot) {
            result.word = PreserveCasing(word, rules::ApplyTone(L"cuôc", active_tone));
            result.kind = CorrectionKind::UoVowelSubstitution;
            result.score = 900;
            result.changed = true;
            result.high_confidence = true;
            return result;
        }
        if (flat_word == L"luon" && active_tone == ToneMark::Grave) {
            result.word = PreserveCasing(word, rules::ApplyTone(L"luôn", active_tone));
            result.kind = CorrectionKind::UoVowelSubstitution;
            result.score = 900;
            result.changed = true;
            result.high_confidence = true;
            return result;
        }

        // Try replacing "uo" with "ươ"
        std::wstring flat_uo_replaced = ReplaceAll(flat_word, L"uo", L"ươ");
        std::wstring candidate = rules::ApplyTone(flat_uo_replaced, active_tone);
        if (IsInDictionary(candidate)) {
            result.word = PreserveCasing(word, candidate);
            result.kind = CorrectionKind::UoVowelSubstitution;
            result.score = 900;
            result.changed = true;
            result.high_confidence = true;
            return result;
        }

        // Try replacing "uo" with "uô"
        flat_uo_replaced = ReplaceAll(flat_word, L"uo", L"uô");
        candidate = rules::ApplyTone(flat_uo_replaced, active_tone);
        if (IsInDictionary(candidate)) {
            result.word = PreserveCasing(word, candidate);
            result.kind = CorrectionKind::UoVowelSubstitution;
            result.score = 900;
            result.changed = true;
            result.high_confidence = true;
            return result;
        }
    }

    // Horn-pair glide checks
    if (active_tone != ToneMark::None) {
        size_t horn_pair_pos = flat_word.find(L"\u01B0\u01A1");
        while (horn_pair_pos != std::wstring::npos) {
            std::wstring candidate = flat_word;
            candidate[horn_pair_pos] = L'u';
            candidate[horn_pair_pos + 1] = rules::MakeVowel(L'\u01A1', active_tone, false);
            if (IsInDictionary(candidate)) {
                result.word = PreserveCasing(word, candidate);
                result.kind = CorrectionKind::UoVowelSubstitution;
                result.score = 900;
                result.changed = true;
                result.high_confidence = true;
                return result;
            }
            horn_pair_pos = flat_word.find(L"\u01B0\u01A1", horn_pair_pos + 1);
        }
    }

    // 4. Try Specific Typo Corrections (e.g. thuyes -> thuyết, vies -> viết)
    if (IsAllowedMissingFinalTRawKeys(raw_lower) &&
        ShouldTryMissingFinalTCorrection(flat_word, active_tone)) {
        std::wstring corrected_flat(flat_word);
        if (EndsWith(corrected_flat, L"uye")) {
            corrected_flat.replace(corrected_flat.length() - 3, 3, L"uyê");
        } else if (EndsWith(corrected_flat, L"ie")) {
            corrected_flat.replace(corrected_flat.length() - 2, 2, L"iê");
        }

        if (!corrected_flat.empty() && rules::IsVowel(corrected_flat.back())) {
            std::wstring flat_appended = corrected_flat + L"t";
            std::wstring candidate = rules::ApplyTone(flat_appended, active_tone);
            if (IsInDictionary(candidate)) {
                result.word = PreserveCasing(word, candidate);
                result.kind = CorrectionKind::MissingFinalT;
                result.score = 900;
                result.changed = true;
                result.high_confidence = true;
                return result;
            }
        }
    }


    if (raw_lower == L"thuyes") {
        result.word = PreserveCasing(word, L"thuyết");
        result.kind = CorrectionKind::MissingFinalT;
        result.score = 900;
        result.changed = true;
        result.high_confidence = true;
        return result;
    }
    if (raw_lower == L"vies") {
        result.word = PreserveCasing(word, L"vi\u1EBFt");
        result.kind = CorrectionKind::MissingFinalT;
        result.score = 900;
        result.changed = true;
        result.high_confidence = true;
        return result;
    }

    // 5. Try Tone Shifting
    //
    // Deliberately NOT guarded on is_valid_vietnamese the way the rules below
    // it are: this is what puts the tone where the dictionary spells it, and
    // Neokey spells the oa/oe/uy rimes the older way on purpose - "hoà",
    // "khoá", "nhoè". See the hoaf/khoas tests.
    if (active_tone != ToneMark::None) {
        std::vector<size_t> vowel_indices;
        for (size_t i = 0; i < flat_word.length(); ++i) {
            if (rules::IsVowel(flat_word[i])) {
                vowel_indices.push_back(i);
            }
        }

        if (vowel_indices.size() > 1) {
            for (size_t idx : vowel_indices) {
                std::wstring candidate = flat_word;
                rules::VowelData vd;
                if (rules::GetVowelData(flat_word[idx], vd)) {
                    candidate[idx] = rules::MakeVowel(vd.raw, active_tone, vd.is_upper);
                    if (IsInDictionary(candidate)) {
                        result.word = PreserveCasing(word, candidate);
                        result.kind = CorrectionKind::ToneRelocation;
                        result.score = 900;
                        result.changed = true;
                        result.high_confidence = true;
                        return result;
                    }
                }
            }
        }
    }

    // 6. Try Missing Modifier (e.g. kiẻm -> kiểm, kiém -> kiếm, kiẹm -> kiệm)
    if (!is_valid_vietnamese) {
        for (size_t i = 0; i < flat_word.length(); ++i) {
            wchar_t original_char = flat_word[i];
            std::vector<wchar_t> candidates;
            if (original_char == L'e') {
                candidates.push_back(L'ê');
            } else if (original_char == L'o') {
                candidates.push_back(L'ô');
                candidates.push_back(L'ơ');
            } else if (original_char == L'a') {
                candidates.push_back(L'â');
                candidates.push_back(L'ă');
            } else if (original_char == L'u') {
                candidates.push_back(L'ư');
            } else if (original_char == L'd') {
                candidates.push_back(L'đ');
            }

            for (wchar_t modified_char : candidates) {
                std::wstring candidate_flat = flat_word;
                candidate_flat[i] = modified_char;
                std::wstring candidate = rules::ApplyTone(candidate_flat, active_tone);
                if (IsInDictionary(candidate)) {
                    result.word = PreserveCasing(word, candidate);
                    result.kind = CorrectionKind::MissingModifier;
                    result.score = 900;
                    result.changed = true;
                    result.high_confidence = true;
                    return result;
                }
            }
        }
    }

    // Advanced/Common rules. These run only for CorrectionLevel::Advanced and above.
    // 7. Advanced Correction Level Rules
    if (level >= CorrectionLevel::Advanced && !raw_is_known_english) {
        // A. General Missing Final Consonant
        if (!is_valid_vietnamese && !flat_word.empty() && rules::IsVowel(flat_word.back()) && active_tone != ToneMark::None) {
            const std::wstring common_final_consonants[] = { L"n", L"ng", L"t", L"c", L"p", L"m", L"nh", L"ch" };
            std::vector<std::wstring> matched_candidates;
            for (const auto& cons : common_final_consonants) {
                std::wstring candidate = rules::ApplyTone(flat_word + cons, active_tone);
                if (IsInDictionary(candidate)) {
                    if (std::find(matched_candidates.begin(), matched_candidates.end(), candidate) == matched_candidates.end()) {
                        matched_candidates.push_back(candidate);
                    }
                }
            }
            if (matched_candidates.size() == 1) {
                result.word = PreserveCasing(word, matched_candidates[0]);
                result.kind = CorrectionKind::MissingFinalT;
                result.score = 900;
                result.changed = true;
                result.high_confidence = true;
                return result;
            }
        }

        // B. General Adjacent Final Key Swap
        if (!is_valid_vietnamese && flat_word.length() >= 2) {
            std::wstring swapped_flat = flat_word;
            std::swap(swapped_flat[swapped_flat.length() - 2], swapped_flat[swapped_flat.length() - 1]);
            
            // Check if swapped flat is a dictionary word with the active tone
            std::wstring candidate = rules::ApplyTone(swapped_flat, active_tone);
            if (IsInDictionary(candidate)) {
                result.word = PreserveCasing(word, candidate);
                result.kind = CorrectionKind::AdjacentKeySwap;
                result.score = 900;
                result.changed = true;
                result.high_confidence = true;
                return result;
            }

        }

        // C. Missing Tone
        if (!is_valid_vietnamese && active_tone == ToneMark::None) {
            const ToneMark tones[] = { ToneMark::Sacute, ToneMark::Grave, ToneMark::Hook, ToneMark::Tilde, ToneMark::Dot };
            std::vector<std::wstring> matched_candidates;
            for (auto t : tones) {
                std::wstring candidate = rules::ApplyTone(flat_word, t);
                if (IsInDictionary(candidate)) {
                    if (std::find(matched_candidates.begin(), matched_candidates.end(), candidate) == matched_candidates.end()) {
                        matched_candidates.push_back(candidate);
                    }
                }
            }
            if (matched_candidates.size() == 1) {
                result.word = PreserveCasing(word, matched_candidates[0]);
                result.kind = CorrectionKind::MissingTone;
                result.score = 900;
                result.changed = true;
                result.high_confidence = true;
                return result;
            }
        }

        // D. General Adjacent Initial Key Swap (e.g. hcào -> chào, gnon -> ngon, hpong -> phong)
        if (!is_valid_vietnamese && flat_word.length() >= 2) {
            std::wstring swapped_flat = flat_word;
            std::swap(swapped_flat[0], swapped_flat[1]);
            std::wstring candidate = rules::ApplyTone(swapped_flat, active_tone);
            if (IsInDictionary(candidate) && rules::IsValidVietnamese(candidate, false)) {
                result.word = PreserveCasing(word, candidate);
                result.kind = CorrectionKind::AdjacentKeySwap;
                result.score = 900;
                result.changed = true;
                result.high_confidence = true;
                return result;
            }
        }
    }

    // 8. Experimental Level Rules
    if (level >= CorrectionLevel::Experimental && !raw_is_known_english) {
        // A. Damerau-Levenshtein Typo Distance Correction
        if (auto dl_result = TryDamerauLevenshteinCorrection(word, lower_word, level)) {
            return *dl_result;
        }

        // B. Two raw keys in the wrong order, anywhere in the word. Runs after
        // the edit-distance scan so that rule keeps first refusal.
        if (auto transposed_result = TryTransposedKeysCorrection(
                word, lower_word, raw_lower, level, method)) {
            return *transposed_result;
        }
    }

    // 9. Bounced key. A Normal-level rule, but placed last on purpose: it is
    // the only rule that DROPS a keystroke, and dropping one is a weaker
    // explanation than reordering or retyping one. Ordered before the blocks
    // above it, "onns" collapsed to "ón" instead of being read as the
    // transposition of "nons" -> "nón", and Advanced lost 65 first-pair
    // repairs to it.
    if (!raw_is_known_english) {
        if (auto bounced_result = TryBouncedKeyCorrection(
                word, lower_word, raw_lower, level, method)) {
            return *bounced_result;
        }
    }

    return result;
}

CorrectionResult CorrectWordEx(
    std::wstring_view word,
    std::wstring_view raw_keys,
    CorrectionLevel level,
    InputMethod method,
    bool enable_english_protection) {
    return CorrectWordEx(
        word,
        raw_keys,
        level,
        method,
        enable_english_protection
            ? EnglishProtectionLevel::Balanced
            : EnglishProtectionLevel::Off);
}

CorrectionResult CorrectWordEx(
    std::wstring_view word,
    std::wstring_view raw_keys,
    CorrectionLevel level) {
    return CorrectWordEx(word, raw_keys, level, InputMethod::Telex);
}

} // namespace vn_ime::core::speller
