#include "engine.hpp"

#include "free_typing.hpp"
#include "free_typing_repair.hpp"
#include "rules.hpp"
#include "speller.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <limits>
#include <windows.h>
#include <cwctype>
#include <vector>

namespace vn_ime::core {

namespace {

struct Letter {
    wchar_t current;
    wchar_t original;
    bool modified_by_w;
    size_t raw_index;
    bool is_escaped = false;
    // The key that gave this letter its Telex circumflex or stroke - the
    // second a of "aa", the second d of "dd" - so that the same key pressed
    // straight after it can take the mark back. See TryProcessTelexKeys.
    size_t shaped_by_raw_index = static_cast<size_t>(-1);
};

struct ProcessedResult {
    std::wstring word;
    bool has_escaped = false;
};

constexpr bool IsAsciiLower(wchar_t ch) noexcept {
    return ch >= L'a' && ch <= L'z';
}

constexpr bool IsAsciiUpper(wchar_t ch) noexcept {
    return ch >= L'A' && ch <= L'Z';
}

constexpr bool IsAsciiAlpha(wchar_t ch) noexcept {
    return IsAsciiLower(ch) || IsAsciiUpper(ch);
}

constexpr bool IsAsciiDigit(wchar_t ch) noexcept {
    return ch >= L'0' && ch <= L'9';
}

constexpr bool IsAsciiAlphaNumeric(wchar_t ch) noexcept {
    return IsAsciiAlpha(ch) || IsAsciiDigit(ch);
}

constexpr wchar_t ToLowerAscii(wchar_t ch) noexcept {
    return IsAsciiUpper(ch) ? ch - L'A' + L'a' : ch;
}

bool StartsWithAsciiCaseInsensitive(
    std::wstring_view text,
    std::wstring_view prefix) noexcept {
    if (text.length() < prefix.length()) {
        return false;
    }
    for (size_t i = 0; i < prefix.length(); ++i) {
        if (ToLowerAscii(text[i]) != prefix[i]) {
            return false;
        }
    }
    return true;
}

bool EqualsAsciiCaseInsensitive(
    std::wstring_view text,
    std::wstring_view expected) noexcept {
    return text.length() == expected.length() &&
        StartsWithAsciiCaseInsensitive(text, expected);
}

bool IsEmailContextToken(std::wstring_view token) noexcept {
    const size_t at = token.find(L'@');
    if (at == std::wstring_view::npos || at == 0 ||
        token.find(L'@', at + 1) != std::wstring_view::npos) {
        return false;
    }

    for (size_t i = 0; i < at; ++i) {
        const wchar_t ch = token[i];
        if (!IsAsciiAlphaNumeric(ch) && ch != L'.' && ch != L'_' &&
            ch != L'+' && ch != L'-') {
            return false;
        }
    }
    for (size_t i = at + 1; i < token.length(); ++i) {
        const wchar_t ch = token[i];
        if (!IsAsciiAlphaNumeric(ch) && ch != L'.' && ch != L'-') {
            return false;
        }
    }
    return true;
}

bool IsUrlContextToken(std::wstring_view token) noexcept {
    const bool has_url_prefix =
        StartsWithAsciiCaseInsensitive(token, L"http:") ||
        StartsWithAsciiCaseInsensitive(token, L"https:") ||
        StartsWithAsciiCaseInsensitive(token, L"www.");
    if (!has_url_prefix) {
        return false;
    }

    for (const wchar_t ch : token) {
        if (IsAsciiAlphaNumeric(ch)) {
            continue;
        }
        switch (ch) {
            case L':': case L'/': case L'.': case L'_': case L'-':
            case L'?': case L'&': case L'=': case L'%': case L'#':
            case L'~': case L'+': case L'@':
                break;
            default:
                return false;
        }
    }
    return true;
}

bool IsUnderscoreIdentifier(std::wstring_view token) noexcept {
    bool has_underscore = false;
    bool has_alpha = false;
    for (const wchar_t ch : token) {
        if (ch == L'_') {
            has_underscore = true;
        } else if (IsAsciiAlpha(ch)) {
            has_alpha = true;
        } else if (!IsAsciiDigit(ch)) {
            return false;
        }
    }
    return has_underscore && has_alpha;
}

bool HasInternalLowerToUpperTransition(std::wstring_view token) noexcept {
    if (token.length() < 2) {
        return false;
    }
    for (const wchar_t ch : token) {
        if (!IsAsciiAlphaNumeric(ch)) {
            return false;
        }
    }
    for (size_t i = 1; i < token.length(); ++i) {
        if (IsAsciiLower(token[i - 1]) && IsAsciiUpper(token[i])) {
            return true;
        }
    }
    return false;
}

bool IsKnownCodeFamilyToken(std::wstring_view token) noexcept {
    const size_t digit_start = token.find_first_of(L"0123456789");
    if (digit_start == std::wstring_view::npos || digit_start == 0) {
        return false;
    }
    for (size_t i = 0; i < digit_start; ++i) {
        if (!IsAsciiAlpha(token[i])) {
            return false;
        }
    }
    for (size_t i = digit_start; i < token.length(); ++i) {
        if (!IsAsciiAlphaNumeric(token[i])) {
            return false;
        }
    }

    static constexpr std::wstring_view kKnownCodeFamilies[] = {
        L"arm", L"base", L"ipv", L"sha", L"utf", L"win",
        L"windows", L"x",
    };
    const std::wstring_view family = token.substr(0, digit_start);
    for (const std::wstring_view known : kKnownCodeFamilies) {
        if (EqualsAsciiCaseInsensitive(family, known)) {
            return true;
        }
    }
    return false;
}

bool HasDigits(const std::vector<Letter>& base_word) {
    for (const auto& l : base_word) {
        if (l.current >= L'0' && l.current <= L'9') {
            return true;
        }
    }
    return false;
}

static bool HasUySequence(const std::vector<Letter>& base_word) {
    if (base_word.size() < 2) return false;
    for (size_t idx = 0; idx + 1 < base_word.size(); ++idx) {
        wchar_t first = rules::ToLower(base_word[idx].current);
        wchar_t second = rules::ToLower(base_word[idx + 1].current);
        if (first == L'u' && second == L'y') {
            return true;
        }
    }
    return false;
}

// huơ and thuở horn the o alone: h or th, then u and o with nothing after
// them yet. The horn key - Telex w, VNI 7 - puts it there for both methods;
// anything typed after the o (hương, thương, hươu) horns the u as well again,
// in SynchronizeHornModification.
static bool IsOpenHuoOrThuo(const std::vector<Letter>& base_word, size_t u_idx, size_t o_idx) {
    if (u_idx + 1 != o_idx || o_idx + 1 != base_word.size()) {
        return false;
    }
    if (base_word.size() == 3) {
        return rules::ToLower(base_word[0].current) == L'h';
    }
    if (base_word.size() == 4) {
        return rules::ToLower(base_word[0].current) == L't' && rules::ToLower(base_word[1].current) == L'h';
    }
    return false;
}

bool TryProcessTelexKeys(
    wchar_t ch,
    wchar_t lch,
    size_t i,
    const std::wstring& raw,
    std::vector<Letter>& base_word,
    wchar_t& last_tone_key,
    bool& prev_w_consumed,
    CorrectionLevel correction_level,
    InputMethod method) {

    bool processed = false;

    // UniKey's Telex brackets: [ is ơ and ] is ư, { and } their capitals -
    // "t[" is tơ, "nh]ngx" những. Simple Telex leaves them alone, as it does
    // a lone w. The text service only hands a bracket over when it can be one
    // of these, see Engine::AcceptsTelexBracket; everywhere else it is typed.
    if (method == InputMethod::Telex &&
        (ch == L'[' || ch == L']' || ch == L'{' || ch == L'}')) {
        const bool horn_o = ch == L'[' || ch == L'{';
        const bool upper = ch == L'{' || ch == L'}';
        const wchar_t vowel = horn_o ? (upper ? L'Ơ' : L'ơ')
                                     : (upper ? L'Ư' : L'ư');
        base_word.push_back({vowel, ch, false, i, false});
        last_tone_key = L'\0';
        prev_w_consumed = false;
        return true;
    }

    // Telex double key/free-style modification for a, e, o, d
    if (lch == L'a' || lch == L'e' || lch == L'o' || lch == L'd') {
        // Pressed a third time, the key takes its mark back and types itself:
        // "ooo" is "oo", as "ww" is "w" and "ss" is "s". Without this the
        // third o went in beside the circumflexed one, so xoong, boong and the
        // oo of rơ-moóc could not be typed at all, and neither could the
        // English that a Vietnamese reading now wins - "tooo" for too, "teeen"
        // for teen. Only straight after the key that made the mark: a later
        // one is free-style placement reaching back, not a change of mind.
        if (i > 0 && rules::ToLower(raw[i - 1]) == lch) {
            for (auto it = base_word.rbegin(); it != base_word.rend(); ++it) {
                if (it->shaped_by_raw_index != i - 1) {
                    continue;
                }
                const bool is_upper = it->current != rules::ToLower(it->current);
                it->current = is_upper ? rules::ToUpper(lch) : lch;
                it->modified_by_w = false;
                it->shaped_by_raw_index = static_cast<size_t>(-1);
                it->is_escaped = true;
                base_word.push_back({ch, ch, false, i, true});
                last_tone_key = L'\0';
                return true;
            }
        }

        bool modified = false;
        // Free-style placement lets the modifier arrive after the rest of the
        // syllable - "tana" for "tân" - by searching back through the word for
        // something to change. That search cannot tell a late modifier from the
        // next syllable's own letter, so in a run with no spaces the second "a"
        // of "thanhtam" reaches back and rewrites the first, giving "thânhtm".
        //
        // Reaching back across another vowel is sometimes right - "tuoio" is
        // tuôi - and sometimes a new letter: the second o of "ngoeo" is the
        // last vowel of ngoèo, not a circumflex for the first, which made
        // ngoằn ngoèo come out "ngồ". Across a vowel, the mark is placed only
        // if the vowels it makes are a group Vietnamese has - ôe is not. Only
        // the vowels are asked about: the whole-syllable rules turn down some
        // dictionary words, pây among them, that reaching back always typed.
        const auto reach_is_plausible = [&](size_t target, wchar_t shaped) {
            bool crosses_vowel = false;
            for (size_t k = target + 1; k < base_word.size(); ++k) {
                if (rules::IsVowel(base_word[k].current)) {
                    crosses_vowel = true;
                    break;
                }
            }
            if (!crosses_vowel) {
                return true;
            }
            std::wstring candidate;
            candidate.reserve(base_word.size());
            for (size_t k = 0; k < base_word.size(); ++k) {
                candidate.push_back(k == target ? shaped : base_word[k].current);
            }
            return rules::HasPlausibleVowelCluster(candidate);
        };
        for (size_t it_idx = base_word.size(); it_idx > 0; --it_idx) {
            size_t idx = it_idx - 1;
            auto& letter = base_word[idx];
            // A key given back by doubling is where the reaching stops. Behind
            // it the user has asked for the letters as typed: "therre" is
            // there - the doubled r hands back the r, and the last e must not
            // reach across it to make the first one ê, as it did ("thêr"),
            // leaving the English word no key to be typed with. Likewise
            // "xooong" is xoong, not a fourth o reaching back for a
            // circumflex.
            if (letter.is_escaped) {
                break;
            }
            wchar_t cur = letter.current;
            wchar_t cur_low = rules::ToLower(cur);
            bool is_upper = (cur != cur_low);
            if (lch != L'd' &&
                ((lch == L'e' && cur_low == L'e') ||
                 (lch == L'a' && (cur_low == L'a' || cur_low == L'ă')) ||
                 (lch == L'o' && (cur_low == L'o' || cur_low == L'ơ'))) &&
                !reach_is_plausible(
                    idx, lch == L'e' ? L'ê' : (lch == L'a' ? L'â' : L'ô'))) {
                break;
            }

            if (lch == L'e' && cur_low == L'e') {
                letter.current = is_upper ? L'Ê' : L'ê';
                letter.shaped_by_raw_index = i;
                modified = true;
                break;
            }
            else if (lch == L'a' && (cur_low == L'a' || cur_low == L'ă')) {
                letter.current = is_upper ? L'Â' : L'â';
                letter.modified_by_w = false;
                letter.shaped_by_raw_index = i;
                modified = true;
                break;
            }
            else if (lch == L'o' && (cur_low == L'o' || cur_low == L'ơ')) {
                letter.current = is_upper ? L'Ô' : L'ô';
                letter.modified_by_w = false;
                letter.shaped_by_raw_index = i;
                modified = true;
                break;
            }
            else if (lch == L'd' && cur_low == L'd') {
                letter.current = is_upper ? L'Đ' : L'đ';
                letter.shaped_by_raw_index = i;
                modified = true;
                break;
            }
        }
        
        if (modified) {
            processed = true;
            last_tone_key = L'\0';
        }
    }
    
    if (!processed && lch == L'w') {
        if (i > 0 && rules::ToLower(raw[i-1]) == L'w' && !prev_w_consumed) {
            // Revert w modification
            bool found_w_mod = false;
            for (auto it = base_word.rbegin(); it != base_word.rend(); ++it) {
                if (it->modified_by_w) {
                    it->current = it->original;
                    it->modified_by_w = false;
                    found_w_mod = true;
                }
            }
            if (found_w_mod) {
                base_word.push_back({ch, ch, false, i, true});
            } else if (!base_word.empty() && rules::ToLower(base_word.back().current) == L'ư') {
                // Standalone ư -> w, and this w is a w as well: "ww" is ww.
                // It used to give the one w back, the way "ss" gives one s,
                // which left no way to type exactly ww - two presses made w
                // and a third made www - and made www depend on the word
                // being handed back as keys for not being Vietnamese. A w that
                // marked a vowel is still taken back singly: "aww" is aw,
                // "owwn" own.
                auto& u_horn = base_word.back();
                u_horn.current = (u_horn.current == L'Ư') ? L'W' : L'w';
                u_horn.original = L'w';
                u_horn.is_escaped = true;
                base_word.push_back({ch, ch, false, i, true});
            } else {
                // Nothing to take back - a Simple Telex w that stayed a w -
                // so this one is a letter too: "ww" is ww there, not w.
                base_word.push_back({ch, ch, false, i, false});
            }
            prev_w_consumed = true;
            processed = true;
            last_tone_key = L'\0';
        } else {
            // Try applying w modification
            if (correction_level != CorrectionLevel::Off && HasUySequence(base_word)) {
                processed = true;
                prev_w_consumed = false;
                last_tone_key = L'\0';
            } else {
                bool has_u = false, has_o = false, has_a = false;
                size_t u_idx = 0, o_idx = 0, a_idx = 0;
                for (size_t idx = 0; idx < base_word.size(); ++idx) {
                    wchar_t base_vowel = rules::ToLower(base_word[idx].current);
                    const bool is_qu_glide = idx == 1 &&
                        rules::ToLower(base_word[0].current) == L'q';
                    if ((base_vowel == L'u' || base_vowel == L'ư') && !is_qu_glide && !has_u) {
                        has_u = true;
                        u_idx = idx;
                    }
                    else if ((base_vowel == L'o' || base_vowel == L'ơ') && !has_o) {
                        has_o = true;
                        o_idx = idx;
                    }
                    else if (base_vowel == L'a' || base_vowel == L'ă') { has_a = true; a_idx = idx; }
                }
                
                if (has_u && has_o) {
                    if (IsOpenHuoOrThuo(base_word, u_idx, o_idx)) {
                        base_word[o_idx].current = (base_word[o_idx].current == L'O' || base_word[o_idx].current == L'Ơ') ? L'Ơ' : L'ơ';
                        base_word[o_idx].modified_by_w = true;
                        processed = true;
                    } else {
                        base_word[u_idx].current = (base_word[u_idx].current == L'U' || base_word[u_idx].current == L'Ư') ? L'Ư' : L'ư';
                        base_word[u_idx].modified_by_w = true;
                        base_word[o_idx].current = (base_word[o_idx].current == L'O' || base_word[o_idx].current == L'Ơ') ? L'Ơ' : L'ơ';
                        base_word[o_idx].modified_by_w = true;
                        processed = true;
                    }
                } else if (has_o && has_a && a_idx == o_idx + 1) {
                    // In the oa nucleus, w belongs to a (oă), not o (ơa).
                    base_word[a_idx].current = (base_word[a_idx].current == L'A' || base_word[a_idx].current == L'Ă') ? L'Ă' : L'ă';
                    base_word[a_idx].modified_by_w = true;
                    processed = true;
                } else if (has_u) {
                    base_word[u_idx].current = (base_word[u_idx].current == L'U' || base_word[u_idx].current == L'Ư') ? L'Ư' : L'ư';
                    base_word[u_idx].modified_by_w = true;
                    processed = true;
                } else if (has_o) {
                    base_word[o_idx].current = (base_word[o_idx].current == L'O' || base_word[o_idx].current == L'Ơ') ? L'Ơ' : L'ơ';
                    base_word[o_idx].modified_by_w = true;
                    processed = true;
                } else if (has_a) {
                    base_word[a_idx].current = (base_word[a_idx].current == L'A' || base_word[a_idx].current == L'Ă') ? L'Ă' : L'ă';
                    base_word[a_idx].modified_by_w = true;
                    processed = true;
                }
                
                if (!processed) {
                    const bool after_literal_w = !base_word.empty() &&
                        base_word.back().is_escaped &&
                        rules::ToLower(base_word.back().current) == L'w';
                    if (method == InputMethod::SimpleTelex || after_literal_w) {
                        // Simple Telex leaves a w with no vowel to mark alone,
                        // and once a w has been given back as a letter the
                        // ones after it are letters too: "www" is www.
                        base_word.push_back({ch, ch, false, i, after_literal_w});
                    } else {
                        // In Telex a w on its own is ư, after an onset - hw is
                        // hư - and at the start of a word too: wf is ừ, wa is
                        // ưa. It used to stay a w at the start, which made ừ,
                        // ưa, ước and ướt untypable that way. English that
                        // starts with w is not Vietnamese once the rest is
                        // typed, and the display hands back the keys for it.
                        base_word.push_back({(ch == L'W') ? L'Ư' : L'ư', L'w', false, i, false});
                    }
                    processed = true;
                }
                prev_w_consumed = false;
                last_tone_key = L'\0';
            }
        }
    }
    
    return processed;
}

bool TryProcessVNIKeys(
    wchar_t ch,
    wchar_t lch,
    size_t i,
    std::vector<Letter>& base_word,
    wchar_t& last_mod_key,
    bool skip_vni_processing,
    CorrectionLevel correction_level) {
    
    bool processed = false;
    
    // VNI vowel modifications: 6, 7, 8, 9
    if (ch >= L'6' && ch <= L'9' && !skip_vni_processing) {
        const bool is_doubled = (last_mod_key != L'\0' && last_mod_key == ch);
        if (ch == L'9') {
            // Scan backward to find the first character that can accept d-bar (d/đ)
            for (int idx = static_cast<int>(base_word.size()) - 1; idx >= 0; --idx) {
                wchar_t bv = rules::ToLower(base_word[idx].current);
                if (bv == L'd' || bv == L'đ') {
                    bool is_upper = (base_word[idx].current != bv);
                    if (bv == L'd') base_word[idx].current = is_upper ? L'Đ' : L'đ';
                    else base_word[idx].current = is_upper ? L'D' : L'd';
                    processed = true;
                    break;
                }
            }
        } else if (ch == L'6') {
            // circumflex on a, e, o
            for (int idx = static_cast<int>(base_word.size()) - 1; idx >= 0; --idx) {
                wchar_t bv = rules::ToLower(base_word[idx].current);
                bool is_upper = (base_word[idx].current != bv);
                if (bv == L'a' || bv == L'â' || bv == L'ă') {
                    base_word[idx].current = (bv == L'â') ? (is_upper ? L'A' : L'a') : (is_upper ? L'Â' : L'â');
                    processed = true;
                    break;
                } else if (bv == L'e' || bv == L'ê') {
                    base_word[idx].current = (bv == L'ê') ? (is_upper ? L'E' : L'e') : (is_upper ? L'Ê' : L'ê');
                    processed = true;
                    break;
                } else if (bv == L'o' || bv == L'ô' || bv == L'ơ') {
                    const bool to_circumflex = (bv != L'ô');
                    if (to_circumflex && idx > 0 &&
                        rules::ToLower(base_word[static_cast<size_t>(idx) - 1].current) == L'ư') {
                        auto& prev = base_word[static_cast<size_t>(idx) - 1];
                        prev.current = (prev.current == L'Ư') ? L'U' : L'u';
                        prev.modified_by_w = false;
                    }
                    base_word[idx].current = to_circumflex ? (is_upper ? L'Ô' : L'ô')
                                                           : (is_upper ? L'O' : L'o');
                    processed = true;
                    break;
                }
            }
        } else if (ch == L'7') {
            // horn on u, o
            if (correction_level != CorrectionLevel::Off && HasUySequence(base_word)) {
                processed = true;
            } else {
                bool has_u = false, has_o = false;
                size_t u_idx = 0, o_idx = 0;
                for (size_t idx = 0; idx < base_word.size(); ++idx) {
                    wchar_t bv = rules::ToLower(base_word[idx].current);
                    const bool is_qu_glide = idx == 1 &&
                        rules::ToLower(base_word[0].current) == L'q';
                    if ((bv == L'u' || bv == L'ư') && !is_qu_glide && !has_u) {
                        has_u = true;
                        u_idx = idx;
                    }
                    else if ((bv == L'o' || bv == L'ơ' || bv == L'ô') && !has_o) {
                        has_o = true;
                        o_idx = idx;
                    }
                }
                if (has_u && has_o && rules::ToLower(base_word[u_idx].current) == L'u' &&
                    rules::ToLower(base_word[o_idx].current) == L'o' &&
                    IsOpenHuoOrThuo(base_word, u_idx, o_idx)) {
                    // "huo7" is huơ, as "huow" is in Telex; an u after it
                    // makes hươu.
                    base_word[o_idx].current = (base_word[o_idx].current == L'O') ? L'Ơ' : L'ơ';
                    processed = true;
                } else if (has_u && has_o) {
                    base_word[u_idx].current = (rules::ToLower(base_word[u_idx].current) == L'u') ? ((base_word[u_idx].current == L'U') ? L'Ư' : L'ư') : ((base_word[u_idx].current == L'Ư') ? L'U' : L'u');
                    base_word[o_idx].current = (rules::ToLower(base_word[o_idx].current) == L'ơ') ? ((base_word[o_idx].current == L'Ơ') ? L'O' : L'o') : ((base_word[o_idx].current == L'O' || base_word[o_idx].current == L'Ô') ? L'Ơ' : L'ơ');
                    processed = true;
                } else if (has_u) {
                    base_word[u_idx].current = (rules::ToLower(base_word[u_idx].current) == L'u') ? ((base_word[u_idx].current == L'U') ? L'Ư' : L'ư') : ((base_word[u_idx].current == L'Ư') ? L'U' : L'u');
                    processed = true;
                } else if (has_o) {
                    base_word[o_idx].current = (rules::ToLower(base_word[o_idx].current) == L'ơ') ? ((base_word[o_idx].current == L'Ơ') ? L'O' : L'o') : ((base_word[o_idx].current == L'O' || base_word[o_idx].current == L'Ô') ? L'Ơ' : L'ơ');
                    processed = true;
                }
            }
        } else if (ch == L'8') {
            // breve on a
            for (int idx = static_cast<int>(base_word.size()) - 1; idx >= 0; --idx) {
                wchar_t bv = rules::ToLower(base_word[idx].current);
                bool is_upper = (base_word[idx].current != bv);
                if (bv == L'a' || bv == L'â' || bv == L'ă') {
                    base_word[idx].current = (bv == L'ă') ? (is_upper ? L'A' : L'a') : (is_upper ? L'Ă' : L'ă');
                    processed = true;
                    break;
                }
            }
        }
        
        if (processed) {
            if (is_doubled) {
                base_word.push_back({ch, ch, false, i, true});
                last_mod_key = L'\0';
            } else {
                last_mod_key = ch;
            }
        } else {
            last_mod_key = L'\0';
        }
    } else {
        last_mod_key = L'\0';
    }
    
    return processed;
}

void SynchronizeHornModification(std::vector<Letter>& base_word) {
    bool has_u_vowel = false;
    bool has_o_vowel = false;
    bool has_horn = false;
    size_t u_idx = 0;
    size_t o_idx = 0;

    for (size_t idx = 0; idx < base_word.size(); ++idx) {
        wchar_t bv = rules::ToLower(base_word[idx].current);
        const bool is_qu_glide = idx == 1 &&
            rules::ToLower(base_word[0].current) == L'q';
        if ((bv == L'u' || bv == L'ư') && !is_qu_glide && !has_u_vowel) {
            has_u_vowel = true;
            u_idx = idx;
            if (bv == L'ư') has_horn = true;
        }
        else if ((bv == L'o' || bv == L'ơ') && !has_o_vowel) {
            has_o_vowel = true;
            o_idx = idx;
            if (bv == L'ơ') has_horn = true;
        }
    }
    
    // huơ and thuở horn the o alone, and the w handler already put the horn
    // there for h or th, u, o with nothing after it. Horning the u as well
    // undid that: "huow" came out hươ, which is no word, and thuở reached
    // the page only because the corrector took the horn off again. Anything
    // typed after the o - hương, thương, hươu - still horns both.
    const bool horned_o_alone =
        has_u_vowel && has_o_vowel &&
        rules::ToLower(base_word[u_idx].current) == L'u' &&
        rules::ToLower(base_word[o_idx].current) == L'ơ' &&
        o_idx == u_idx + 1 && o_idx + 1 == base_word.size() &&
        ((u_idx == 1 && rules::ToLower(base_word[0].current) == L'h') ||
         (u_idx == 2 && rules::ToLower(base_word[0].current) == L't' &&
          rules::ToLower(base_word[1].current) == L'h'));
    if (has_u_vowel && has_o_vowel && has_horn && !horned_o_alone) {
        base_word[u_idx].current = (base_word[u_idx].current == L'U' || base_word[u_idx].current == L'Ư') ? L'Ư' : L'ư';
        base_word[o_idx].current = (base_word[o_idx].current == L'O' || base_word[o_idx].current == L'Ơ') ? L'Ơ' : L'ơ';
    }
}

// `w_waits_for_vowel`: a Telex w typed before any vowel, with a, o or u still
// to come, is held and put on that vowel - "vwatj" is vặt. The other reading
// is that the w is ư, which is what it means to everyone who types "chwa" for
// chưa. ProcessRawKeys below decides which reading a word gets.
ProcessedResult ProcessRawKeysWith(const std::wstring& raw, InputMethod method,
                                   CorrectionLevel correction_level,
                                   bool w_waits_for_vowel) {
    std::vector<Letter> base_word;
    base_word.reserve(raw.length());
    ToneMark active_tone = ToneMark::None;

    wchar_t last_tone_key = L'\0';
    wchar_t last_mod_key = L'\0';
    bool prev_w_consumed = false;

    // Pending state variables
    wchar_t pending_modifier = L'\0';
    size_t pending_mod_raw_idx = 0;
    
    wchar_t pending_tone_key = L'\0';
    ToneMark pending_tone = ToneMark::None;
    size_t pending_tone_raw_idx = 0;

    // A tone key pressed twice is the user asking for the letter, which no
    // Vietnamese syllable has, so the word is English from there on and
    // every later mark key is a letter too. It used to apply again:
    // "passport" came out paspỏt, "stuffs" stúf, "a111" á1. A shape key given
    // back is different - "booong" is boong and still takes its tone, goòng.
    bool tone_given_back = false;

    for (size_t i = 0; i < raw.length(); ++i) {
        wchar_t ch = raw[i];
        wchar_t lch = rules::ToLower(ch);
        
        bool skip_vni_processing = false;
        if (method == InputMethod::VNI && HasDigits(base_word)) {
            if (i == 0 || ch != raw[i - 1]) {
                skip_vni_processing = true;
            }
        }
        
        // 1. Check if it's a tone key
        bool is_tone = false;
        ToneMark tone = ToneMark::None;
        
        if (method == InputMethod::Telex || method == InputMethod::SimpleTelex) {
            if (lch == L's') { tone = ToneMark::Sacute; is_tone = true; }
            else if (lch == L'f') { tone = ToneMark::Grave; is_tone = true; }
            else if (lch == L'r') { tone = ToneMark::Hook; is_tone = true; }
            else if (lch == L'x') { tone = ToneMark::Tilde; is_tone = true; }
            else if (lch == L'j') { tone = ToneMark::Dot; is_tone = true; }
            else if (lch == L'z') { tone = ToneMark::None; is_tone = true; }
        } else if (method == InputMethod::VNI) {
            if (!skip_vni_processing) {
                if (lch == L'1') { tone = ToneMark::Sacute; is_tone = true; }
                else if (lch == L'2') { tone = ToneMark::Grave; is_tone = true; }
                else if (lch == L'3') { tone = ToneMark::Hook; is_tone = true; }
                else if (lch == L'4') { tone = ToneMark::Tilde; is_tone = true; }
                else if (lch == L'5') { tone = ToneMark::Dot; is_tone = true; }
                else if (lch == L'0') { tone = ToneMark::None; is_tone = true; }
            }
        }

        // We can only apply tone if there is at least one vowel in the current base word
        bool has_vowels = false;
        for (const auto& l : base_word) {
            if (rules::IsVowel(l.current)) {
                has_vowels = true;
                break;
            }
        }

        bool is_valid_tone_position = false;
        if (has_vowels) {
            is_valid_tone_position = true;
        } else if (method == InputMethod::VNI) {
            bool has_letter_before = false;
            for (size_t k = 0; k < i; ++k) {
                if (rules::IsWordChar(raw[k])) {
                    has_letter_before = true;
                    break;
                }
            }
            bool has_vowels_anywhere = false;
            for (wchar_t rc : raw) {
                if (rules::IsVowel(rc)) {
                    has_vowels_anywhere = true;
                    break;
                }
            }
            if (has_letter_before && has_vowels_anywhere) {
                is_valid_tone_position = true;
            }
        }

        // Check modifier
        bool is_modifier = false;
        if (method == InputMethod::Telex || method == InputMethod::SimpleTelex) {
            if (lch == L'w') {
                is_modifier = true;
            }
        } else if (method == InputMethod::VNI) {
            if (!skip_vni_processing) {
                if (lch == L'6' || lch == L'7' || lch == L'8') {
                    is_modifier = true;
                }
            }
        }

        // 2. Logic pending modifier / tone before vowel
        bool processed_as_pending = false;

        // VNI validation: number key is only a mod/tone if preceded by a letter
        bool is_vni_valid_mod_or_tone = true;
        if (method == InputMethod::VNI) {
            bool has_letter_before = false;
            for (size_t k = 0; k < i; ++k) {
                if (rules::IsWordChar(raw[k])) {
                    has_letter_before = true;
                    break;
                }
            }
            if (!has_letter_before) {
                is_vni_valid_mod_or_tone = false;
            }
        }

        if (!has_vowels && is_vni_valid_mod_or_tone) {
            if (is_modifier) {
                // Scan to see if there is a compatible vowel ahead in raw
                bool has_compatible_vowel_after = false;
                for (size_t k = i + 1; k < raw.length(); ++k) {
                    wchar_t next_lch = rules::ToLower(raw[k]);
                    if (method == InputMethod::Telex || method == InputMethod::SimpleTelex) {
                        if (lch == L'w' && w_waits_for_vowel &&
                            (next_lch == L'a' || next_lch == L'o' || next_lch == L'u')) {
                            has_compatible_vowel_after = true;
                            break;
                        }
                    } else if (method == InputMethod::VNI) {
                        if (lch == L'6' && (next_lch == L'a' || next_lch == L'e' || next_lch == L'o')) {
                            has_compatible_vowel_after = true;
                            break;
                        }
                        if (lch == L'7' && (next_lch == L'u' || next_lch == L'o')) {
                            has_compatible_vowel_after = true;
                            break;
                        }
                        if (lch == L'8' && next_lch == L'a') {
                            has_compatible_vowel_after = true;
                            break;
                        }
                    }
                }
                if (has_compatible_vowel_after) {
                    pending_modifier = ch;
                    pending_mod_raw_idx = i;
                    processed_as_pending = true;
                }
            } else if (is_tone && !is_valid_tone_position) {
                // Only allow pending tone in VNI to prevent conflict with initial consonants/consonant glides in Telex (like r, s, x)
                if (method == InputMethod::VNI) {
                    // Scan to see if there is a vowel ahead in raw
                    bool has_vowels_after = false;
                    for (size_t k = i + 1; k < raw.length(); ++k) {
                        if (rules::IsVowel(raw[k])) {
                            has_vowels_after = true;
                            break;
                        }
                    }
                    if (has_vowels_after) {
                        pending_tone_key = ch;
                        pending_tone = tone;
                        pending_tone_raw_idx = i;
                        processed_as_pending = true;
                    }
                }
            }
        }

        if (processed_as_pending) {
            continue;
        }

        // Process normal key
        bool is_current_vowel = rules::IsVowel(ch) || rules::IsVowel(lch);

        if (!is_current_vowel) {
            // Flush pending modifier/tone as literal if about to process a non-vowel
            if (pending_modifier != L'\0') {
                base_word.push_back({pending_modifier, pending_modifier, false, pending_mod_raw_idx, false});
                pending_modifier = L'\0';
            }
            if (pending_tone_key != L'\0') {
                base_word.push_back({pending_tone_key, pending_tone_key, false, pending_tone_raw_idx, false});
                pending_tone_key = L'\0';
                pending_tone = ToneMark::None;
            }
        }

        // z in Telex and 0 in VNI take the tone off. With no tone on the word
        // there is nothing to take, and the key used to vanish: "voz" was vo,
        // a second z was needed for voz, and pizza came out piza because the
        // second z of the pair took the first one back. A key with nothing to
        // do is the letter it is. The tone it would take off, once there is
        // one, still goes - vosz is vo - and a second press after that still
        // gives the key back, voszz being voz.
        const bool removes_nothing = is_tone && is_valid_tone_position &&
            tone == ToneMark::None && active_tone == ToneMark::None &&
            (last_tone_key == L'\0' || rules::ToLower(last_tone_key) != lch);
        if (tone_given_back) {
            base_word.push_back({ch, ch, false, i, is_tone || is_modifier});
            last_tone_key = L'\0';
            last_mod_key = L'\0';
            prev_w_consumed = false;
        } else if (removes_nothing) {
            base_word.push_back({ch, ch, false, i, false});
            last_tone_key = L'\0';
            last_mod_key = L'\0';
            prev_w_consumed = false;
        } else if (is_tone && is_valid_tone_position) {
            if (last_tone_key != L'\0' && rules::ToLower(last_tone_key) == lch) {
                // Escape tone: remove tone and append literal key
                active_tone = ToneMark::None;
                base_word.push_back({ch, ch, false, i, true});
                last_tone_key = L'\0';
                tone_given_back = true;
            } else {
                active_tone = tone;
                last_tone_key = ch;
            }
            last_mod_key = L'\0';
            prev_w_consumed = false;
        } else {
            // Non-tone character
            bool processed = false;
            
            if (method == InputMethod::Telex || method == InputMethod::SimpleTelex) {
                processed = TryProcessTelexKeys(ch, lch, i, raw, base_word, last_tone_key, prev_w_consumed, correction_level, method);
            } else if (method == InputMethod::VNI) {
                processed = TryProcessVNIKeys(ch, lch, i, base_word, last_mod_key, skip_vni_processing, correction_level);
            }

            if (!processed) {
                base_word.push_back({ch, ch, false, i, false});
                prev_w_consumed = false;
                last_mod_key = L'\0';
            }
        }

        // Apply pending modifier / tone to the newly added vowel
        bool has_vowels_now = false;
        for (const auto& l : base_word) {
            if (rules::IsVowel(l.current)) {
                has_vowels_now = true;
                break;
            }
        }

        if (has_vowels_now && is_current_vowel) {
            wchar_t last_vowel_char = L'\0';
            for (auto it = base_word.rbegin(); it != base_word.rend(); ++it) {
                if (rules::IsVowel(it->current)) {
                    last_vowel_char = rules::ToLower(it->current);
                    break;
                }
            }

            if (pending_modifier != L'\0') {
                wchar_t p_mod_lch = rules::ToLower(pending_modifier);
                bool compatible = false;
                if (method == InputMethod::Telex || method == InputMethod::SimpleTelex) {
                    if (p_mod_lch == L'w' && (last_vowel_char == L'a' || last_vowel_char == L'o' || last_vowel_char == L'u' ||
                                              last_vowel_char == L'ă' || last_vowel_char == L'ơ' || last_vowel_char == L'ư')) {
                        compatible = true;
                    }
                } else if (method == InputMethod::VNI) {
                    if (p_mod_lch == L'6' && (last_vowel_char == L'a' || last_vowel_char == L'e' || last_vowel_char == L'o' ||
                                              last_vowel_char == L'â' || last_vowel_char == L'ê' || last_vowel_char == L'ô')) {
                        compatible = true;
                    }
                    if (p_mod_lch == L'7' && (last_vowel_char == L'u' || last_vowel_char == L'o' ||
                                              last_vowel_char == L'ư' || last_vowel_char == L'ơ')) {
                        compatible = true;
                    }
                    if (p_mod_lch == L'8' && (last_vowel_char == L'a' || last_vowel_char == L'ă')) {
                        compatible = true;
                    }
                }

                if (compatible) {
                    if (method == InputMethod::Telex || method == InputMethod::SimpleTelex) {
                        TryProcessTelexKeys(pending_modifier, p_mod_lch, pending_mod_raw_idx, raw, base_word, last_tone_key, prev_w_consumed, correction_level, method);
                    } else if (method == InputMethod::VNI) {
                        TryProcessVNIKeys(pending_modifier, p_mod_lch, pending_mod_raw_idx, base_word, last_mod_key, skip_vni_processing, correction_level);
                    }
                    pending_modifier = L'\0';
                } else {
                    // Not compatible, flush pending modifier before the last vowel
                    size_t last_vowel_idx = base_word.size() - 1;
                    for (int idx = static_cast<int>(base_word.size()) - 1; idx >= 0; --idx) {
                        if (rules::IsVowel(base_word[idx].current)) {
                            last_vowel_idx = idx;
                            break;
                        }
                    }
                    base_word.insert(base_word.begin() + last_vowel_idx, {pending_modifier, pending_modifier, false, pending_mod_raw_idx, false});
                    pending_modifier = L'\0';
                }
            }

            if (pending_tone_key != L'\0') {
                active_tone = pending_tone;
                last_tone_key = pending_tone_key;
                pending_tone_key = L'\0';
                pending_tone = ToneMark::None;
            }
        }
    }

    // Flush any leftover pending modifier/tone keys
    if (pending_modifier != L'\0') {
        base_word.push_back({pending_modifier, pending_modifier, false, pending_mod_raw_idx, false});
    }
    if (pending_tone_key != L'\0') {
        base_word.push_back({pending_tone_key, pending_tone_key, false, pending_tone_raw_idx, false});
    }

    // Synchronize horn modification for u and o vowel pairs
    SynchronizeHornModification(base_word);

    // Build the string representation of the base word
    std::wstring result_word;
    result_word.reserve(base_word.size());
    for (const auto& l : base_word) {
        result_word.push_back(l.current);
    }

    // Apply the active tone mark.
    //
    // On joined text the tone belongs to the syllable being typed, not to the
    // run as a whole. ApplyTone reads a word and puts the mark where that word
    // wants it, so handed "Đaminh" it answers for the first vowel it can
    // justify and returns "Đàminh". Handed only the tail it returns "mình",
    // and the head in front of it is text the typist already finished.
    if (active_tone != ToneMark::None) {
        result_word = rules::ApplyTone(result_word, active_tone);
    }
    bool has_escaped = false;
    for (const auto& l : base_word) {
        if (l.is_escaped) {
            has_escaped = true;
            break;
        }
    }
    return {result_word, has_escaped};
}

void SecureErase(std::wstring& value);

// Whether some w in the keys could be held for a vowel: no vowel before it,
// and a, o or u after it. Only then is there a second reading to consider.
bool HasWaitingW(const std::wstring& raw) noexcept {
    bool vowel_seen = false;
    for (size_t i = 0; i < raw.length(); ++i) {
        const wchar_t lch = rules::ToLower(raw[i]);
        if (rules::IsVowel(lch)) {
            vowel_seen = true;
        } else if (lch == L'w' && !vowel_seen) {
            for (size_t k = i + 1; k < raw.length(); ++k) {
                const wchar_t next = rules::ToLower(raw[k]);
                if (next == L'a' || next == L'o' || next == L'u') {
                    return true;
                }
            }
        }
    }
    return false;
}

// A w with no vowel in front of it, in Telex, is ư: "chwa" is chưa, "nwowcs"
// is nước, "wf" is ừ. Neokey used to read it the other way whenever a vowel
// followed - as a horn or breve typed early for that vowel - so chwa came out
// chă, mwa mă, lwu lư and nwowcs nớc, the ư vanishing the moment the o was
// typed. That reading is kept for the words that need it and only those:
// "vwatj" has no ư reading, and is still vặt.
//
// Simple Telex exists to leave a w on its own alone, so there it is neither ư
// nor held for a vowel; it only ever marks a vowel typed before it.
// ưa, ưu, ưi, ươi and ươu end a syllable; nothing follows them. The syllable
// validator does not know that - it reads "vưat" as a word waiting for its
// acute - so the choice below asks it separately, and only about the ư the
// other reading would have put there.
bool HasConsonantAfterOpenUHornRhyme(std::wstring_view word) {
    for (size_t start = 0; start < word.length(); ++start) {
        rules::VowelData first;
        if (!rules::GetVowelData(word[start], first) ||
            rules::ToLower(first.raw) != L'ư') {
            continue;
        }
        std::wstring rhyme;
        size_t end = start;
        for (; end < word.length(); ++end) {
            rules::VowelData vd;
            if (!rules::GetVowelData(word[end], vd)) {
                break;
            }
            rhyme.push_back(rules::ToLower(vd.raw));
        }
        const bool open_only = rhyme == L"ưa" || rhyme == L"ưu" ||
            rhyme == L"ưi" || rhyme == L"ươi" || rhyme == L"ươu";
        return open_only && end < word.length();
    }
    return false;
}

ProcessedResult ProcessRawKeys(const std::wstring& raw, InputMethod method,
                               CorrectionLevel correction_level) {
    if (method == InputMethod::SimpleTelex) {
        return ProcessRawKeysWith(raw, method, correction_level, false);
    }
    if (method != InputMethod::Telex || !HasWaitingW(raw)) {
        return ProcessRawKeysWith(raw, method, correction_level, true);
    }
    ProcessedResult as_u_horn =
        ProcessRawKeysWith(raw, method, correction_level, false);
    if (rules::IsValidVietnamese(as_u_horn.word, true) &&
        !HasConsonantAfterOpenUHornRhyme(as_u_horn.word)) {
        return as_u_horn;
    }
    ProcessedResult held = ProcessRawKeysWith(raw, method, correction_level, true);
    // Neither being Vietnamese - "swift", "want" - the display falls back to
    // the keys either way, and the held reading is what this always gave.
    SecureErase(as_u_horn.word);
    return held;
}

// Free typing hands the syllables to the very same processor, one at a time,
// so nothing below this line has to know the mode exists. See free_typing.hpp
// for why the split is derived on every keystroke rather than kept.
free_typing::Composition ComposeRun(const std::wstring& raw, InputMethod method,
                                    CorrectionLevel correction_level,
                                    bool* any_escaped = nullptr) {
    return free_typing::Compose(raw, [&](std::wstring_view segment) {
        const ProcessedResult piece =
            ProcessRawKeys(std::wstring(segment), method, correction_level);
        if (any_escaped) {
            *any_escaped = *any_escaped || piece.has_escaped;
        }
        return piece.word;
    });
}

// UniKey's Quick Telex: a doubled consonant at the start of a word is the
// two-letter onset it stands for - cc ch, gg gi, kk kh, nn ng, pp ph, qq qu,
// tt th - so "ttoi" is thôi. No Vietnamese word starts with a doubled
// consonant, so nothing Vietnamese is lost; a third press gives the two
// letters back, as a third press of any Telex key does. The keys themselves
// are left as typed, so anything that reads them back still sees "tt".
// `escaped` is set when the third press gave the letters back, so the word is
// shown as it is rather than handed back as keys for not being Vietnamese.
std::wstring ExpandQuickTelexOnset(const std::wstring& raw, bool& escaped) {
    escaped = false;
    if (raw.length() < 2) {
        return raw;
    }
    const wchar_t first = rules::ToLower(raw[0]);
    if (rules::ToLower(raw[1]) != first) {
        return raw;
    }
    wchar_t second = 0;
    switch (first) {
        case L'c': second = L'h'; break;
        case L'g': second = L'i'; break;
        case L'k': second = L'h'; break;
        case L'n': second = L'g'; break;
        case L'p': second = L'h'; break;
        case L'q': second = L'u'; break;
        case L't': second = L'h'; break;
        default: return raw;
    }
    std::wstring expanded = raw;
    if (raw.length() >= 3 && rules::ToLower(raw[2]) == first) {
        // Pressed a third time: the two letters, as typed.
        expanded.erase(2, 1);
        escaped = true;
        return expanded;
    }
    // The added letter follows what comes after it: "TTooi" is Thôi, as
    // "Thooi" would be, and "TTOOI" is THÔI.
    const wchar_t case_from = raw.length() >= 3 ? raw[2] : raw[1];
    const bool upper = case_from != rules::ToLower(case_from);
    expanded[1] = upper ? rules::ToUpper(second) : second;
    return expanded;
}

ProcessedResult ProcessRun(const std::wstring& typed_raw, InputMethod method,
                           CorrectionLevel correction_level, bool free_typing,
                           bool quick_telex) {
    bool quick_escaped = false;
    const std::wstring raw =
        quick_telex && (method == InputMethod::Telex ||
                        method == InputMethod::SimpleTelex)
            ? ExpandQuickTelexOnset(typed_raw, quick_escaped)
            : typed_raw;
    if (!free_typing) {
        ProcessedResult result = ProcessRawKeys(raw, method, correction_level);
        result.has_escaped = result.has_escaped || quick_escaped;
        return result;
    }
    bool any_escaped = false;
    const auto composition =
        ComposeRun(raw, method, correction_level, &any_escaped);

    // The syllable still being typed may be a mistyped one, and the split will
    // have broken it apart at the mistyped key rather than around it. See
    // free_typing_repair.hpp. The corrector is handed the uncorrected text, so
    // the processor here is deliberately not the one above.
    if (free_typing::TailRepairAvailable(correction_level)) {
        std::optional<std::wstring> repaired = free_typing::RepairTail(
            composition,
            [&](std::wstring_view segment) {
                return ProcessRawKeys(std::wstring(segment), method,
                                      CorrectionLevel::Off)
                    .word;
            },
            method, correction_level);
        if (repaired) {
            return {std::move(*repaired), any_escaped};
        }
    }
    return {composition.text, any_escaped};
}

void SecureErase(std::wstring& value) {
    if (!value.empty()) {
        SecureZeroMemory(value.data(), value.size() * sizeof(wchar_t));
        value.clear();
    }
}

bool IsValidReconversionCandidate(std::wstring_view candidate) {
    if (candidate.empty()) {
        return false;
    }

    std::wstring lower_candidate;
    lower_candidate.reserve(candidate.length());
    for (wchar_t c : candidate) {
        lower_candidate.push_back(rules::ToLower(c));
    }

    const bool valid =
        speller::IsInDictionary(lower_candidate) ||
        rules::IsValidVietnamese(candidate, true);
    SecureErase(lower_candidate);
    return valid;
}

bool HasVietnameseDiacritic(std::wstring_view word) noexcept {
    for (const wchar_t ch : word) {
        rules::VowelData vowel{};
        if (rules::GetVowelData(ch, vowel)) {
            if (vowel.tone != ToneMark::None ||
                rules::ToLower(vowel.raw) != vowel.base) {
                return true;
            }
        } else if (rules::ToLower(ch) == L'\u0111') {
            return true;
        }
    }
    return false;
}

std::optional<ReconversionCandidate> BuildCandidateFromRaw(
    std::wstring raw,
    std::wstring_view committed_word,
    size_t selection_start,
    size_t selection_end,
    InputMethod method,
    bool allow_same_word = false) {
    if (raw.length() > kMaxRawKeysPerComposition) {
        SecureErase(raw);
        return std::nullopt;
    }

    Engine engine(method);
    engine.SetAutoCorrect(false);
    engine.SetEnglishProtectionLevel(EnglishProtectionLevel::Off);
    engine.SetSmartContextProtection(false);
    for (wchar_t raw_key : raw) {
        engine.ProcessKey(raw_key);
    }

    ReconversionCandidate candidate;
    candidate.replacement = engine.GetDisplayString();
    candidate.selection_start = (std::min)(selection_start, candidate.replacement.length());
    candidate.selection_end = (std::min)(selection_end, candidate.replacement.length());
    engine.SecureClear();
    SecureErase(raw);

    const bool is_same = (std::wstring_view(candidate.replacement) == committed_word);
    if ((is_same && !allow_same_word) ||
        !IsValidReconversionCandidate(candidate.replacement)) {
        SecureErase(candidate.replacement);
        return std::nullopt;
    }

    return candidate;
}

void AppendToneKey(std::wstring& raw, ToneMark tone, InputMethod method) {
    if (tone == ToneMark::None) {
        return;
    }

    if (method == InputMethod::Telex || method == InputMethod::SimpleTelex) {
        if (tone == ToneMark::Sacute) raw.push_back(L's');
        else if (tone == ToneMark::Grave) raw.push_back(L'f');
        else if (tone == ToneMark::Hook) raw.push_back(L'r');
        else if (tone == ToneMark::Tilde) raw.push_back(L'x');
        else if (tone == ToneMark::Dot) raw.push_back(L'j');
    } else if (method == InputMethod::VNI) {
        if (tone == ToneMark::Sacute) raw.push_back(L'1');
        else if (tone == ToneMark::Grave) raw.push_back(L'2');
        else if (tone == ToneMark::Hook) raw.push_back(L'3');
        else if (tone == ToneMark::Tilde) raw.push_back(L'4');
        else if (tone == ToneMark::Dot) raw.push_back(L'5');
    }
}

std::wstring ReconstructRawKeysWithCaretEdit(
    std::wstring_view word,
    size_t selection_start,
    size_t selection_end,
    wchar_t key,
    InputMethod method) {
    std::wstring raw_base;
    std::vector<wchar_t> mods;
    bool has_u_horn = false;
    bool has_o_horn = false;
    ToneMark tone = ToneMark::None;

    for (wchar_t c : word) {
        rules::VowelData vd;
        if (rules::GetVowelData(c, vd)) {
            wchar_t vowel_char = rules::MakeVowel(vd.raw, ToneMark::None, vd.is_upper);
            wchar_t base_char = vd.base;
            if (vd.is_upper) base_char = rules::ToUpper(base_char);
            raw_base.push_back(base_char);

            if (method == InputMethod::Telex || method == InputMethod::SimpleTelex) {
                if (vowel_char == L'â' || vowel_char == L'Â') {
                    mods.push_back(L'a');
                } else if (vowel_char == L'ă' || vowel_char == L'Ă') {
                    mods.push_back(L'w');
                } else if (vowel_char == L'ê' || vowel_char == L'Ê') {
                    mods.push_back(L'e');
                } else if (vowel_char == L'ô' || vowel_char == L'Ô') {
                    mods.push_back(L'o');
                } else if (vowel_char == L'ơ' || vowel_char == L'Ơ') {
                    has_o_horn = true;
                } else if (vowel_char == L'ư' || vowel_char == L'Ư') {
                    has_u_horn = true;
                }
            } else if (method == InputMethod::VNI) {
                if (vowel_char == L'â' || vowel_char == L'Â') {
                    mods.push_back(L'6');
                } else if (vowel_char == L'ă' || vowel_char == L'Ă') {
                    mods.push_back(L'8');
                } else if (vowel_char == L'ê' || vowel_char == L'Ê') {
                    mods.push_back(L'6');
                } else if (vowel_char == L'ô' || vowel_char == L'Ô') {
                    mods.push_back(L'6');
                } else if (vowel_char == L'ơ' || vowel_char == L'Ơ') {
                    has_o_horn = true;
                } else if (vowel_char == L'ư' || vowel_char == L'Ư') {
                    has_u_horn = true;
                }
            }

            if (tone == ToneMark::None && vd.tone != ToneMark::None) {
                tone = vd.tone;
            }
        } else {
            wchar_t lch = rules::ToLower(c);
            if (lch == L'đ') {
                raw_base.push_back(c == L'đ' ? L'd' : L'D');
                if (method == InputMethod::Telex || method == InputMethod::SimpleTelex) {
                    mods.push_back(L'd');
                } else if (method == InputMethod::VNI) {
                    mods.push_back(L'9');
                }
            } else {
                raw_base.push_back(c);
            }
        }
    }

    selection_start = (std::min)(selection_start, raw_base.length());
    selection_end = (std::min)(selection_end, raw_base.length());
    if (selection_start > selection_end) {
        std::swap(selection_start, selection_end);
    }
    raw_base.replace(selection_start, selection_end - selection_start, 1, key);

    if (method == InputMethod::Telex || method == InputMethod::SimpleTelex) {
        if (has_u_horn || has_o_horn) {
            mods.push_back(L'w');
        }
    } else if (method == InputMethod::VNI) {
        if (has_u_horn || has_o_horn) {
            mods.push_back(L'7');
        }
    }

    for (wchar_t mod : mods) {
        raw_base.push_back(mod);
    }
    AppendToneKey(raw_base, tone, method);
    return raw_base;
}

} // namespace

SmartContextKind ClassifySmartContextToken(
    std::wstring_view raw_keys,
    bool underscore_starts_new_word) noexcept {
    if (raw_keys.empty() ||
        raw_keys.length() > kMaxRawKeysPerComposition) {
        return SmartContextKind::None;
    }
    for (const wchar_t ch : raw_keys) {
        if (ch < L'!' || ch > L'~') {
            return SmartContextKind::None;
        }
    }

    if (IsEmailContextToken(raw_keys)) {
        return SmartContextKind::Email;
    }
    if (IsUrlContextToken(raw_keys)) {
        return SmartContextKind::Url;
    }
    // The underscore rule alone is skipped when underscores separate words.
    // "nguyeenx_hoafng_linh" and "user_name" are the same shape, so the only
    // way to have both is to let the typist say which one they are writing.
    // Email, URL and camelCase protection are unaffected either way.
    if ((!underscore_starts_new_word && IsUnderscoreIdentifier(raw_keys)) ||
        HasInternalLowerToUpperTransition(raw_keys) ||
        IsKnownCodeFamilyToken(raw_keys)) {
        return SmartContextKind::Code;
    }
    return SmartContextKind::None;
}

bool ShouldContinueSmartContextToken(
    std::wstring_view raw_keys,
    wchar_t next_char,
    bool underscore_starts_new_word) noexcept {
    if (raw_keys.empty() ||
        raw_keys.length() >= kMaxRawKeysPerComposition ||
        next_char < L'!' || next_char > L'~') {
        return false;
    }

    std::array<wchar_t, kMaxRawKeysPerComposition + 1> candidate{};
    std::copy(raw_keys.begin(), raw_keys.end(), candidate.begin());
    candidate[raw_keys.length()] = next_char;
    const bool should_continue = ClassifySmartContextToken(
        std::wstring_view(candidate.data(), raw_keys.length() + 1),
        underscore_starts_new_word) != SmartContextKind::None;
    SecureZeroMemory(candidate.data(), candidate.size() * sizeof(wchar_t));
    return should_continue;
}

namespace {
std::atomic<bool> g_default_new_style_tone_placement{true};
std::atomic<bool> g_default_quick_telex{false};
} // namespace

void Engine::SetDefaultNewStyleTonePlacement(bool enable) noexcept {
    g_default_new_style_tone_placement.store(enable, std::memory_order_relaxed);
}

bool Engine::DefaultNewStyleTonePlacement() noexcept {
    return g_default_new_style_tone_placement.load(std::memory_order_relaxed);
}

void Engine::SetDefaultQuickTelex(bool enable) noexcept {
    g_default_quick_telex.store(enable, std::memory_order_relaxed);
}

bool Engine::DefaultQuickTelex() noexcept {
    return g_default_quick_telex.load(std::memory_order_relaxed);
}

bool Engine::AcceptsTelexBracket() const {
    if (method_ != InputMethod::Telex || raw_keys_.empty() ||
        raw_overflow_bypass_) {
        return false;
    }
    return rules::IsTelexBracketPosition(processed_word_);
}

Engine::Engine(InputMethod method)
    : method_(method),
      new_style_tone_placement_(DefaultNewStyleTonePlacement()),
      quick_telex_(DefaultQuickTelex()) {
    raw_keys_.reserve(kMaxRawKeysPerComposition + 1);
    processed_word_.reserve(kMaxRawKeysPerComposition + 1);
}

void Engine::SetAutoCorrect(bool enable) {
    if (!enable) {
        correction_level_ = CorrectionLevel::Off;
    } else if (correction_level_ == CorrectionLevel::Off) {
        correction_level_ = CorrectionLevel::Normal;
    }
}

void Engine::SetCorrectionLevel(CorrectionLevel level) noexcept {
    switch (level) {
        case CorrectionLevel::Off:
        case CorrectionLevel::Normal:
        case CorrectionLevel::Advanced:
        case CorrectionLevel::Experimental:
            correction_level_ = level;
            break;
        default:
            correction_level_ = CorrectionLevel::Normal;
            break;
    }
    if (correction_level_ == CorrectionLevel::Experimental) {
        // Build the edit-distance index now rather than inside the first
        // keystroke that reaches it.
        speller::WarmUpEditDistanceIndex();
    }
}

void Engine::SetEnglishProtectionLevel(EnglishProtectionLevel level) noexcept {
    switch (level) {
        case EnglishProtectionLevel::Off:
        case EnglishProtectionLevel::Balanced:
        case EnglishProtectionLevel::EnglishFirst:
            english_protection_level_ = level;
            break;
        default:
            english_protection_level_ = EnglishProtectionLevel::Balanced;
            break;
    }
}

bool Engine::ShouldContinueSmartContext(wchar_t next_char) const noexcept {
    return smart_context_protection_enabled_ &&
        ShouldContinueSmartContextToken(
            raw_keys_, next_char, underscore_starts_new_word_);
}

namespace {

inline wchar_t LowerAsciiLetter(wchar_t ch) noexcept {
    if (ch >= L'A' && ch <= L'Z') {
        return static_cast<wchar_t>(ch - L'A' + L'a');
    }
    return (ch >= L'a' && ch <= L'z') ? ch : 0;
}

// Capitalisation belongs to the position in the word, not to the letter that
// happened to land there: the user shifted the first key of "Thu", so after the
// pair is put back in order the capital stays on the first letter.
inline wchar_t WithCaseOf(wchar_t model, wchar_t letter) noexcept {
    const bool upper = model >= L'A' && model <= L'Z';
    const wchar_t lower = LowerAsciiLetter(letter);
    if (lower == 0) {
        return letter;
    }
    return upper ? static_cast<wchar_t>(lower - L'a' + L'A') : lower;
}

inline bool IsAsciiVowel(wchar_t ch) noexcept {
    const wchar_t lower = LowerAsciiLetter(ch);
    return lower == L'a' || lower == L'e' || lower == L'i' || lower == L'o' ||
           lower == L'u' || lower == L'y';
}

// The two-letter onsets of Vietnamese. "ngh" is the only three-letter one and
// starts with "ng", so it needs no separate entry here.
inline bool IsVietnameseOnsetPair(wchar_t first, wchar_t second) noexcept {
    const wchar_t a = LowerAsciiLetter(first);
    const wchar_t b = LowerAsciiLetter(second);
    if (a == 0 || b == 0) {
        return false;
    }
    switch (a) {
        case L'c': return b == L'h';
        case L'g': return b == L'h' || b == L'i';
        case L'k': return b == L'h';
        case L'n': return b == L'g' || b == L'h';
        case L'p': return b == L'h';
        case L'q': return b == L'u';
        case L't': return b == L'h' || b == L'r';
        default: return false;
    }
}

} // namespace

// A rolled pair is only repaired at the moment the vowel arrives. Waiting for
// the vowel is what makes this safe: "ht" alone could still become "html", but
// "ht" followed by a vowel has no reading other than a transposed "th".
bool Engine::ShouldRepairRolledOnset(wchar_t ch) const noexcept {
    if (has_escaped_ || raw_overflow_bypass_) {
        return false;
    }
    if (raw_keys_.length() != 2) {
        return false;
    }
    if (onset_pair_interval_ms_ > kRolledOnsetWindowMs) {
        return false;
    }
    if (!IsAsciiVowel(ch)) {
        return false;
    }
    const wchar_t first = raw_keys_[0];
    const wchar_t second = raw_keys_[1];
    if (LowerAsciiLetter(first) == 0 || LowerAsciiLetter(second) == 0) {
        return false;
    }
    return !IsVietnameseOnsetPair(first, second) &&
           IsVietnameseOnsetPair(second, first);
}

bool Engine::ProcessKey(wchar_t ch) {
    suppress_auto_correct_ = false;
    // A key typed after Backspace has taken a word back to its keys reads the
    // whole word afresh: "buowc" and then j is bược.
    raw_backspace_mode_ = RawBackspaceMode::None;
    ClearBackspaceDisplay();
    if (ShouldRepairRolledOnset(ch)) {
        const wchar_t first = raw_keys_[0];
        const wchar_t second = raw_keys_[1];
        raw_keys_[0] = WithCaseOf(first, second);
        raw_keys_[1] = WithCaseOf(second, first);
        // Repaired once; a later key must not swap the pair back.
        onset_pair_interval_ms_ = kUnknownKeyInterval;
    }
    raw_keys_.push_back(ch);
    onset_pair_interval_ms_ = raw_keys_.length() == 2
        ? last_key_interval_ms_
        : (raw_keys_.length() < 2 ? kUnknownKeyInterval
                                  : onset_pair_interval_ms_);
    if (raw_keys_.length() > kMaxRawKeysPerComposition) {
        raw_overflow_bypass_ = true;
        SecureErase(processed_word_);
        has_escaped_ = false;
        return true;
    }

    raw_overflow_bypass_ = false;
    auto res = ProcessRun(raw_keys_, method_, correction_level_, free_typing_, quick_telex_);
    processed_word_ = res.word;
    has_escaped_ = res.has_escaped;
    return true;
}

bool Engine::Backspace() {
    if (raw_keys_.empty()) return false;
    ClearBackspaceDisplay();
    suppress_auto_correct_ = true;
    raw_keys_.pop_back();
    // Editing the word by hand is deliberate: whatever timing the onset once
    // had no longer describes what is in the buffer.
    onset_pair_interval_ms_ = kUnknownKeyInterval;
    if (raw_keys_.empty()) {
        raw_overflow_bypass_ = false;
        SecureErase(processed_word_);
        has_escaped_ = false;
        return true;
    }
    if (raw_keys_.length() > kMaxRawKeysPerComposition) {
        raw_overflow_bypass_ = true;
        SecureErase(processed_word_);
        has_escaped_ = false;
        return true;
    }

    raw_overflow_bypass_ = false;
    auto res = ProcessRun(raw_keys_, method_, correction_level_, free_typing_, quick_telex_);
    processed_word_ = res.word;
    has_escaped_ = res.has_escaped;
    return true;
}

namespace {

// A word's letters with every mark taken off, case kept: bươcd is buocd, Đ is
// D. The VNI keys that type a word are its letters plus a digit per mark, so
// dropping the digits leaves the letters. Asked in VNI whatever the user types
// in; nothing here reaches the keyboard. A word with digits of its own has no
// such reading, and the caller keeps its keys instead.
std::optional<std::wstring> LettersWithoutMarks(std::wstring_view word) {
    for (const wchar_t ch : word) {
        if (ch >= L'0' && ch <= L'9') {
            return std::nullopt;
        }
    }
    const std::wstring keys = rules::ReconstructRawKeys(word, InputMethod::VNI);
    std::wstring letters;
    letters.reserve(keys.length());
    for (const wchar_t ch : keys) {
        if (ch < L'0' || ch > L'9') {
            letters.push_back(ch);
        }
    }
    return letters;
}

} // namespace

// Whether the word was on screen as Vietnamese at some point while its keys
// went in: "buowcdk" was bươc before "dk" broke it, "backspace" never had a
// mark. Replayed from the keys rather than remembered, so a word taken back
// from the text after Space reads the same as one still being typed.
bool Engine::WasShownAsVietnamese() const {
    Engine probe = *this;
    probe.SecureClear();
    for (const wchar_t key : raw_keys_) {
        probe.ProcessKey(key);
        if (probe.ComputeDisplayResult().text != probe.raw_keys_) {
            probe.SecureClear();
            return true;
        }
    }
    probe.SecureClear();
    return false;
}

// Why the word on screen is its own keys rather than what they would type, if
// it is. Only a word whose keys do type something else counts: "back" shows
// its keys because they are its letters, and nothing is hidden.
//
// NotVietnamese is a Vietnamese word gone wrong, and is the only reason a
// Backspace takes marks off (VNI's digits always, Telex's mark letters with
// the experimental option). An English word shown as its keys
// is not one, whether the English lists keep it or it merely failed to be
// Vietnamese: "backspace" is in no list, so it counts as English by never
// having shown a mark, and "work", which showed ươ on the way, by being an
// English word.
//
// Telling the two apart replays the word, so it is only done when asked: in
// Telex with the experimental Backspace off, both are taken back one key at a
// time.
Engine::RawDisplayReason Engine::CurrentRawDisplayReason(
    bool tell_mistyped_apart) const {
    if (raw_keys_.empty() || processed_word_.empty() ||
        processed_word_ == raw_keys_ || free_typing_) {
        return RawDisplayReason::None;
    }
    if (ComputeDisplayResult().text != raw_keys_) {
        return RawDisplayReason::None;
    }
    // A URL, an address or code keeps its keys too, even when the Backspace
    // takes off the one character that made it one: "max@" less the @ is
    // "max", not mã. What was typed as an address is still being typed as one.
    if (smart_context_protection_enabled_ &&
        ClassifySmartContextToken(raw_keys_, underscore_starts_new_word_) !=
            SmartContextKind::None) {
        return RawDisplayReason::KeptOnPurpose;
    }
    if (speller::ClassifyEnglishProtection(
            raw_keys_, processed_word_, method_, english_protection_level_) ==
        speller::EnglishProtectionDecision::PreserveRaw) {
        return RawDisplayReason::KeptOnPurpose;
    }
    if (!tell_mistyped_apart) {
        return RawDisplayReason::KeptOnPurpose;
    }
    std::wstring lower;
    lower.reserve(raw_keys_.length());
    for (const wchar_t ch : raw_keys_) {
        lower.push_back(rules::ToLower(ch));
    }
    const bool english_word =
        speller::IsCommonEnglishWord(lower) ||
        speller::LookupBilingualEnglishWord(lower) !=
            speller::EnglishLexiconTier::None;
    SecureErase(lower);
    if (english_word || !WasShownAsVietnamese()) {
        return RawDisplayReason::KeptOnPurpose;
    }
    return RawDisplayReason::NotVietnamese;
}

// Backspace on a word shown as its own keys. The general path below rebuilds
// keys from what is on screen and types them again, and for such a word that
// puts back the marks the word was shown without - and, with the correction
// suppressed after a Backspace, nothing sends it back to its keys: "buowcdk"
// became "bươcd", "backspace" became "bấckpc". Here Backspace takes off the
// last key and the word stays its keys until the next key is typed.
//
// A Vietnamese word gone wrong - shown as its keys because they type nothing
// valid, not because it is English, a URL or code - loses its marks instead:
//
// - In VNI the marks are digits, never letters, so the first Backspace takes
//   off every one of them and nothing else: "buo7c5dk" is "buocdk". The next
//   ones take letters, "buocd", "buoc", and 7 5 on top make "bược".
// - In Telex a mark is a letter, and only strip_marks_on_backspace_ (off by
//   default) takes them: each Backspace takes one key off and the marks with
//   it, "buowcdk" is "buocd", then "buoc", and w j on top make "bược".
bool Engine::BackspaceRawDisplay() {
    if (raw_backspace_mode_ == RawBackspaceMode::None) {
        const bool vni = method_ == InputMethod::VNI;
        const RawDisplayReason reason = CurrentRawDisplayReason(true);
        if (reason == RawDisplayReason::None) {
            return false;
        }
        // One stray key after a finished word is the usual reason for this
        // Backspace: "tiếng" and then a k shows its keys, tieengsk. Taking the
        // k off gives the word back (the user's choice, 2026-10-01) rather
        // than leaving "tieengs" on screen to be committed as keys.
        if (reason == RawDisplayReason::NotVietnamese && BackspaceBackToVietnamese()) {
            return true;
        }
        raw_backspace_mode_ = RawBackspaceMode::Literal;
        if (reason == RawDisplayReason::NotVietnamese) {
            if (vni) {
                if (DropVniMarkDigits()) {
                    return true;
                }
            } else if (strip_marks_on_backspace_) {
                raw_backspace_mode_ = RawBackspaceMode::BaseLetters;
            }
        }
    }

    raw_keys_.pop_back();
    if (raw_keys_.empty()) {
        SecureClear();
        return true;
    }
    auto res = ProcessRun(raw_keys_, method_, correction_level_, free_typing_,
                          quick_telex_);
    if (raw_backspace_mode_ == RawBackspaceMode::BaseLetters) {
        if (std::optional<std::wstring> letters = LettersWithoutMarks(res.word)) {
            SecureErase(raw_keys_);
            SecureErase(res.word);
            raw_keys_ = std::move(*letters);
            res = ProcessRun(raw_keys_, method_, correction_level_,
                             free_typing_, quick_telex_);
        } else {
            raw_backspace_mode_ = RawBackspaceMode::Literal;
        }
    }
    SecureErase(processed_word_);
    processed_word_ = std::move(res.word);
    has_escaped_ = res.has_escaped;
    suppress_auto_correct_ = true;
    return true;
}

// The keys less the last one, when they type a whole Vietnamese word: the
// word is back on screen as that word. False, changing nothing, otherwise -
// "buowcdk" less the k is still no word, and keeps being taken back as keys.
bool Engine::BackspaceBackToVietnamese() {
    if (raw_keys_.length() < 2) {
        return false;
    }
    Engine probe = *this;
    probe.raw_backspace_mode_ = RawBackspaceMode::None;
    probe.ClearBackspaceDisplay();
    probe.raw_keys_.pop_back();
    auto res = ProcessRun(probe.raw_keys_, method_, correction_level_,
                          free_typing_, quick_telex_);
    probe.processed_word_ = res.word;
    probe.has_escaped_ = res.has_escaped;
    probe.suppress_auto_correct_ = true;
    const std::wstring shown = probe.GetDisplayString();
    const bool word = !shown.empty() && shown != probe.raw_keys_ &&
                      rules::IsValidVietnamese(shown, false);
    if (word) {
        SecureErase(raw_keys_);
        SecureErase(processed_word_);
        raw_keys_ = probe.raw_keys_;
        processed_word_ = std::move(res.word);
        has_escaped_ = res.has_escaped;
        suppress_auto_correct_ = true;
        raw_backspace_mode_ = RawBackspaceMode::None;
    } else {
        SecureErase(res.word);
    }
    probe.SecureClear();
    return word;
}

// The longest run of the keys as typed that shows `target` once a Backspace
// has turned correction off. Taking keys back keeps what rebuilding them from
// the screen cannot see: a key given back by doubling ("there", typed therre,
// less its e is "therr", ther - rebuilt it was "ther", thẻ) and the order the
// marks were typed in.
bool Engine::TakeKeysBackTo(const std::wstring& target) {
    for (size_t length = raw_keys_.length() - 1; length > 0; --length) {
        Engine probe = *this;
        probe.raw_backspace_mode_ = RawBackspaceMode::None;
        probe.ClearBackspaceDisplay();
        probe.raw_keys_.resize(length);
        auto res = ProcessRun(probe.raw_keys_, method_, correction_level_,
                              free_typing_, quick_telex_);
        probe.processed_word_ = res.word;
        probe.has_escaped_ = res.has_escaped;
        probe.suppress_auto_correct_ = true;
        const bool matches = probe.GetDisplayString() == target;
        if (matches) {
            SecureErase(raw_keys_);
            SecureErase(processed_word_);
            raw_keys_ = probe.raw_keys_;
            processed_word_ = std::move(res.word);
            has_escaped_ = res.has_escaped;
            suppress_auto_correct_ = true;
        } else {
            SecureErase(res.word);
        }
        probe.SecureClear();
        if (matches) {
            return true;
        }
    }
    return false;
}

// The first Backspace on a VNI word gone wrong: its letters stay, its mark
// digits go. False when there is nothing to take, and the Backspace then takes
// a key as usual.
bool Engine::DropVniMarkDigits() {
    std::optional<std::wstring> letters = LettersWithoutMarks(processed_word_);
    if (!letters || letters->empty() || *letters == raw_keys_) {
        if (letters) {
            SecureErase(*letters);
        }
        return false;
    }
    SecureErase(raw_keys_);
    raw_keys_ = std::move(*letters);
    auto res = ProcessRun(raw_keys_, method_, correction_level_, free_typing_,
                          quick_telex_);
    SecureErase(processed_word_);
    processed_word_ = std::move(res.word);
    has_escaped_ = res.has_escaped;
    suppress_auto_correct_ = true;
    return true;
}

bool Engine::BackspaceDisplayChar() {
    if (raw_overflow_bypass_) {
        return Backspace();
    }
    // What is on screen, read before anything below changes it: an earlier
    // Backspace may have left its own text there. That text is the word, not
    // its keys, even when the keys rebuilt for it read as English - so it is
    // not taken back as keys: "lắm", Backspace, Backspace is l, not law.
    const bool shown_by_backspace =
        !backspace_display_.empty() && backspace_display_raw_ == raw_keys_;
    std::wstring display = GetDisplayString();
    ClearBackspaceDisplay();
    if (!shown_by_backspace && BackspaceRawDisplay()) {
        SecureErase(display);
        return true;
    }

    if (display.empty()) {
        return false;
    }

    display.pop_back();
    if (display.empty()) {
        SecureErase(display);
        SecureClear();
        return true;
    }

    if (!free_typing_ && TakeKeysBackTo(display)) {
        SecureErase(display);
        return true;
    }

    // Reconstructing keys from what is on screen only works for one syllable.
    // Run over joined text it turned every marked letter back into keystrokes
    // and re-split the lot, and what came back was not what had been typed: one
    // Backspace on "kiemtrathutinhnanggotudo" with its marks left the raw keys
    // showing and every mark gone. So only the syllable being edited is rebuilt
    // - the ones before it keep the keys they were made from.
    if (free_typing_) {
        const auto composition =
            ComposeRun(raw_keys_, method_, correction_level_);
        if (!composition.segment_texts.empty()) {
            std::wstring tail = composition.segment_texts.back();
            if (!tail.empty()) {
                tail.pop_back();
            }
            std::wstring rebuilt;
            for (size_t i = 0; i + 1 < composition.raw_segments.size(); ++i) {
                rebuilt += composition.raw_segments[i];
            }
            if (!tail.empty()) {
                rebuilt += rules::ReconstructRawKeys(tail, method_);
            }
            SecureErase(raw_keys_);
            SecureErase(processed_word_);
            raw_keys_ = std::move(rebuilt);
            SecureErase(tail);
            raw_overflow_bypass_ =
                raw_keys_.length() > kMaxRawKeysPerComposition;
            if (!raw_overflow_bypass_) {
                auto tail_res = ProcessRun(raw_keys_, method_,
                                           correction_level_, free_typing_,
                                           quick_telex_);
                processed_word_ = tail_res.word;
                has_escaped_ = tail_res.has_escaped;
            } else {
                has_escaped_ = false;
            }
            suppress_auto_correct_ = true;
            SecureErase(display);
            return true;
        }
    }

    SecureErase(raw_keys_);
    SecureErase(processed_word_);
    raw_keys_ = rules::ReconstructRawKeys(display, method_);
    raw_overflow_bypass_ = raw_keys_.length() > kMaxRawKeysPerComposition;
    if (raw_overflow_bypass_) {
        SecureErase(processed_word_);
        has_escaped_ = false;
        suppress_auto_correct_ = true;
        SecureErase(display);
        return true;
    }
    auto res = ProcessRun(raw_keys_, method_, correction_level_, free_typing_, quick_telex_);
    processed_word_ = res.word;
    has_escaped_ = res.has_escaped;
    suppress_auto_correct_ = true;
    // The rebuilt keys type the marks in one fixed order, and that order can
    // read differently from the screen: "lắ" rebuilt is "laws", which the
    // English lists keep as typed, and "hoặ" is "hoawj", whose tone lands on
    // the o of an unfinished syllable. What the user sees is the word less one
    // character, whatever the keys would show.
    if (!free_typing_ && GetDisplayString() != display) {
        backspace_display_ = display;
        backspace_display_raw_ = raw_keys_;
    }
    SecureErase(display);
    return true;
}

void Engine::Clear() {
    SecureClear();
}

void Engine::ClearCorrectionCache() noexcept {
    SecureErase(correction_cache_word_);
    SecureErase(correction_cache_raw_);
    SecureErase(correction_cache_result_.word);
    correction_cache_result_ = speller::CorrectionResult{};
    correction_cache_valid_ = false;
}

const speller::CorrectionResult& Engine::CachedCorrection() const {
    if (correction_cache_valid_ &&
        correction_cache_level_ == correction_level_ &&
        correction_cache_method_ == method_ &&
        correction_cache_protection_ == english_protection_level_ &&
        correction_cache_word_ == processed_word_ &&
        correction_cache_raw_ == raw_keys_) {
        return correction_cache_result_;
    }

    correction_cache_result_ = speller::CorrectWordEx(
        processed_word_, raw_keys_, correction_level_, method_,
        english_protection_level_);
    correction_cache_word_ = processed_word_;
    correction_cache_raw_ = raw_keys_;
    correction_cache_level_ = correction_level_;
    correction_cache_method_ = method_;
    correction_cache_protection_ = english_protection_level_;
    correction_cache_valid_ = true;
    return correction_cache_result_;
}

void Engine::SecureClear() {
    ClearCorrectionCache();
    SecureErase(raw_keys_);
    SecureErase(processed_word_);
    suppress_auto_correct_ = false;
    has_escaped_ = false;
    raw_overflow_bypass_ = false;
    raw_backspace_mode_ = RawBackspaceMode::None;
    ClearBackspaceDisplay();
    onset_pair_interval_ms_ = kUnknownKeyInterval;
    last_key_interval_ms_ = kUnknownKeyInterval;
}

void Engine::ClearBackspaceDisplay() noexcept {
    SecureErase(backspace_display_);
    SecureErase(backspace_display_raw_);
}

EngineDisplayResult Engine::GetDisplayResult() const {
    if (!backspace_display_.empty() && backspace_display_raw_ == raw_keys_) {
        // Already in the style the user reads, since it was taken from what
        // was on screen.
        EngineDisplayResult shown;
        shown.text = backspace_display_;
        return shown;
    }
    EngineDisplayResult display_result = ComputeDisplayResult();
    if (!new_style_tone_placement_) {
        display_result.text = rules::ToOldStyleTonePlacement(display_result.text);
    }
    return display_result;
}

// In Telex a common Vietnamese syllable takes its keys from an English word -
// "as" is á - and the English word is then typed with the mark key doubled.
// Sometimes the doubled spelling is an English word as well: "ass", "hiss".
// The English lists then kept it as typed, and the word the doubled key was
// reaching for could not be had at all. The doubled spelling gives way only
// in exactly that case: the escape made the word, and that word's own keys
// go to Vietnamese. "class", "off", "less" and "miss" keep their letters,
// since clas is no word, and of, les and mis are not lost to Vietnamese.
bool Engine::DoubledKeyReachesYieldedEnglish() const {
    if (!has_escaped_ || raw_keys_.length() < 3 ||
        (method_ != InputMethod::Telex && method_ != InputMethod::SimpleTelex)) {
        return false;
    }
    const size_t n = raw_keys_.length();
    if (rules::ToLower(raw_keys_[n - 1]) != rules::ToLower(raw_keys_[n - 2])) {
        return false;
    }
    const std::wstring_view english =
        std::wstring_view(raw_keys_).substr(0, n - 1);
    if (processed_word_ != english) {
        return false;
    }
    // And only where the doubled spelling is the rarer word. ass and hiss are
    // in the extended lexicon, which only English First protects; boss,
    // class, off and less are common English and are meant as typed.
    std::wstring raw_lower;
    for (const wchar_t ch : raw_keys_) {
        raw_lower.push_back(rules::ToLower(ch));
    }
    const bool doubled_is_common_english =
        speller::IsCommonEnglishWord(raw_lower) ||
        speller::LookupBilingualEnglishWord(raw_lower) ==
            speller::EnglishLexiconTier::Common;
    if (doubled_is_common_english) {
        return false;
    }
    Engine direct(method_);
    direct.SetCorrectionLevel(CorrectionLevel::Off);
    direct.SetEnglishProtectionLevel(english_protection_level_);
    direct.SetSmartContextProtection(false);
    for (const wchar_t key : english) {
        direct.ProcessKey(key);
    }
    // Taken by a syllable in common use, as everywhere else: Balanced also
    // gives "of" to ò and "less" to lé, rare as they are, and off and less
    // are far likelier to be meant than of and les reached by a doubled key.
    std::wstring lower;
    for (const wchar_t ch : direct.processed_word_) {
        lower.push_back(rules::ToLower(ch));
    }
    const bool yields =
        speller::SyllableFrequencyTier(lower) >= speller::kCommonSyllableTier &&
        speller::ClassifyEnglishProtection(
            english, direct.processed_word_, method_,
            english_protection_level_) ==
            speller::EnglishProtectionDecision::AmbiguousVietnamese;
    direct.SecureClear();
    return yields;
}

EngineDisplayResult Engine::ComputeDisplayResult() const {
    EngineDisplayResult display_result;
    if (raw_overflow_bypass_ ||
        raw_backspace_mode_ != RawBackspaceMode::None) {
        display_result.text = raw_keys_;
        return display_result;
    }

    if (processed_word_.empty()) {
        display_result.text = raw_keys_;
        return display_result;
    }

    if (smart_context_protection_enabled_ &&
        ClassifySmartContextToken(raw_keys_, underscore_starts_new_word_) !=
            SmartContextKind::None) {
        display_result.text = raw_keys_;
        return display_result;
    }

    const auto english_decision = speller::ClassifyEnglishProtection(
        raw_keys_, processed_word_, method_, english_protection_level_);
    if (english_decision == speller::EnglishProtectionDecision::PreserveRaw) {
        display_result.text = DoubledKeyReachesYieldedEnglish()
            ? processed_word_
            : raw_keys_;
        return display_result;
    }

    if (has_escaped_) {
        display_result.text = processed_word_;
        return display_result;
    }

    if (correction_level_ == CorrectionLevel::Off || suppress_auto_correct_) {
        display_result.text = processed_word_;
        return display_result;
    }

    // 1. Run spelling correction on the processed word
    const speller::CorrectionResult& correction = CachedCorrection();
    const std::wstring& corrected = correction.word;

    // Check if the corrected word is in the dictionary (case-insensitive)
    std::wstring lower_corrected;
    lower_corrected.reserve(corrected.length());
    for (wchar_t c : corrected) {
        lower_corrected.push_back(rules::ToLower(c));
    }

    if (speller::IsInDictionary(lower_corrected)) {
        display_result.text = corrected;
        display_result.correction_kind = correction.kind;
        display_result.correction_score = correction.score;
        display_result.correction_changed = correction.changed;
        display_result.correction_high_confidence = correction.high_confidence;
        return display_result;
    }

    // 2. If not in dictionary, check if it's a structurally valid Vietnamese syllable (possibly in-progress)
    if (rules::IsValidVietnamese(corrected, true)) {
        display_result.text = corrected;
        display_result.correction_kind = correction.kind;
        display_result.correction_score = correction.score;
        display_result.correction_changed = correction.changed;
        display_result.correction_high_confidence = correction.high_confidence;
        return display_result;
    }

    // 3. Check if the last two keys form a double-key escape sequence
    if (raw_keys_.length() >= 2) {
        wchar_t last = rules::ToLower(raw_keys_.back());
        wchar_t prev = rules::ToLower(raw_keys_[raw_keys_.length() - 2]);
        if (last == prev) {
            bool is_escape_key = false;
            if (method_ == InputMethod::Telex || method_ == InputMethod::SimpleTelex) {
                is_escape_key = (last == L's' || last == L'f' || last == L'r' || last == L'x' || 
                                 last == L'j' || last == L'z' || last == L'a' || last == L'e' || 
                                 last == L'o' || last == L'd' || last == L'w');
            } else if (method_ == InputMethod::VNI) {
                is_escape_key = (last >= L'0' && last <= L'9');
            }
            if (is_escape_key) {
                display_result.text = processed_word_;
                return display_result;
            }
        }
    }

    // 4. Otherwise, bypass and return raw English keys.
    //
    // Not in free typing. A joined name is never a valid Vietnamese syllable -
    // "Đaminh" is two of them - so this fallback would hand back "DDaminh" and
    // undo the very thing the mode exists for. Reading the word as English is
    // the bilingual guess, and free typing is where the typist has said they
    // are not writing prose.
    if (!free_typing_) {
        display_result.text = raw_keys_;
        return display_result;
    }
    display_result.text = processed_word_;
    return display_result;
}

std::wstring Engine::GetDisplayString() const {
    return GetDisplayResult().text;
}

std::wstring Engine::GetPreCorrectionDisplayString() const {
    if (!backspace_display_.empty() && backspace_display_raw_ == raw_keys_) {
        return backspace_display_;
    }
    if (raw_overflow_bypass_ || processed_word_.empty() ||
        raw_backspace_mode_ != RawBackspaceMode::None) {
        return raw_keys_;
    }
    if (smart_context_protection_enabled_ &&
        ClassifySmartContextToken(raw_keys_, underscore_starts_new_word_) !=
            SmartContextKind::None) {
        return raw_keys_;
    }
    const auto english_decision = speller::ClassifyEnglishProtection(
        raw_keys_, processed_word_, method_, english_protection_level_);
    if (english_decision ==
        speller::EnglishProtectionDecision::PreserveRaw) {
        return DoubledKeyReachesYieldedEnglish() ? processed_word_ : raw_keys_;
    }
    return new_style_tone_placement_
        ? processed_word_
        : rules::ToOldStyleTonePlacement(processed_word_);
}

std::wstring Engine::GetRawString() const {
    return raw_keys_;
}

void Engine::SetInputMethod(InputMethod method) {
    if (method_ != method) {
        method_ = method;
        if (raw_keys_.length() > kMaxRawKeysPerComposition) {
            raw_overflow_bypass_ = true;
            SecureErase(processed_word_);
            has_escaped_ = false;
            return;
        }

        raw_overflow_bypass_ = false;
        auto res = ProcessRun(raw_keys_, method_, correction_level_, free_typing_, quick_telex_);
        processed_word_ = res.word;
        has_escaped_ = res.has_escaped;
    }
}

std::optional<std::wstring> BuildReconversionCandidate(
    std::wstring_view committed_word,
    wchar_t key,
    InputMethod method) {
    auto candidate = BuildReconversionCandidateWithSelection(
        committed_word,
        committed_word.length(),
        committed_word.length(),
        key,
        method);
    if (!candidate) {
        return std::nullopt;
    }
    return std::move(candidate->replacement);
}

static ToneMark GetToneFromKey(wchar_t key, InputMethod method) noexcept {
    wchar_t lch = rules::ToLower(key);
    if (method == InputMethod::Telex || method == InputMethod::SimpleTelex) {
        if (lch == L's') return ToneMark::Sacute;
        if (lch == L'f') return ToneMark::Grave;
        if (lch == L'r') return ToneMark::Hook;
        if (lch == L'x') return ToneMark::Tilde;
        if (lch == L'j') return ToneMark::Dot;
    } else if (method == InputMethod::VNI) {
        if (lch == L'1') return ToneMark::Sacute;
        if (lch == L'2') return ToneMark::Grave;
        if (lch == L'3') return ToneMark::Hook;
        if (lch == L'4') return ToneMark::Tilde;
        if (lch == L'5') return ToneMark::Dot;
    }
    return ToneMark::None;
}

static ToneMark GetWordTone(std::wstring_view word) noexcept {
    for (wchar_t c : word) {
        rules::VowelData vd;
        if (rules::GetVowelData(c, vd) && vd.tone != ToneMark::None) {
            return vd.tone;
        }
    }
    return ToneMark::None;
}

std::optional<ReconversionCandidate> BuildReconversionCandidateWithSelection(
    std::wstring_view committed_word,
    size_t selection_start,
    size_t selection_end,
    wchar_t key,
    InputMethod method) {
    if (committed_word.empty() || key == 0) {
        return std::nullopt;
    }
    if (committed_word.length() > kMaxRawKeysPerComposition) {
        return std::nullopt;
    }
    if (selection_start > selection_end || selection_end > committed_word.length()) {
        return std::nullopt;
    }

    std::wstring raw = rules::ReconstructRawKeys(committed_word, method);
    raw.push_back(key);
    if (raw.length() > kMaxRawKeysPerComposition) {
        SecureErase(raw);
        return std::nullopt;
    }

    const bool key_is_tone = rules::IsToneKey(key, method);
    const bool key_is_mod = rules::IsModificationKey(key, method);
    const bool key_is_tone_or_mod = key_is_tone || key_is_mod;
    const bool at_end =
        selection_start == committed_word.length() && selection_end == committed_word.length();

    if (at_end && key_is_tone) {
        const ToneMark key_tone = GetToneFromKey(key, method);
        const ToneMark word_tone = GetWordTone(committed_word);
        if (key_tone != ToneMark::None && key_tone == word_tone) {
            std::wstring untoned = rules::ApplyTone(committed_word, ToneMark::None);
            if (untoned != committed_word && IsValidReconversionCandidate(untoned)) {
                ReconversionCandidate candidate;
                candidate.replacement = std::move(untoned);
                candidate.selection_start = candidate.replacement.length();
                candidate.selection_end = candidate.replacement.length();
                SecureErase(raw);
                return candidate;
            }
        }
    }

    auto build_append_candidate = [&]() -> std::optional<ReconversionCandidate> {
        size_t candidate_selection_start = selection_start;
        size_t candidate_selection_end = selection_end;
        // A key typed at the end of the word leaves the caret at the end of
        // whatever the word became. That used to be one past the old end for a
        // plain letter and the old end for a Telex mark key - but a, e, o, d
        // and w are letters as often as marks: "b" and a is ba, not a
        // circumflex, and the caret was left after the b. Every key after it
        // went into the middle of the word - b, Space, Backspace, "anh" gave
        // banh with the caret after b, and t with "anh" gave than.
        if (at_end && selection_start == selection_end) {
            candidate_selection_start = (std::numeric_limits<size_t>::max)();
            candidate_selection_end = candidate_selection_start;
        }
        return BuildCandidateFromRaw(
            raw,
            committed_word,
            candidate_selection_start,
            candidate_selection_end,
            method,
            at_end && key_is_tone_or_mod);
    };

    auto build_insert_candidate = [&]() -> std::optional<ReconversionCandidate> {
        if (!rules::IsWordChar(key)) {
            return std::nullopt;
        }
        std::wstring edited_raw = ReconstructRawKeysWithCaretEdit(
            committed_word,
            selection_start,
            selection_end,
            key,
            method);
        const size_t new_caret = selection_start + 1;
        return BuildCandidateFromRaw(
            std::move(edited_raw),
            committed_word,
            new_caret,
            new_caret,
            method);
    };

    if (key_is_tone_or_mod || at_end) {
        auto candidate = build_append_candidate();
        if (candidate) {
            SecureErase(raw);
            return candidate;
        }
    }

    auto inserted = build_insert_candidate();
    if (inserted) {
        SecureErase(raw);
        return inserted;
    }

    if (!key_is_tone_or_mod && !at_end) {
        auto candidate = build_append_candidate();
        SecureErase(raw);
        return candidate;
    }

    SecureErase(raw);
    return std::nullopt;
}

bool ShouldAttemptTypedReconversion(
    const rules::ReconversionSpan& span,
    wchar_t key,
    InputMethod method) noexcept {
    if (key == 0) {
        return false;
    }

    const bool is_tone_or_mod = rules::IsToneKey(key, method) || rules::IsModificationKey(key, method);

    // For VNI, do not allow tone/modification keys (which are digits) to trigger reconversion at the start of a word.
    if (method == InputMethod::VNI && is_tone_or_mod) {
        if (span.selection_start == span.selection_end && span.selection_start <= span.start) {
            return false;
        }
    }

    if (is_tone_or_mod) {
        return true;
    }

    if (span.selection_start != span.selection_end) {
        return false;
    }

    return span.selection_start > span.start;
}

std::optional<ReconversionEdit> BuildReconversionEdit(
    std::wstring_view text,
    size_t selection_start,
    size_t selection_end,
    wchar_t key,
    InputMethod method,
    bool truncated_left,
    bool truncated_right) {
    auto span = rules::ResolveReconversionSpan(
        text,
        selection_start,
        selection_end,
        truncated_left,
        truncated_right,
        kMaxRawKeysPerComposition);
    if (!span) {
        return std::nullopt;
    }
    if (span->end - span->start > kMaxRawKeysPerComposition) {
        return std::nullopt;
    }

    if (!ShouldAttemptTypedReconversion(*span, key, method)) {
        return std::nullopt;
    }

    std::optional<ReconversionCandidate> candidate = BuildReconversionCandidateWithSelection(
        text.substr(span->start, span->end - span->start),
        span->selection_start - span->start,
        span->selection_end - span->start,
        key,
        method);
    if (!candidate) {
        return std::nullopt;
    }

    ReconversionEdit edit;
    edit.start = span->start;
    edit.end = span->end;
    edit.selection_start = (std::min)(candidate->selection_start, candidate->replacement.length());
    edit.selection_end = (std::min)(candidate->selection_end, candidate->replacement.length());
    edit.replacement = std::move(candidate->replacement);
    return edit;
}

// A word's letters with every mark taken off, in order.
//
// Built by asking which VNI keys would type the word and dropping the digits:
// VNI spells every mark as a digit and every letter as itself, so what is left
// is the letters. It is asked in VNI whatever the user types in - this compares
// one spelling of a word against another and never goes near the keyboard.
std::wstring BaseLettersForComparison(std::wstring_view word) {
    const std::wstring keys =
        rules::ReconstructRawKeys(word, InputMethod::VNI);
    std::wstring letters;
    letters.reserve(keys.length());
    for (const wchar_t ch : keys) {
        if (ch < L'0' || ch > L'9') {
            letters.push_back(rules::ToLower(ch));
        }
    }
    return letters;
}

std::optional<std::wstring> BrowserUrlTypedKeys::KeysFor(
    std::wstring_view token) {
    // The last key landed as expected: its expectation is now the record.
    if (!expected_text_.empty() && token == expected_text_) {
        SecureErase(text_);
        SecureErase(keys_);
        text_ = std::move(expected_text_);
        keys_ = std::move(expected_keys_);
        expected_text_.clear();
        expected_keys_.clear();
    }
    std::optional<std::wstring> answer;
    if (!text_.empty() && token == text_) {
        answer = keys_;
    } else {
        // Something else is in the box. Whatever was remembered belongs to a
        // word that is no longer there, and the same text turning up again
        // later - retyped by hand after a backspace - would not have come
        // from these keys.
        Clear();
    }
    SecureErase(asked_token_);
    if (asked_keys_) {
        SecureErase(*asked_keys_);
    }
    asked_token_.assign(token);
    asked_keys_ = answer;
    return answer;
}

std::optional<std::wstring> BrowserUrlTypedKeys::LastAsk(
    std::wstring_view token) const {
    if (token == asked_token_) {
        return asked_keys_;
    }
    return std::nullopt;
}

void BrowserUrlTypedKeys::Expect(
    std::wstring_view text, std::wstring_view keys) {
    SecureErase(expected_text_);
    SecureErase(expected_keys_);
    expected_text_.assign(text);
    expected_keys_.assign(keys);
}

void BrowserUrlTypedKeys::Clear() noexcept {
    SecureErase(text_);
    SecureErase(keys_);
    SecureErase(expected_text_);
    SecureErase(expected_keys_);
    SecureErase(asked_token_);
    if (asked_keys_) {
        SecureErase(*asked_keys_);
        asked_keys_.reset();
    }
}

std::optional<std::wstring> BuildBrowserUrlTypedReconversionCandidate(
    std::wstring_view committed_token,
    wchar_t key,
    InputMethod method,
    CorrectionLevel correction_level,
    EnglishProtectionLevel english_protection_level,
    bool smart_context_protection_enabled,
    BrowserUrlTypedKeys* typed_keys,
    bool asking_again,
    wchar_t before_token) {
    // The act that follows a test reads what the test saw and records nothing.
    BrowserUrlTypedKeys* const record = asking_again ? nullptr : typed_keys;
    if (committed_token.empty() || key == 0 ||
        committed_token.length() > kMaxRawKeysPerComposition) {
        if (record) {
            record->Clear();
        }
        return std::nullopt;
    }
    for (const wchar_t ch : committed_token) {
        if (!rules::IsWordChar(ch)) {
            if (record) {
                record->Clear();
            }
            return std::nullopt;
        }
    }

    // Esc: the keys as typed, from the record only. See
    // kBrowserUrlRestoreKeysKey.
    if (key == kBrowserUrlRestoreKeysKey) {
        std::optional<std::wstring> keys =
            !typed_keys   ? std::nullopt
            : asking_again ? typed_keys->LastAsk(committed_token)
                           : typed_keys->KeysFor(committed_token);
        if (!keys || *keys == committed_token) {
            if (keys) {
                SecureErase(*keys);
            }
            return std::nullopt;
        }
        // The box will hold the keys, and they are what produced it.
        if (record) {
            record->Expect(*keys, *keys);
        }
        return keys;
    }

    // The rest of an address, not a word: "docs.google.com" typed without
    // https:// had "google" and "com" read as words, and "report.docx" had its
    // extension made "dõc". Nothing Neokey wrote is in this word, so there is
    // nothing to follow either.
    if (IsAddressSeparator(before_token)) {
        if (record) {
            record->Clear();
        }
        return std::nullopt;
    }

    // A Telex bracket is ơ or ư only where the composition would take it, by
    // the same rule. Left to the check below, the corrector made "arr[" arơ.
    const bool bracket_key =
        key == L'[' || key == L']' || key == L'{' || key == L'}';
    if (bracket_key &&
        (method != InputMethod::Telex ||
         !rules::IsTelexBracketPosition(committed_token))) {
        if (record) {
            record->Clear();
        }
        return std::nullopt;
    }

    // The keys behind the word: remembered if it was followed from the start,
    // otherwise read back from the screen. Reading back is exact until the
    // first correction, and a word nobody has corrected is the only kind that
    // arrives without a record.
    std::optional<std::wstring> remembered =
        !typed_keys   ? std::nullopt
        : asking_again ? typed_keys->LastAsk(committed_token)
                       : typed_keys->KeysFor(committed_token);
    std::wstring raw = remembered
        ? std::move(*remembered)
        : rules::ReconstructRawKeys(committed_token, method);
    // The box holds a correction of ours when what is on screen is not simply
    // the keys that were pressed. Going back on it is then not a new edit the
    // word has to justify; it is the word the user was typing all along.
    const bool box_holds_our_correction =
        remembered.has_value() && committed_token != raw;
    if (raw.length() >= kMaxRawKeysPerComposition) {
        SecureErase(raw);
        if (record) {
            record->Clear();
        }
        return std::nullopt;
    }
    const wchar_t normalized_key = rules::ToLower(key);
    const bool repeats_existing_modifier =
        !raw.empty() &&
        rules::ToLower(raw.back()) == normalized_key &&
        (rules::IsToneKey(normalized_key, method) ||
         rules::IsModificationKey(normalized_key, method));
    raw.push_back(key);

    const auto replay_at = [&](CorrectionLevel level) {
        Engine replay(method);
        replay.SetCorrectionLevel(level);
        replay.SetEnglishProtectionLevel(english_protection_level);
        replay.SetSmartContextProtection(smart_context_protection_enabled);
        for (const wchar_t raw_key : raw) {
            replay.ProcessKey(raw_key);
        }
        std::wstring result = replay.GetDisplayString();
        replay.SecureClear();
        return result;
    };

    // A key that only places a mark may not move letters.
    //
    // Everywhere else a correction is a suggestion the next keystroke can
    // overturn, because the engine keeps the keys that were actually typed and
    // re-derives the word from them each time. This path reads the word back
    // out of the document, so without BrowserUrlTypedKeys whatever it writes
    // becomes the input to the next keystroke.
    //
    // That turns one plausible correction into a lost word. "ma" and a
    // circumflex key corrects to "am" with the mark on the a - a real word, and
    // every check below passes it - and from that moment the letters the user
    // typed are gone, "u" is read against the corrected word, and "mau" with
    // its marks can no longer be reached.
    //
    // Only a mark key is held to this. A letter key is allowed to rearrange
    // what is there, because that is what a typo correction is: the n of
    // "tuyetn" is meant to fix the letters, and refusing it would be refusing
    // the feature. A mark key has nothing to fix - it was asked for a mark.
    // Remembering the keys made a letter correction recoverable - "gmail" no
    // longer depends on this rule - but the rule stays: an address bar reacts
    // to every letter with suggestions, and a word that jumps to different
    // letters on a mark key sends those somewhere the user did not type.
    //
    // The correction is dropped, not the keystroke: what goes in is the same
    // word without it, which is what the next key needs to finish the word.
    const bool key_only_places_a_mark =
        rules::IsToneKey(normalized_key, method) ||
        rules::IsModificationKey(normalized_key, method);
    const std::wstring token_letters =
        BaseLettersForComparison(committed_token);
    const auto keeps_letters = [&](std::wstring_view text) {
        if (!key_only_places_a_mark) {
            return true;
        }
        // Handing back exactly the keys that were typed is not moving letters
        // about - it is the escape. "tw" shows tư, and the second w gives tww;
        // held to the letters on screen, t and u, that came out "tưw".
        if (text == raw) {
            return true;
        }
        const std::wstring letters = BaseLettersForComparison(text);
        return letters.length() >= token_letters.length() &&
            letters.compare(0, token_letters.length(), token_letters) == 0;
    };

    std::wstring candidate = replay_at(correction_level);
    if (!keeps_letters(candidate) && correction_level != CorrectionLevel::Off) {
        SecureErase(candidate);
        candidate = replay_at(CorrectionLevel::Off);
    }

    std::wstring native_append;
    native_append.reserve(committed_token.length() + 1);
    native_append.assign(committed_token);
    native_append.push_back(key);
    const bool transformed = candidate != native_append;

    // A key that leaves the word exactly as it was has not been used, and
    // claiming it swallows it. Telex reads z as "take the tone off", so on a
    // word with no tone it produces the word back unchanged - and "vo" stayed
    // "vo" however many times z was pressed, which is a real word nobody could
    // type. Handing the key back puts the letter in, which is what was wanted.
    //
    // Unless the word on screen is a correction of ours. Then an unchanged word
    // means the engine took the key into what it had already written, and the
    // key belongs there: "nguye6" and 4 is corrected to "nguyen" with its marks,
    // and the n typed next is the n the correction already supplied - handing it
    // back made it "nguyenn". The same with a second horn key after VNI has
    // already horned both vowels of "duo".
    const bool changed_the_word =
        candidate != committed_token || box_holds_our_correction;

    const bool candidate_is_valid =
        IsValidReconversionCandidate(candidate);
    const bool committed_token_is_valid =
        IsValidReconversionCandidate(committed_token) ||
        (committed_token.length() == 1 &&
         HasVietnameseDiacritic(committed_token));
    const bool is_verified_escape =
        !candidate_is_valid && repeats_existing_modifier &&
        HasVietnameseDiacritic(committed_token) &&
        committed_token_is_valid;

    // A candidate that is not a Vietnamese word is normally left alone - that
    // is what keeps addresses, English and code native. Undoing a correction
    // of our own is the exception: "gam" was put there by us, "gmai" is what
    // the keys say, and refusing it because "gmai" is not a word would keep a
    // word the user never typed. What goes in is what a composing host would
    // show for the same keys at this point.
    const bool accepted =
        transformed && changed_the_word && keeps_letters(candidate) &&
        (candidate_is_valid || is_verified_escape ||
         box_holds_our_correction);

    // The keys are known when they were remembered, or when reading the word
    // back gave exactly the text on screen - a word with no marks and no
    // corrections, which can only have been typed one letter at a time.
    // Anything else was read back from marks, and is not followed further.
    if (record) {
        const bool keys_are_known =
            remembered.has_value() ||
            (raw.length() == native_append.length() &&
             std::wstring_view(raw).substr(0, raw.length() - 1) ==
                 committed_token);
        if (keys_are_known) {
            record->Expect(accepted ? candidate : native_append, raw);
        } else {
            record->Clear();
        }
    }
    SecureErase(raw);
    SecureErase(native_append);

    if (!accepted) {
        SecureErase(candidate);
        return std::nullopt;
    }
    return candidate;
}

std::optional<std::wstring> BuildBrowserUrlDottedWordCandidate(
    std::wstring_view word_and_dot,
    wchar_t key,
    BrowserUrlTypedKeys* typed_keys,
    bool asking_again) {
    if (!typed_keys || word_and_dot.length() < 2 ||
        word_and_dot.back() != L'.' || !IsAsciiAlphaNumeric(key)) {
        return std::nullopt;
    }
    const std::wstring_view word =
        word_and_dot.substr(0, word_and_dot.length() - 1);
    for (const wchar_t ch : word) {
        if (!rules::IsWordChar(ch)) {
            return std::nullopt;
        }
    }
    std::optional<std::wstring> keys = asking_again
        ? typed_keys->LastAsk(word)
        : typed_keys->KeysFor(word);
    if (!keys || *keys == word) {
        if (keys) {
            SecureErase(*keys);
        }
        return std::nullopt;
    }
    std::wstring candidate = std::move(*keys);
    candidate.push_back(L'.');
    candidate.push_back(key);
    return candidate;
}

ExcelFormulaInputKind ClassifyExcelFormulaPrefix(
    std::wstring_view prefix,
    bool truncated) {
    if (truncated) {
        return ExcelFormulaInputKind::Unknown;
    }
    
    // Skip leading whitespace
    size_t start = 0;
    while (start < prefix.length() && (prefix[start] == L' ' || prefix[start] == L'\t')) {
        ++start;
    }

    if (start >= prefix.length() || prefix[start] != L'=') {
        return ExcelFormulaInputKind::NotFormula;
    }

    bool in_quoted = false;
    for (size_t i = start + 1; i < prefix.length(); ++i) {
        wchar_t ch = prefix[i];
        if (ch == L'"') {
            if (in_quoted && i + 1 < prefix.length() && prefix[i + 1] == L'"') {
                ++i;
            } else {
                in_quoted = !in_quoted;
            }
        }
    }
    
    return in_quoted
        ? ExcelFormulaInputKind::QuotedText
        : ExcelFormulaInputKind::FormulaSyntax;
}

ExcelFormulaSessionState AdvanceExcelFormulaSessionState(
    ExcelFormulaSessionState state,
    wchar_t observed_char,
    bool reset) noexcept {
    if (reset) {
        return ExcelFormulaSessionState::Idle;
    }

    if (state == ExcelFormulaSessionState::Idle) {
        return observed_char == L'='
            ? ExcelFormulaSessionState::PendingFormulaStart
            : state;
    }

    if (state == ExcelFormulaSessionState::PendingFormulaStart) {
        return observed_char == L'"'
            ? ExcelFormulaSessionState::QuotedText
            : state;
    }

    if (observed_char != L'"') {
        return state;
    }

    return state == ExcelFormulaSessionState::FormulaSyntax
        ? ExcelFormulaSessionState::QuotedText
        : ExcelFormulaSessionState::FormulaSyntax;
}

ExcelFormulaSessionState AdoptPendingExcelFormulaSession(
    ExcelFormulaSessionState state) noexcept {
    return state == ExcelFormulaSessionState::PendingFormulaStart
        ? ExcelFormulaSessionState::FormulaSyntax
        : state;
}

ExcelFormulaSessionState MergeExcelFormulaSessionProbe(
    ExcelFormulaSessionState state,
    ExcelFormulaInputKind probe) noexcept {
    if (probe == ExcelFormulaInputKind::FormulaSyntax) {
        return ExcelFormulaSessionState::FormulaSyntax;
    }
    if (probe == ExcelFormulaInputKind::QuotedText) {
        return ExcelFormulaSessionState::QuotedText;
    }
    return state;
}

bool ShouldStartExcelFormulaAtEntry(
    bool local_start_eligible) noexcept {
    return local_start_eligible;
}

size_t AdvanceExcelCellChars(
    size_t committed_chars,
    size_t composing_chars,
    bool is_backspace,
    bool produces_character,
    bool is_composition_key) noexcept {
    if (is_backspace) {
        // A live word takes the Backspace itself; only once it is gone does
        // Backspace start eating what the cell already holds.
        if (composing_chars > 0) {
            return committed_chars;
        }
        return committed_chars > 0 ? committed_chars - 1 : 0;
    }
    if (!produces_character || is_composition_key) {
        // Composition keys land in the word being built, which is counted on
        // its own until something commits it.
        return committed_chars;
    }
    // Anything else ends that word and then inserts itself.
    return committed_chars + composing_chars + 1;
}

bool IsExcelCaretAtCellStart(
    size_t committed_chars,
    size_t composing_chars) noexcept {
    return committed_chars == 0 && composing_chars == 0;
}

bool ShouldExcelHostTypeFirstChar(
    bool is_composition_key,
    bool has_composition,
    bool in_formula_session,
    bool has_native_prefix,
    bool cell_editor_open,
    size_t committed_chars) noexcept {
    if (!is_composition_key || has_composition || in_formula_session) {
        return false;
    }
    // The hand-over exists only to survive the document switch Excel makes when
    // it opens the cell editor. With the editor already open there is no switch
    // to survive, and a character given to the host there can come back wearing
    // an AutoComplete suggestion that nothing is allowed to remove - the cell
    // may hold the user's own text past the caret.
    if (cell_editor_open) {
        return false;
    }
    // One character only: once the host holds one, the next key takes the word
    // over rather than handing another away.
    if (has_native_prefix) {
        return false;
    }
    return IsExcelCaretAtCellStart(committed_chars, 0);
}

bool ShouldReenterExcelQuotedTextOnBackspace(
    bool has_closed_quote,
    size_t formula_chars_after_closed_quote) noexcept {
    return has_closed_quote && formula_chars_after_closed_quote == 0;
}

bool Engine::UpdateCasingFromHost(std::wstring_view host_text) {
    if (host_text.empty() || raw_keys_.empty()) {
        return false;
    }
    if (raw_overflow_bypass_ || raw_keys_.length() > kMaxRawKeysPerComposition) {
        raw_overflow_bypass_ = raw_keys_.length() > kMaxRawKeysPerComposition;
        return false;
    }

    std::wstring current_display = GetDisplayString();
    if (host_text.length() != current_display.length()) {
        SecureErase(current_display);
        return false;
    }

    for (size_t i = 0; i < host_text.length(); ++i) {
        if (rules::ToLower(host_text[i]) != rules::ToLower(current_display[i])) {
            SecureErase(current_display);
            return false;
        }
        if (i > 0 && host_text[i] != current_display[i]) {
            SecureErase(current_display);
            return false;
        }
    }

    if (host_text == current_display) {
        SecureErase(current_display);
        return true;
    }

    const wchar_t host_first = host_text.front();
    const wchar_t current_first = current_display.front();
    const bool host_upper = host_first != rules::ToLower(host_first);
    const bool current_upper = current_first != rules::ToLower(current_first);
    const wchar_t original_raw_first = raw_keys_.front();
    const bool original_has_escaped = has_escaped_;
    std::wstring original_processed_word = processed_word_;
    if (host_upper != current_upper) {
        raw_keys_[0] = host_upper
            ? rules::ToUpper(raw_keys_[0])
            : rules::ToLower(raw_keys_[0]);
        auto res = ProcessRun(raw_keys_, method_, correction_level_, free_typing_, quick_telex_);
        processed_word_ = std::move(res.word);
        has_escaped_ = res.has_escaped;
    }

    SecureErase(current_display);
    std::wstring synchronized_display = GetDisplayString();
    const bool synchronized = host_text == synchronized_display;
    SecureErase(synchronized_display);
    if (!synchronized) {
        raw_keys_[0] = original_raw_first;
        SecureErase(processed_word_);
        processed_word_ = std::move(original_processed_word);
        has_escaped_ = original_has_escaped;
    } else {
        SecureErase(original_processed_word);
    }
    return synchronized;
}

} // namespace vn_ime::core
