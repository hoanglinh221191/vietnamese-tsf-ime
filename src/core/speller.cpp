#include "speller.hpp"
#include "speller_data.hpp"
#include "rules.hpp"
#include "engine.hpp"
#include <algorithm>
#include <optional>
#include <vector>
#include <cwctype>

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

std::vector<wchar_t> GetNearbyDauKeys(wchar_t key, InputMethod method) {
    std::vector<wchar_t> res;
    if (method == InputMethod::Telex || method == InputMethod::SimpleTelex) {
        switch (key) {
            case L'a': res = {L's', L'z', L'w'}; break;
            case L'd': res = {L's', L'f', L'r'}; break;
            case L'z': res = {L's', L'x', L'z'}; break;
            case L'w': res = {L's', L'w'}; break;
            case L'q': res = {L'w'}; break;
            case L'e': res = {L'w', L'r'}; break;
            case L'g': res = {L'f', L'j'}; break;
            case L'r': res = {L'f', L'r'}; break;
            case L'v': res = {L'f'}; break;
            case L'c': res = {L'f', L'x'}; break;
            case L't': res = {L'r'}; break;
            case L's': res = {L'x', L'z', L'w', L's'}; break;
            case L'h': res = {L'j'}; break;
            case L'k': res = {L'j'}; break;
            case L'u': res = {L'j'}; break;
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
            case L'i': res = {L'8'}; break;
            default: break;
        }
    }
    return res;
}

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

    std::vector<std::wstring> matched_words;
    const bool is_vni = (method == InputMethod::VNI);

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
            
            Engine temp_engine(method);
            temp_engine.SetAutoCorrect(false);
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

std::wstring StripAllAccents(std::wstring_view str) {
    std::wstring result;
    result.reserve(str.length());
    for (wchar_t c : str) {
        rules::VowelData vd;
        if (rules::GetVowelData(c, vd)) {
            result.push_back(vd.raw);
        } else if (c == L'đ' || c == L'Đ') {
            result.push_back(L'd');
        } else {
            result.push_back(rules::ToLower(c));
        }
    }
    return result;
}

size_t CalculateDamerauLevenshtein(std::wstring_view s1, std::wstring_view s2) {
    size_t len1 = s1.length();
    size_t len2 = s2.length();
    if (len1 > 14 || len2 > 14) return 999;

    int d[16][16];
    for (int i = 0; i <= static_cast<int>(len1); ++i) d[i][0] = i;
    for (int j = 0; j <= static_cast<int>(len2); ++j) d[0][j] = j;

    for (int i = 1; i <= static_cast<int>(len1); ++i) {
        for (int j = 1; j <= static_cast<int>(len2); ++j) {
            int cost = (s1[i - 1] == s2[j - 1]) ? 0 : 1;
            d[i][j] = (std::min)({
                d[i - 1][j] + 1,
                d[i][j - 1] + 1,
                d[i - 1][j - 1] + cost
            });
            if (i > 1 && j > 1 && s1[i - 1] == s2[j - 2] && s1[i - 2] == s2[j - 1]) {
                d[i][j] = (std::min)(d[i][j], d[i - 2][j - 2] + cost);
            }
        }
    }
    return static_cast<size_t>(d[len1][len2]);
}

