#include "engine.hpp"
#include "rules.hpp"
#include "speller.hpp"
#include <algorithm>
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
};

struct ProcessedResult {
    std::wstring word;
    bool has_escaped = false;
};

bool HasDigits(const std::vector<Letter>& base_word) {
    for (const auto& l : base_word) {
        if (l.current >= L'0' && l.current <= L'9') {
            return true;
        }
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
    bool& prev_w_consumed) {
    
    bool processed = false;
    
    // Telex double key/free-style modification for a, e, o, d
    if (lch == L'a' || lch == L'e' || lch == L'o' || lch == L'd') {
        bool modified = false;
        for (size_t it_idx = base_word.size(); it_idx > 0; --it_idx) {
            size_t idx = it_idx - 1;
            auto& letter = base_word[idx];
            wchar_t cur = letter.current;
            wchar_t cur_low = rules::ToLower(cur);
            bool is_upper = (cur != cur_low);
            
            if (lch == L'e' && cur_low == L'e') {
                letter.current = is_upper ? L'Ê' : L'ê';
                modified = true;
                break;
            }
            else if (lch == L'a' && (cur_low == L'a' || cur_low == L'ă')) {
                letter.current = is_upper ? L'Â' : L'â';
                letter.modified_by_w = false;
                modified = true;
                break;
            }
            else if (lch == L'o' && (cur_low == L'o' || cur_low == L'ơ')) {
                letter.current = is_upper ? L'Ô' : L'ô';
                letter.modified_by_w = false;
                modified = true;
                break;
            }
            else if (lch == L'd' && cur_low == L'd') {
                letter.current = is_upper ? L'Đ' : L'đ';
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
                // Standalone ư -> w
                base_word.back().current = (ch == L'W') ? L'W' : L'w';
                base_word.back().original = L'w';
                base_word.back().is_escaped = true;
            }
            prev_w_consumed = true;
            processed = true;
            last_tone_key = L'\0';
        } else {
            // Try applying w modification
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
                base_word[u_idx].current = (base_word[u_idx].current == L'U' || base_word[u_idx].current == L'Ư') ? L'Ư' : L'ư';
                base_word[u_idx].modified_by_w = true;
                base_word[o_idx].current = (base_word[o_idx].current == L'O' || base_word[o_idx].current == L'Ơ') ? L'Ơ' : L'ơ';
                base_word[o_idx].modified_by_w = true;
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
                if (base_word.empty()) {
                    base_word.push_back({ch, ch, false, i, false});
                } else {
                    // Standalone w after an onset can still form syllables like hw -> hư.
                    base_word.push_back({(ch == L'W') ? L'Ư' : L'ư', L'w', false, i, false});
                }
                processed = true;
            }
            prev_w_consumed = false;
            last_tone_key = L'\0';
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
    bool skip_vni_processing) {
    
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
            if (has_u && has_o) {
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
    
    if (has_u_vowel && has_o_vowel && has_horn) {
        base_word[u_idx].current = (base_word[u_idx].current == L'U' || base_word[u_idx].current == L'Ư') ? L'Ư' : L'ư';
        base_word[o_idx].current = (base_word[o_idx].current == L'O' || base_word[o_idx].current == L'Ơ') ? L'Ơ' : L'ơ';
    }
}

ProcessedResult ProcessRawKeys(const std::wstring& raw, InputMethod method) {
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
                        if (lch == L'w' && (next_lch == L'a' || next_lch == L'o' || next_lch == L'u')) {
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

        if (is_tone && is_valid_tone_position) {
            if (last_tone_key != L'\0' && rules::ToLower(last_tone_key) == lch) {
                // Escape tone: remove tone and append literal key
                active_tone = ToneMark::None;
                base_word.push_back({ch, ch, false, i, true});
                last_tone_key = L'\0';
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
                processed = TryProcessTelexKeys(ch, lch, i, raw, base_word, last_tone_key, prev_w_consumed);
            } else if (method == InputMethod::VNI) {
                processed = TryProcessVNIKeys(ch, lch, i, base_word, last_mod_key, skip_vni_processing);
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
                        TryProcessTelexKeys(pending_modifier, p_mod_lch, pending_mod_raw_idx, raw, base_word, last_tone_key, prev_w_consumed);
                    } else if (method == InputMethod::VNI) {
                        TryProcessVNIKeys(pending_modifier, p_mod_lch, pending_mod_raw_idx, base_word, last_mod_key, skip_vni_processing);
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

    // Apply the active tone mark
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

std::optional<ReconversionCandidate> BuildCandidateFromRaw(
    std::wstring raw,
    std::wstring_view committed_word,
    size_t selection_start,
    size_t selection_end,
    InputMethod method) {
    if (raw.length() > kMaxRawKeysPerComposition) {
        SecureErase(raw);
        return std::nullopt;
    }

    Engine engine(method);
    engine.SetAutoCorrect(false);
    for (wchar_t raw_key : raw) {
        engine.ProcessKey(raw_key);
    }

    ReconversionCandidate candidate;
    candidate.replacement = engine.GetDisplayString();
    candidate.selection_start = (std::min)(selection_start, candidate.replacement.length());
    candidate.selection_end = (std::min)(selection_end, candidate.replacement.length());
    engine.SecureClear();
    SecureErase(raw);

    if (std::wstring_view(candidate.replacement) == committed_word ||
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

Engine::Engine(InputMethod method)
    : method_(method) {
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
}

bool Engine::ProcessKey(wchar_t ch) {
    suppress_auto_correct_ = false;
    raw_keys_.push_back(ch);
    if (raw_keys_.length() > kMaxRawKeysPerComposition) {
        raw_overflow_bypass_ = true;
        SecureErase(processed_word_);
        has_escaped_ = false;
        return true;
    }

    raw_overflow_bypass_ = false;
    auto res = ProcessRawKeys(raw_keys_, method_);
    processed_word_ = res.word;
    has_escaped_ = res.has_escaped;
    return true;
}

bool Engine::Backspace() {
    if (raw_keys_.empty()) return false;
    suppress_auto_correct_ = true;
    raw_keys_.pop_back();
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
    auto res = ProcessRawKeys(raw_keys_, method_);
    processed_word_ = res.word;
    has_escaped_ = res.has_escaped;
    return true;
}

bool Engine::BackspaceDisplayChar() {
    if (raw_overflow_bypass_) {
        return Backspace();
    }

    std::wstring display = GetDisplayString();
    if (display.empty()) {
        return false;
    }

    display.pop_back();
    if (display.empty()) {
        SecureErase(display);
        SecureClear();
        return true;
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
    auto res = ProcessRawKeys(raw_keys_, method_);
    processed_word_ = res.word;
    has_escaped_ = res.has_escaped;
    suppress_auto_correct_ = true;
    SecureErase(display);
    return true;
}

void Engine::Clear() {
    SecureClear();
}

void Engine::SecureClear() {
    SecureErase(raw_keys_);
    SecureErase(processed_word_);
    suppress_auto_correct_ = false;
    has_escaped_ = false;
    raw_overflow_bypass_ = false;
}

std::wstring Engine::GetDisplayString() const {
    if (raw_overflow_bypass_) {
        return raw_keys_;
    }

    if (processed_word_.empty()) {
        return raw_keys_;
    }

    if (has_escaped_) {
        return processed_word_;
    }

    if (correction_level_ == CorrectionLevel::Off || suppress_auto_correct_) {
        return processed_word_;
    }

    // 1. Run spelling correction on the processed word
    std::wstring corrected = speller::CorrectWordEx(processed_word_, raw_keys_, correction_level_, method_).word;

    // Check if the corrected word is in the dictionary (case-insensitive)
    std::wstring lower_corrected;
    lower_corrected.reserve(corrected.length());
    for (wchar_t c : corrected) {
        lower_corrected.push_back(rules::ToLower(c));
    }

    if (speller::IsInDictionary(lower_corrected)) {
        return corrected;
    }

    // 2. If not in dictionary, check if it's a structurally valid Vietnamese syllable (possibly in-progress)
    if (rules::IsValidVietnamese(corrected, true)) {
        return corrected;
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
                return processed_word_;
            }
        }
    }

    // 4. Otherwise, bypass and return raw English keys
    return raw_keys_;
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
        auto res = ProcessRawKeys(raw_keys_, method_);
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

    const bool key_is_tone_or_mod =
        rules::IsToneKey(key, method) || rules::IsModificationKey(key, method);
    const bool at_end =
        selection_start == committed_word.length() && selection_end == committed_word.length();

    auto build_append_candidate = [&]() -> std::optional<ReconversionCandidate> {
        size_t candidate_selection_start = selection_start;
        size_t candidate_selection_end = selection_end;
        if (at_end && selection_start == selection_end &&
            rules::IsWordChar(key) && !key_is_tone_or_mod) {
            candidate_selection_start = selection_start + 1;
            candidate_selection_end = candidate_selection_start;
        }
        return BuildCandidateFromRaw(
            raw,
            committed_word,
            candidate_selection_start,
            candidate_selection_end,
            method);
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

ExcelFormulaInputKind ClassifyExcelFormulaPrefix(
    std::wstring_view prefix,
    bool truncated) {
    if (truncated) {
        return ExcelFormulaInputKind::Unknown;
    }
    
    bool in_formula = false;
    bool in_quoted = false;
    
    for (size_t i = 0; i < prefix.length(); ++i) {
        wchar_t ch = prefix[i];
        if (!in_formula) {
            if (ch == L'=') {
                in_formula = true;
            }
        } else {
            if (ch == L'"') {
                if (in_quoted && i + 1 < prefix.length() && prefix[i + 1] == L'"') {
                    ++i;
                } else {
                    in_quoted = !in_quoted;
                }
            }
        }
    }
    
    if (!in_formula) {
        return ExcelFormulaInputKind::NotFormula;
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

void Engine::UpdateCasingFromHost(const std::wstring& host_text) {
    if (host_text.empty() || raw_keys_.empty()) {
        return;
    }
    if (raw_overflow_bypass_ || raw_keys_.length() > kMaxRawKeysPerComposition) {
        raw_overflow_bypass_ = raw_keys_.length() > kMaxRawKeysPerComposition;
        return;
    }

    wchar_t host_first = host_text[0];
    wchar_t current_first = GetDisplayString().empty() ? L'\0' : GetDisplayString()[0];
    if (current_first != L'\0' && host_first != current_first) {
        bool host_upper = (host_first != rules::ToLower(host_first));
        bool current_upper = (current_first != rules::ToLower(current_first));
        if (host_upper != current_upper) {
            raw_keys_[0] = host_upper ? rules::ToUpper(raw_keys_[0]) : rules::ToLower(raw_keys_[0]);
            auto res = ProcessRawKeys(raw_keys_, method_);
            processed_word_ = res.word;
            has_escaped_ = res.has_escaped;
        }
    }
}

} // namespace vn_ime::core
