#pragma once

// The syllable still being typed, at the end of a joined run.
//
// The splitter decides a boundary by asking whether what it has still reads as
// Vietnamese - and a mistyped key is exactly where a syllable stops reading as
// Vietnamese. So the split cuts AT the slip, every time:
//
//     chungscuae   ->  chungs | cua | e
//     vawndduowkc  ->  vawn | dduow | k | c
//
// The syllable the typist meant is not any one of those pieces, which is why
// correcting the pieces one at a time does nothing whatever: measured that way
// it repaired none of 29,606 runs, and the zero was not a bug. What the typist
// meant is the debris at the end, taken together.
//
// So this leaves the splitter alone and works on what it hands back. The
// trailing pieces that are not words are gathered up with the piece they were
// broken off, and the corrector is asked about the whole of them at once.
// Everything before that is settled - it is on screen with its marks already on
// it - and is never reconsidered. That is the difference from the joined-word
// module that had to be thrown away: that one re-read the run from the start on
// every key, and turned "nguyenvanas" into "nguyenvấn".
//
// A repair is accepted only when the settled syllable before it and the
// repaired syllable are a pair the corpus recorded - the sliding window running
// from the word already settled to the word still being typed. The gate is what
// makes this safe rather than merely clever. Without it the rule is right 61% of
// the time and damages runs that were already correct. With it, over the pair
// corpus, it was right on 2,092 of 2,106 Telex firings and on all 290 VNI ones,
// and fired on no clean run at all.

#include "free_typing.hpp"
#include "types.hpp"

#include <optional>
#include <string>

namespace vn_ime::core::free_typing {

// At most this many pieces are gathered back together, and at most this many
// keys. A slip breaks a syllable into a few fragments, not into a sentence, and
// a bound keeps a long run from being re-read as one enormous word.
inline constexpr size_t kMaxTailRepairPieces = 4;
inline constexpr size_t kMaxTailRepairRawKeys = 12;

// Whether the correction level asks for this at all.
//
// Switching free typing on raises the level, so the repair is on by default in
// the mode. Dropping the level back to Normal is how somebody who finds it
// intrusive turns it off while keeping free typing itself.
inline constexpr bool TailRepairAvailable(CorrectionLevel level) noexcept {
    return level == CorrectionLevel::Advanced ||
           level == CorrectionLevel::Experimental;
}

// The whole run with its last syllable repaired, or nothing when the rule
// declines - which is nearly always.
//
// `plain` must NOT correct. The corrector is handed the text the keys plainly
// make together with the keys themselves, and decides for itself; giving it
// something already corrected asks it the wrong question.
std::optional<std::wstring> RepairTail(const Composition& composed,
                                       const SyllableProcessor& plain,
                                       InputMethod method,
                                       CorrectionLevel level);

}  // namespace vn_ime::core::free_typing
