#include "free_typing_repair.hpp"

#include "rules.hpp"
#include "speller.hpp"

namespace vn_ime::core::free_typing {

namespace {

std::wstring Lowered(std::wstring_view text) {
    std::wstring lower;
    lower.reserve(text.length());
    for (const wchar_t c : text) {
        lower.push_back(rules::ToLower(c));
    }
    return lower;
}

bool IsWord(std::wstring_view text) {
    return !text.empty() && speller::IsInDictionary(Lowered(text));
}

// Where the debris at the end of the run begins.
//
// Walking back over the trailing pieces that are not words finds the fragments
// the slip made; one more step back picks up the piece they were broken off,
// which is where the syllable actually started. Returns the number of pieces
// when nothing at the end is broken, and zero when the walk reaches the front -
// a run that is debris all the way back has no settled syllable to be judged
// against, so there is nothing to anchor a guess to.
size_t TailStart(const Composition& composed) {
    const size_t pieces = composed.segment_texts.size();
    size_t from = pieces;
    while (from > 0 && !IsWord(composed.segment_texts[from - 1])) {
        --from;
    }
    if (from == pieces || from < 2) {
        return pieces;
    }
    return from - 1;
}

}  // namespace

std::optional<std::wstring> RepairTail(const Composition& composed,
                                       const SyllableProcessor& plain,
                                       InputMethod method,
                                       CorrectionLevel level) {
    if (!plain || !TailRepairAvailable(level)) {
        return std::nullopt;
    }
    const size_t pieces = composed.segment_texts.size();
    if (pieces < 2 || composed.raw_segments.size() != pieces) {
        return std::nullopt;
    }

    const size_t from = TailStart(composed);
    if (from == pieces || from == 0 || pieces - from > kMaxTailRepairPieces) {
        return std::nullopt;
    }

    std::wstring raw;
    for (size_t index = from; index < pieces; ++index) {
        raw += composed.raw_segments[index];
    }
    if (raw.length() > kMaxTailRepairRawKeys) {
        return std::nullopt;
    }

    const speller::CorrectionResult fixed =
        speller::CorrectWordEx(plain(raw), raw, level, method);
    if (!fixed.changed || !IsWord(fixed.word)) {
        return std::nullopt;
    }

    // The sliding window: the syllable already settled, and the one still being
    // typed. Only a pair the corpus recorded gets through.
    const int settled =
        speller::DictionaryIndexOf(Lowered(composed.segment_texts[from - 1]));
    const int repaired = speller::DictionaryIndexOf(Lowered(fixed.word));
    if (!speller::HasVietnameseBigram(settled, repaired)) {
        return std::nullopt;
    }

    std::wstring text;
    for (size_t index = 0; index < from; ++index) {
        text += composed.segment_texts[index];
    }
    text += fixed.word;
    return text;
}

}  // namespace vn_ime::core::free_typing
