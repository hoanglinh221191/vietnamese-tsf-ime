#include "free_typing.hpp"

#include "rules.hpp"

namespace vn_ime::core::free_typing {

namespace {

// Reads as Vietnamese so far. Not the same as being a finished word, and the
// looser test is the one this needs: "nguyen" is only spelled that way while
// its circumflex is still to come, so the validator does not call it complete -
// and requiring complete refused to close any syllable at all, leaving
// "nguyenvanan" in one piece.
//
// Closing on the looser test cannot strand a fragment, because a syllable is
// only ever closed when the next key has already made it impossible to grow.
bool ReadsAsVietnamese(const std::wstring& text) {
    return !text.empty() && rules::IsValidVietnamese(text, true);
}

// A key may reshape the letter it follows and nothing before it.
//
// Reshaping is how Telex works at all - the second "d" of "dd" turns the first
// into an "đ", the "w" of "duw" puts a horn on the "u" - and every one of those
// acts on the letter just typed. Reaching further back is the other thing Telex
// does, placing a mark late: "tana" for "tân". On a joined run that is
// indistinguishable from the next syllable beginning, and reading it as a late
// mark is what turned "nguyenvanan" into "nguyenvann" with the wrong letter
// marked. So a key that would rewrite settled letters is taken as the start of
// the next syllable instead, which is the reading joined text asks for.
// A settled letter may still take a tone mark, which is a different thing: a
// tone belongs to the syllable and lands on whichever vowel the syllable gives
// it, so "hoang" plus "f" marking the "a" is the mark arriving, not a letter
// being rewritten. The comparison is made with the tones taken off, which
// leaves exactly the distinction that matters - "a" against "â" is a rewrite,
// "a" against "à" is not.
bool KeepsSettledLetters(const std::wstring& before, const std::wstring& after) {
    if (before.empty()) {
        return true;
    }
    std::wstring bare_before = rules::ApplyTone(before, ToneMark::None);
    std::wstring bare_after = rules::ApplyTone(after, ToneMark::None);
    bool keeps = true;
    if (!bare_before.empty()) {
        const size_t settled = bare_before.size() - 1;
        keeps = bare_after.size() >= settled &&
                bare_after.compare(0, settled, bare_before, 0, settled) == 0;
    }
    SecureEraseText(bare_before);
    SecureEraseText(bare_after);
    return keeps;
}

}  // namespace

Composition Compose(std::wstring_view raw, const SyllableProcessor& process) {
    Composition result;
    if (raw.empty() || !process) {
        return result;
    }

    // Everything here holds keys or text, so it is sized for the whole run up
    // front: a buffer that grows is copied, and the old one freed with the
    // keys still in it. A run has no more syllables than keys, and the
    // engine's processor makes no more letters than it is given keys. A buffer
    // about to hold less is erased first, so nothing is left past its new end.
    result.text.reserve(raw.length());
    result.raw_segments.reserve(raw.length());
    result.segment_texts.reserve(raw.length());
    std::wstring current_raw;
    std::wstring current_text;
    std::wstring trial_raw;
    current_raw.reserve(raw.length());
    trial_raw.reserve(raw.length());

    for (const wchar_t key : raw) {
        SecureEraseText(trial_raw);
        trial_raw += current_raw;
        trial_raw.push_back(key);
        std::wstring trial_text = process(trial_raw);

        // A key that cannot begin a Vietnamese syllable has nowhere else to
        // go, so there is nothing to disambiguate and the settled-letter rule
        // does not apply to it. Telex's "w" and VNI's digits are the whole of
        // that set. It matters beyond typing: keys reconstructed from text put
        // the modifier at the end of the syllable rather than beside its vowel,
        // and the rule read that as the next syllable starting - one Backspace
        // over "nang" with its marks came back as raw keys.
        const std::wstring key_alone(1, key);
        const bool key_could_start_syllable = ReadsAsVietnamese(key_alone);
        // Swapped rather than moved, here and below: a move frees the buffer
        // it replaces with the syllable so far still in it.
        if (ReadsAsVietnamese(trial_text) &&
            (!key_could_start_syllable ||
             KeepsSettledLetters(current_text, trial_text))) {
            current_raw.swap(trial_raw);
            current_text.swap(trial_text);
            SecureEraseText(trial_text);
            continue;
        }

        // The key does not belong to this syllable, and the syllable can no
        // longer grow. Close it and let the key open the next one.
        if (ReadsAsVietnamese(current_text)) {
            result.raw_segments.push_back(current_raw);
            result.segment_texts.push_back(current_text);
            result.text += current_text;
            SecureEraseText(current_raw);
            current_raw.push_back(key);
            SecureEraseText(current_text);
            current_text = process(current_raw);
            SecureEraseText(trial_text);
            continue;
        }

        // Nowhere better for it to go. Keeping it in the current syllable is
        // what the ordinary engine would do anyway, and it guarantees that
        // everything typed appears in what is shown.
        current_raw.swap(trial_raw);
        current_text.swap(trial_text);
        SecureEraseText(trial_text);
    }

    if (!current_raw.empty()) {
        result.raw_segments.push_back(current_raw);
        result.segment_texts.push_back(current_text);
        result.text += current_text;
    }
    SecureEraseText(current_raw);
    SecureEraseText(current_text);
    SecureEraseText(trial_raw);
    return result;
}

// A character per volatile store, which the compiler may not drop. Not
// SecureZeroMemory: that stores a byte at a time here, on every key.
void SecureEraseText(std::wstring& text) noexcept {
    volatile wchar_t* erased = text.data();
    for (size_t i = 0; i < text.size(); ++i) {
        erased[i] = 0;
    }
    text.clear();
}

}  // namespace vn_ime::core::free_typing
