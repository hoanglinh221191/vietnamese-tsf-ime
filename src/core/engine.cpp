#include "engine.hpp"
#include "rules.hpp"
#include "speller.hpp"
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
};


std::wstring ProcessRawKeys(const std::wstring& raw, InputMethod method) {
    std::vector<Letter> base_word;
    ToneMark active_tone = ToneMark::None;
    wchar_t last_tone_key = L'\0';
    bool prev_w_consumed = false;

    for (size_t i = 0; i < raw.length(); ++i) {
        wchar_t ch = raw[i];
        wchar_t lch = rules::ToLower(ch);
        
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
            if (lch == L'1') { tone = ToneMark::Sacute; is_tone = true; }
            else if (lch == L'2') { tone = ToneMark::Grave; is_tone = true; }
            else if (lch == L'3') { tone = ToneMark::Hook; is_tone = true; }
            else if (lch == L'4') { tone = ToneMark::Tilde; is_tone = true; }
            else if (lch == L'5') { tone = ToneMark::Dot; is_tone = true; }
            else if (lch == L'0') { tone = ToneMark::None; is_tone = true; }
        }

        // We can only apply tone if there is at least one vowel in the current base word
        bool has_vowels = false;
        for (const auto& l : base_word) {
            if (rules::IsVowel(l.current)) {
                has_vowels = true;
                break;
            }
        }

        if (is_tone && has_vowels) {
            if (last_tone_key != L'\0' && rules::ToLower(last_tone_key) == lch) {
                // Escape tone: remove tone and append literal key
                active_tone = ToneMark::None;
                base_word.push_back({ch, ch, false, i});
                last_tone_key = L'\0';
            } else {
                active_tone = tone;
                last_tone_key = ch;
            }
            prev_w_consumed = false;
        } else {
            // Non-tone character
            bool processed = false;
            
            if (method == InputMethod::Telex || method == InputMethod::SimpleTelex) {
                // Telex double key/free-style modification for a, e, o, d
                if (lch == L'a' || lch == L'e' || lch == L'o' || lch == L'd') {
                    bool modified = false;
                    for (size_t it_idx = base_word.size(); it_idx > 0; --it_idx) {
                        size_t idx = it_idx - 1;
                        auto& letter = base_word[idx];
                        wchar_t cur = letter.current;
                        wchar_t cur_low = rules::ToLower(cur);
                        bool is_upper = (cur != cur_low);
                        
                        // Check if we can modify this character
                        bool can_modify = true;
                        
                        if (can_modify) {
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
                            base_word.push_back({ch, ch, false, i});
                        } else if (!base_word.empty() && rules::ToLower(base_word.back().current) == L'ư') {
                            // Standalone ư -> w
                            base_word.back().current = (ch == L'W') ? L'W' : L'w';
                            base_word.back().original = L'w';
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
                            if (base_vowel == L'u' || base_vowel == L'ư') { has_u = true; u_idx = idx; }
                            else if (base_vowel == L'o' || base_vowel == L'ơ') { has_o = true; o_idx = idx; }
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
                            // Standalone w -> ư
                            base_word.push_back({(ch == L'W') ? L'Ư' : L'ư', L'w', false, i});
                            processed = true;
                        }
                        prev_w_consumed = false;
                        last_tone_key = L'\0';
                    }
                }
            } else if (method == InputMethod::VNI) {
                // VNI vowel modifications: 6, 7, 8, 9
                if (ch >= L'6' && ch <= L'9') {
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
                                base_word[idx].current = (bv == L'ô') ? (is_upper ? L'O' : L'o') : (is_upper ? L'Ô' : L'ô');
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
                            if (bv == L'u' || bv == L'ư') { has_u = true; u_idx = idx; }
                            else if (bv == L'o' || bv == L'ơ' || bv == L'ô') { has_o = true; o_idx = idx; }
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
                }
            }

            if (!processed) {
                base_word.push_back({ch, ch, false, i});
                prev_w_consumed = false;
            }
        }
    }

    // Synchronize horn modification for u and o vowel pairs
    bool has_u_vowel = false;
    bool has_o_vowel = false;
    bool has_horn = false;
    size_t u_idx = 0;
    size_t o_idx = 0;
    
    for (size_t idx = 0; idx < base_word.size(); ++idx) {
        wchar_t bv = rules::ToLower(base_word[idx].current);
        if (bv == L'u' || bv == L'ư') {
            has_u_vowel = true;
            u_idx = idx;
            if (bv == L'ư') has_horn = true;
        }
        else if (bv == L'o' || bv == L'ơ') {
            has_o_vowel = true;
            o_idx = idx;
            if (bv == L'ơ') has_horn = true;
        }
    }
    
    if (has_u_vowel && has_o_vowel && has_horn) {
        base_word[u_idx].current = (base_word[u_idx].current == L'U' || base_word[u_idx].current == L'Ư') ? L'Ư' : L'ư';
        base_word[o_idx].current = (base_word[o_idx].current == L'O' || base_word[o_idx].current == L'Ơ') ? L'Ơ' : L'ơ';
    }

    // Build the string representation of the base word
    std::wstring result_word;
    for (const auto& l : base_word) {
        result_word.push_back(l.current);
    }

    // Apply the active tone mark
    if (active_tone != ToneMark::None) {
        result_word = rules::ApplyTone(result_word, active_tone);
    }
    return result_word;
}

void SecureErase(std::wstring& value) {
    if (!value.empty()) {
        SecureZeroMemory(value.data(), value.size() * sizeof(wchar_t));
        value.clear();
    }
}

} // namespace

Engine::Engine(InputMethod method)
    : method_(method) {}

bool Engine::ProcessKey(wchar_t ch) {
    suppress_auto_correct_ = false;
    raw_keys_.push_back(ch);
    processed_word_ = ProcessRawKeys(raw_keys_, method_);
    return true;
}

bool Engine::Backspace() {
    if (raw_keys_.empty()) return false;
    suppress_auto_correct_ = true;
    raw_keys_.pop_back();
    processed_word_ = ProcessRawKeys(raw_keys_, method_);
    return true;
}

bool Engine::BackspaceDisplayChar() {
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
    processed_word_ = ProcessRawKeys(raw_keys_, method_);
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
}

std::wstring Engine::GetDisplayString() const {
    if (processed_word_.empty()) {
        return raw_keys_;
    }

    if (!enable_auto_correct_ || suppress_auto_correct_) {
        return processed_word_;
    }

    // 1. Run spelling correction on the processed word
    std::wstring corrected = speller::CorrectWord(processed_word_, raw_keys_);

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
        processed_word_ = ProcessRawKeys(raw_keys_, method_);
    }
}

} // namespace vn_ime::core
