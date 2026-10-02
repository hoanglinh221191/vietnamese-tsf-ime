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
    if (text.empty()) {
        return false;
    }
    std::wstring lower = Lowered(text);
    const bool word = speller::IsInDictionary(lower);
    SecureEraseText(lower);
    return word;
}

int DictionaryIndexOfLowered(std::wstring_view text) {
    std::wstring lower = Lowered(text);
    const int index = speller::DictionaryIndexOf(lower);
    SecureEraseText(lower);
    return index;
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

    // Measured before the keys are gathered, so that the copy is made once,
    // at its full size, and only when it will be used.
    size_t raw_length = 0;
    for (size_t index = from; index < pieces; ++index) {
        raw_length += composed.raw_segments[index].length();
    }
    if (raw_length > kMaxTailRepairRawKeys) {
        return std::nullopt;
    }
    std::wstring raw;
    raw.reserve(raw_length);
    for (size_t index = from; index < pieces; ++index) {
        raw += composed.raw_segments[index];
    }

    std::wstring plain_text = plain(raw);
    speller::CorrectionResult fixed =
        speller::CorrectWordEx(plain_text, raw, level, method);
    SecureEraseText(plain_text);
    SecureEraseText(raw);
    if (!fixed.changed || !IsWord(fixed.word)) {
        SecureEraseText(fixed.word);
        return std::nullopt;
    }

    // The sliding window: the syllable already settled, and the one still being
    // typed. Only a pair the corpus recorded gets through.
    const int settled =
        DictionaryIndexOfLowered(composed.segment_texts[from - 1]);
    const int repaired = DictionaryIndexOfLowered(fixed.word);
    if (!speller::HasVietnameseBigram(settled, repaired)) {
        SecureEraseText(fixed.word);
        return std::nullopt;
    }

    size_t text_length = fixed.word.length();
    for (size_t index = 0; index < from; ++index) {
        text_length += composed.segment_texts[index].length();
    }
    std::wstring text;
    text.reserve(text_length);
    for (size_t index = 0; index < from; ++index) {
        text += composed.segment_texts[index];
    }
    text += fixed.word;
    SecureEraseText(fixed.word);
    return text;
}

}  // namespace vn_ime::core::free_typing