std::optional<CorrectionResult> TryDamerauLevenshteinCorrection(
    std::wstring_view word,
    const std::wstring& lower_word,
    CorrectionLevel level) {
    if (level < CorrectionLevel::Experimental) return std::nullopt;
    std::wstring flat_lower = StripAllAccents(lower_word);
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

    size_t min_dist = 999;
    std::wstring best_match;
    size_t match_count = 0;

    for (size_t i = 0; i < DICTIONARY_SIZE; ++i) {
        std::wstring_view dict_word(DICTIONARY[i]);
        std::wstring flat_dict = StripAllAccents(dict_word);

        if (std::abs(static_cast<int>(flat_dict.length()) - static_cast<int>(flat_lower.length())) > 2) {
            continue;
        }

        size_t dist = CalculateDamerauLevenshtein(flat_lower, flat_dict);
        size_t max_allowed_dist = (flat_lower.length() <= 5) ? 1 : 2;
        if (dist > max_allowed_dist) continue;

        if (dist < min_dist) {
            min_dist = dist;
            best_match = std::wstring(dict_word);
            match_count = 1;
        } else if (dist == min_dist) {
            match_count++;
        }
    }

    if (match_count == 1 && !best_match.empty()) {
        CorrectionResult result;
        result.word = PreserveCasing(word, best_match);
        result.kind = CorrectionKind::AdjacentKeySwap;
        result.score = 850;
        result.changed = true;
        result.high_confidence = true;
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

bool IsCommonEnglishWord(std::wstring_view word) {
    static constexpr std::wstring_view COMMON_ENGLISH_WORDS[] = {
        L"about", L"accept", L"access", L"action", L"active", L"add", L"admin", L"after", L"all", L"allow",
        L"am", L"an", L"and", L"api", L"app", L"apps", L"array", L"as", L"at", L"auto",
        L"back", L"bad", L"bag", L"bar", L"base", L"bat", L"be", L"best", L"beta", L"big",
        L"bit", L"blog", L"body", L"bool", L"bot", L"box", L"boy", L"bug", L"build", L"bus",
        L"but", L"buy", L"by", L"byte", L"call", L"can", L"cancel", L"car", L"card", L"cat",
        L"cell", L"chat", L"check", L"city", L"class", L"clean", L"clear", L"click", L"close", L"cmd",
        L"code", L"const", L"copy", L"core", L"cpu", L"css", L"custom", L"cut", L"data", L"date",
        L"db", L"debug", L"def", L"del", L"delete", L"demo", L"desk", L"dev", L"dir", L"disk",
        L"dl", L"dll", L"do", L"doc", L"docs", L"dog", L"done", L"dos", L"dot", L"download",
        L"draw", L"drive", L"drop", L"each", L"edit", L"else", L"em", L"email", L"end", L"env",
        L"err", L"error", L"etc", L"event", L"ex", L"exec", L"exe", L"exit", L"export", L"fact",
        L"fail", L"false", L"fan", L"fast", L"file", L"files", L"find", L"fix", L"flag", L"flow",
        L"font", L"foo", L"for", L"form", L"from", L"full", L"func", L"game", L"get", L"git",
        L"go", L"good", L"gpu", L"graph", L"group", L"had", L"has", L"have", L"he", L"head",
        L"help", L"home", L"host", L"hot", L"html", L"icon", L"id", L"if", L"image", L"img",
        L"import", L"in", L"index", L"info", L"init", L"input", L"int", L"into", L"ip", L"is",
        L"it", L"item", L"items", L"job", L"join", L"js", L"json", L"key", L"keys", L"kill",
        L"kind", L"lang", L"last", L"left", L"len", L"let", L"level", L"lib", L"like", L"line",
        L"link", L"list", L"load", L"lock", L"log", L"login", L"logs", L"long", L"loop", L"mac",
        L"main", L"make", L"map", L"maps", L"math", L"max", L"me", L"media", L"menu", L"min",
        L"mode", L"model", L"msg", L"my", L"name", L"net", L"new", L"next", L"no", L"node",
        L"none", L"not", L"null", L"num", L"of", L"off", L"ok", L"old", L"on", L"one",
        L"open", L"opt", L"option", L"or", L"order", L"org", L"os", L"out", L"output", L"pack",
        L"page", L"param", L"parse", L"pass", L"path", L"pdf", L"pen", L"ping", L"pipe", L"pkg",
        L"plan", L"play", L"plugin", L"png", L"point", L"port", L"post", L"print", L"pub", L"push",
        L"put", L"query", L"quit", L"ram", L"rank", L"raw", L"rd", L"read", L"real", L"ref",
        L"reg", L"remove", L"req", L"reset", L"res", L"result", L"root", L"row", L"run", L"save",
        L"scan", L"scope", L"search", L"select", L"set", L"shift", L"show", L"sit", L"site", L"size",
        L"skip", L"so", L"socket", L"sort", L"source", L"sql", L"src", L"st", L"stat", L"state",
        L"std", L"step", L"stop", L"str", L"string", L"struct", L"sub", L"sun", L"sync", L"sys",
        L"tab", L"table", L"tag", L"task", L"tax", L"team", L"temp", L"test", L"text", L"th",
        L"the", L"theme", L"this", L"time", L"title", L"to", L"tool", L"top", L"true", L"try",
        L"ts", L"txt", L"type", L"ui", L"unit", L"up", L"update", L"upload", L"url", L"us",
        L"usage", L"use", L"user", L"ux", L"val", L"value", L"var", L"version", L"view", L"views",
        L"vs", L"we", L"web", L"win", L"window", L"word", L"work", L"write", L"xml", L"zip"
    };
    return std::binary_search(std::begin(COMMON_ENGLISH_WORDS), std::end(COMMON_ENGLISH_WORDS), word);
}

} // namespace

std::wstring CorrectWord(std::wstring_view word, std::wstring_view raw_keys) {
    return CorrectWordEx(word, raw_keys, CorrectionLevel::Normal, InputMethod::Telex).word;
}

CorrectionResult CorrectWordEx(
    std::wstring_view word,
    std::wstring_view raw_keys,
    CorrectionLevel level,
    InputMethod method,
    bool enable_english_protection) {
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

    // Exemption for common English words and tech terms (e.g. "us", "is", "in", "app", "api", "git", "struct")
    if (enable_english_protection && (IsCommonEnglishWord(lower_word) || IsCommonEnglishWord(raw_lower))) {
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

    const bool is_valid_vietnamese = rules::IsValidVietnamese(lower_word, false);

    // 2. Extract tone and flat representation
    ToneMark active_tone = ToneMark::None;
    std::wstring flat_word = StripTone(lower_word, active_tone);

    // A later shape modifier may express a correction to an earlier one.
    // Keep the latest key, remove at most one older modifier, and accept only
    // one phonotactically valid replay candidate.
    if (auto stale_modifier_result =
            TryStaleModifierOverrideCorrection(word, raw_lower, method)) {
        return *stale_modifier_result;
    }

    // 2.5 Try Modifier/Tone Before Vowel Correction
    if (auto before_vowel_result = TryModifierBeforeVowelCorrection(word, raw_lower, method)) {
        return *before_vowel_result;
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
    if (auto adj_result = TryAdjacentKeyToneCorrection(word, raw_lower, level, method)) {
        return *adj_result;
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
    if (level >= CorrectionLevel::Experimental) {
        // A. Damerau-Levenshtein Typo Distance Correction
        if (auto dl_result = TryDamerauLevenshteinCorrection(word, lower_word, level)) {
            return *dl_result;
        }
    }

    return result;
}

CorrectionResult CorrectWordEx(
    std::wstring_view word,
    std::wstring_view raw_keys,
    CorrectionLevel level) {
    return CorrectWordEx(word, raw_keys, level, InputMethod::Telex);
}

} // namespace vn_ime::core::speller
