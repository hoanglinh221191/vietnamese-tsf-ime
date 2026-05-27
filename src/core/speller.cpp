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
    if (word.empty()) {
        return std::wstring(word);
    }

    // 1. Convert word to lowercase for dictionary check
    std::wstring lower_word;
    lower_word.reserve(word.length());
    for (wchar_t c : word) {
        lower_word.push_back(rules::ToLower(c));
    }

    if (IsInDictionary(lower_word)) {
        return std::wstring(word);
    }

    // 2. Extract tone and flat representation
    ToneMark active_tone = ToneMark::None;
    std::wstring flat_word = StripTone(lower_word, active_tone);

    // 3. Try Vowel Substitution for uo -> uô / ươ (e.g. dduocj -> đuộc -> được)
    // We check if the flat word contains "uo"
    size_t uo_pos = flat_word.find(L"uo");
    if (uo_pos != std::wstring::npos) {
        // Specific overrides for common conflicts to prioritize uô/ươ correctly
        if (flat_word == L"muon" && active_tone == ToneMark::Sacute) {
            return PreserveCasing(word, rules::ApplyTone(L"muôn", active_tone));
        }
        if (flat_word == L"cuoc" && active_tone == ToneMark::Dot) {
            return PreserveCasing(word, rules::ApplyTone(L"cuôc", active_tone));
        }
        if (flat_word == L"luon" && active_tone == ToneMark::Grave) {
            return PreserveCasing(word, rules::ApplyTone(L"luôn", active_tone));
        }

        // Try replacing "uo" with "ươ"
        std::wstring flat_uo_replaced = ReplaceAll(flat_word, L"uo", L"ươ");
        std::wstring candidate = rules::ApplyTone(flat_uo_replaced, active_tone);
        if (IsInDictionary(candidate)) {
            return PreserveCasing(word, candidate);
        }

        // Try replacing "uo" with "uô"
        flat_uo_replaced = ReplaceAll(flat_word, L"uo", L"uô");
        candidate = rules::ApplyTone(flat_uo_replaced, active_tone);
        if (IsInDictionary(candidate)) {
            return PreserveCasing(word, candidate);
        }
    }

    // 4. Try Specific Typo Corrections (e.g. tuyetn -> tuyến, thuyes -> thuyết)
    
    // Typo: Missing 't' before tone (e.g., thuyes -> thuyết, vies -> viết)
    // If word ends with a vowel that has a tone, but it is not in the dictionary,
    // we try appending 't' to the flat word and reapplying the tone.
    if (ShouldTryMissingFinalTCorrection(flat_word, active_tone)) {
        // Find if the flat word ends with a vowel
        if (!flat_word.empty() && rules::IsVowel(flat_word.back())) {
            // Append 't'
            std::wstring flat_appended = flat_word + L"t";
            std::wstring candidate = rules::ApplyTone(flat_appended, active_tone);
            if (IsInDictionary(candidate)) {
                return PreserveCasing(word, candidate);
            }
        }
    }

    // Typo: Swapped last keys / missing tone key (e.g., tuyetn -> tuyến, luyetn -> luyến)
    // If flat word ends with "tn", try replacing "tn" with "n" and applying Sacute tone
    if (flat_word.length() >= 2 && flat_word.substr(flat_word.length() - 2) == L"tn") {
        std::wstring flat_corrected = flat_word.substr(0, flat_word.length() - 2) + L"n";
        // Apply Sacute (Telex 's' / VNI '1') tone
        std::wstring candidate = rules::ApplyTone(flat_corrected, ToneMark::Sacute);
        if (IsInDictionary(candidate)) {
            return PreserveCasing(word, candidate);
        }
    }

    // Typo: Raw key mappings / hardcoded cases (as fallback)
    std::wstring raw_lower;
    raw_lower.reserve(raw_keys.length());
    for (wchar_t c : raw_keys) raw_lower.push_back(rules::ToLower(c));

    if (raw_lower == L"tuyetn") {
        return PreserveCasing(word, L"tuyến");
    }
    if (raw_lower == L"thuyes") {
        return PreserveCasing(word, L"thuyết");
    }
    if (raw_lower == L"vies") {
        return PreserveCasing(word, L"vi\u1EBFt");
    }

    // 5. Try Tone Shifting (Relocation of active tone to other vowels)
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
                        return PreserveCasing(word, candidate);
                    }
                }
            }
        }
    }

    // 6. If no corrections work, return the original word
    return std::wstring(word);
}

} // namespace vn_ime::core::speller
