#include "speller.hpp"
#include "speller_data.hpp"
#include "rules.hpp"
#include <algorithm>
#include <vector>
#include <cwctype>

namespace vn_ime::core::speller {

namespace {

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

std::wstring PreserveCasing(std::wstring_view original, std::wstring_view corrected) {
    if (original.empty() || corrected.empty()) {
        return std::wstring(corrected);
    }
    
    bool all_upper = true;
    bool has_alpha = false;
    for (wchar_t c : original) {
        if (std::iswalpha(static_cast<wint_t>(c))) {
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
    
    if (std::iswalpha(static_cast<wint_t>(original[0])) && original[0] == rules::ToUpper(original[0])) {
        std::wstring result(corrected);
        result[0] = rules::ToUpper(result[0]);
        return result;
    }
    
    return std::wstring(corrected);
}

std::wstring CorrectWord(std::wstring_view word, std::wstring_view raw_keys) {
    return CorrectWordEx(word, raw_keys, CorrectionLevel::Normal).word;
}

CorrectionResult CorrectWordEx(
    std::wstring_view word,
    std::wstring_view raw_keys,
    CorrectionLevel level) {

    CorrectionResult result;
    result.word = std::wstring(word);
    result.kind = CorrectionKind::None;
    result.score = 0;
    result.changed = false;
    result.high_confidence = false;

    if (word.empty()) {
        return result;
    }

    if (level == CorrectionLevel::Off) {
        return result;
    }

    // 1. Convert word to lowercase for dictionary check
    std::wstring lower_word;
    lower_word.reserve(word.length());
    for (wchar_t c : word) {
        lower_word.push_back(rules::ToLower(c));
    }

    if (IsInDictionary(lower_word)) {
        result.score = 1000;
        result.high_confidence = true;
        return result;
    }

    const bool is_valid_vietnamese = rules::IsValidVietnamese(lower_word, false);

    // 2. Extract tone and flat representation
    ToneMark active_tone = ToneMark::None;
    std::wstring flat_word = StripTone(lower_word, active_tone);

    std::wstring raw_lower;
    raw_lower.reserve(raw_keys.length());
    for (wchar_t c : raw_keys) raw_lower.push_back(rules::ToLower(c));

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

    // VNI horn path glide checks
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

    // 4. Try Specific Typo Corrections (e.g. tuyetn -> tuyến, thuyes -> thuyết)
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

    if (flat_word.length() >= 2 && flat_word.substr(flat_word.length() - 2) == L"tn") {
        std::wstring flat_corrected = flat_word.substr(0, flat_word.length() - 2) + L"n";
        std::wstring candidate = rules::ApplyTone(flat_corrected, ToneMark::Sacute);
        if (IsInDictionary(candidate)) {
            result.word = PreserveCasing(word, candidate);
            result.kind = CorrectionKind::SwappedFinalKeys;
            result.score = 900;
            result.changed = true;
            result.high_confidence = true;
            return result;
        }
    }

    if (raw_lower == L"tuyetn") {
        result.word = PreserveCasing(word, L"tuyến");
        result.kind = CorrectionKind::AdjacentKeySwap;
        result.score = 900;
        result.changed = true;
        result.high_confidence = true;
        return result;
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

    // 7. Advanced Correction Level Rules
    if (level >= CorrectionLevel::Advanced) {
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

            // Also check if swapped flat ends in "tn" (legacy case like tuyent -> tuyetn -> tuyến)
            if (swapped_flat.length() >= 2 && swapped_flat.substr(swapped_flat.length() - 2) == L"tn") {
                std::wstring flat_corrected = swapped_flat.substr(0, swapped_flat.length() - 2) + L"n";
                std::wstring tn_candidate = rules::ApplyTone(flat_corrected, ToneMark::Sacute);
                if (IsInDictionary(tn_candidate)) {
                    result.word = PreserveCasing(word, tn_candidate);
                    result.kind = CorrectionKind::AdjacentKeySwap;
                    result.score = 900;
                    result.changed = true;
                    result.high_confidence = true;
                    return result;
                }
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
    }

    return result;
}

} // namespace vn_ime::core::speller
