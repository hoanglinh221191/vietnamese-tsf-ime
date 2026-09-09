#pragma once

// Free typing: Vietnamese written as one run with no spaces between the
// syllables - "nguyenvanan", "Đaminh", "kiemtra" - which is how names and
// filenames get typed.
//
// The ordinary engine reads a run of keys as one syllable, because that is what
// Vietnamese words are. Everything it does follows from that: a modifier
// searches the word for a vowel to change, a tone mark goes where the word
// wants it, a repeated tone key takes the mark off again. On joined text each
// of those reaches across a boundary that the engine cannot see, and the
// results are wrong in a different way each time - the tone of "hoangflinh"
// landing on "linh", the horn of "hoangduw" landing on "hoang", the second "r"
// of "kieemrtra" swallowed as a repeat instead of typed as a letter.
//
// Narrowing each of those searches one at a time was tried and does not scale:
// the mode ends up threaded through the shared code in six places, and the
// seventh is the one somebody forgets. So the syllables are separated here
// instead, and the engine is handed one at a time - unchanged, and unaware that
// this mode exists.
//
// The split is recomputed from the raw keys on every keystroke and never
// latched. It has to be: when an "a" arrives after "nguyenvan" it is either a
// late mark for "van" or the first letter of "an", and nothing at that moment
// tells them apart. A boundary decided early and kept would be wrong with no
// way back; one derived fresh each time is corrected by the next key.

#include "types.hpp"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace vn_ime::core::free_typing {

// Turns one syllable's worth of raw keys into the text they make. Supplied by
// the caller so this file needs to know nothing about the engine, and so the
// splitting can be tested on its own.
using SyllableProcessor = std::function<std::wstring(std::wstring_view raw)>;

struct Composition {
    // The whole run, syllable by syllable, joined back together.
    std::wstring text;
    // The raw keys each syllable was made from, in order. Their lengths sum to
    // the length of the input.
    std::vector<std::wstring> raw_segments;
    // What each of those makes, in the same order. Joined together they are
    // `text`. Kept apart so an edit can be confined to one syllable - a
    // Backspace rebuilding the whole run turns every marked letter back into
    // keystrokes, and what comes back is not what was typed.
    std::vector<std::wstring> segment_texts;
};

// Splits `raw` into syllables and hands each to `process`.
//
// A key joins the syllable being built while what that syllable makes still
// reads as Vietnamese. When it stops - "kiemt" is not a syllable and never will
// be - the syllable so far is closed if it is finished, and the key starts the
// next one. A key that fits nowhere is kept rather than dropped: the text the
// typist sees must always contain everything they typed.
Composition Compose(std::wstring_view raw, const SyllableProcessor& process);

}  // namespace vn_ime::core::free_typing
