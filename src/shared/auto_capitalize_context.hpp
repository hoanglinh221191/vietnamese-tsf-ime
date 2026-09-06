#pragma once

#include <string_view>

#include "key_sink_dedupe.hpp"

// Auto-capitalisation needs to know whether the caret sits at the start of a
// sentence. The primary answer comes from the host: Neokey reads the twenty
// characters before the caret through an ITfRange and looks for sentence-ending
// punctuation followed by whitespace.
//
// Hosts reached through the IMM32 bridge cannot answer that question. Qt
// applications such as Telegram Desktop and Viber never implement
// ITextStoreACP, so CUAS hands this service a transitory document that holds
// the composition and nothing else: shifting a range back over text the host
// already owns returns zero characters, and the probe is left with no evidence
// rather than with a "no". Those hosts silently lost the feature.
//
// This tracker is the fallback. Neokey sees every real key before the host
// does, so the characters that decide the question - the sentence-ending
// punctuation and the space after it - pass through the key sinks even when the
// document itself is opaque. Anything that moves the caret somewhere this
// service did not type (a click, an arrow key, Backspace, a shortcut, a focus
// change) drops the state back to Unknown, because the tracker then no longer
// describes where the caret is.
namespace vn_ime::auto_capitalize {

// What the host's own text was able to say about the caret position.
enum class HostProbe {
    // The host returned text and it does not start a sentence.
    NotSentenceStart,
    // The host returned text ending in sentence punctuation plus whitespace.
    SentenceStart,
    // The host returned nothing: no range, no shift, or no text. This is the
    // IMM32-bridge case, and the only one where the typed-key fallback speaks.
    Unknown,
};

enum class TypedContext {
    // Nothing typed since the last caret jump; the fallback stays silent.
    Unknown,
    // The last character typed was ordinary text.
    NotSentenceStart,
    // The last character typed ended a sentence ('.', '?', '!').
    SentenceEnd,
    // Sentence-ending punctuation followed by one or more spaces or tabs.
    SentenceStart,
};

constexpr bool IsWhitespace(wchar_t ch) noexcept {
    return ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L'\n';
}

constexpr bool IsSentenceEndPunctuation(wchar_t ch) noexcept {
    return ch == L'.' || ch == L'?' || ch == L'!';
}

constexpr TypedContext NextTypedContext(
    TypedContext state, wchar_t ch) noexcept {
    if (IsSentenceEndPunctuation(ch)) {
        return TypedContext::SentenceEnd;
    }
    if (IsWhitespace(ch)) {
        // Whitespace only carries a boundary that is already known. Leading
        // whitespace after a caret jump keeps the state Unknown so the very
        // first word typed into an opaque host is never capitalised on a guess.
        return state == TypedContext::SentenceEnd ||
                       state == TypedContext::SentenceStart
                   ? TypedContext::SentenceStart
                   : state == TypedContext::Unknown
                         ? TypedContext::Unknown
                         : TypedContext::NotSentenceStart;
    }
    return TypedContext::NotSentenceStart;
}

// Tracks the caret context across keystrokes.
//
// Characters are observed on key-down, before the host applies them, while the
// auto-capitalisation decision for that same key is taken later in the edit
// session. The state as it stood *before* the current key is therefore the one
// that answers the question, and it is kept alongside the current one.
class TypedContextTracker {
public:
    void Reset() noexcept {
        state_ = TypedContext::Unknown;
        state_before_current_key_ = TypedContext::Unknown;
    }

    void ObserveCharacter(wchar_t ch) noexcept {
        state_before_current_key_ = state_;
        state_ = NextTypedContext(state_, ch);
    }

    // A key that moves or reseats the caret. The tracker no longer knows what
    // precedes it, so it must not answer for the key that follows either.
    void ObserveCaretJump() noexcept { Reset(); }

    [[nodiscard]] TypedContext state() const noexcept { return state_; }

    [[nodiscard]] TypedContext state_before_current_key() const noexcept {
        return state_before_current_key_;
    }

    // True when the key currently being processed starts a new sentence.
    [[nodiscard]] bool StartsSentence() const noexcept {
        return state_before_current_key_ == TypedContext::SentenceStart;
    }

private:
    TypedContext state_ = TypedContext::Unknown;
    TypedContext state_before_current_key_ = TypedContext::Unknown;
};

// Any source that can see the sentence boundary is enough.
//
// The typed-key fallback deliberately is not gated on HostProbe::Unknown. A
// host on the IMM32 bridge does not always fail the read outright: its
// transitory document can hand back the tail of the previous composition,
// which reads as ordinary mid-sentence text and would veto the one source that
// did see the boundary. The tracker only ever says "sentence start" about
// characters this service itself watched being typed, with no caret movement
// since, so there is nothing for the host to correct.
constexpr bool ShouldAutoCapitalize(
    HostProbe host_probe,
    bool focused_control_says_sentence_start,
    bool typed_context_says_sentence_start) noexcept {
    return host_probe == HostProbe::SentenceStart ||
           focused_control_says_sentence_start ||
           typed_context_says_sentence_start;
}

constexpr const wchar_t* HostProbeName(HostProbe probe) noexcept {
    switch (probe) {
        case HostProbe::SentenceStart: return L"sentence-start";
        case HostProbe::NotSentenceStart: return L"mid-sentence";
        default: return L"unknown";
    }
}

constexpr const wchar_t* TypedContextName(TypedContext state) noexcept {
    switch (state) {
        case TypedContext::NotSentenceStart: return L"mid-sentence";
        case TypedContext::SentenceEnd: return L"sentence-end";
        case TypedContext::SentenceStart: return L"sentence-start";
        default: return L"unknown";
    }
}

}  // namespace vn_ime::auto_capitalize
