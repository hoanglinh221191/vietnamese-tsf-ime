#pragma once

namespace vn_ime {

enum class WordEditSessionDispatch {
    Failed,
    Completed,
    RetryAsync,
};

enum class WordReconversionContinuation {
    None,
    ProcessChar,
    Backspace,
};

inline constexpr WordEditSessionDispatch DecideWordEditSessionDispatch(
    bool is_word_app,
    bool request_succeeded,
    bool session_succeeded,
    bool synchronous_unavailable) noexcept {
    if (!request_succeeded) {
        return WordEditSessionDispatch::Failed;
    }
    if (session_succeeded) {
        return WordEditSessionDispatch::Completed;
    }
    if (is_word_app && synchronous_unavailable) {
        return WordEditSessionDispatch::RetryAsync;
    }
    return WordEditSessionDispatch::Failed;
}

inline constexpr bool IsAcceptedWordAsyncEditSession(
    bool request_succeeded,
    bool session_succeeded_or_pending) noexcept {
    return request_succeeded && session_succeeded_or_pending;
}

inline constexpr bool ShouldConsumeDirectInlineMutation(
    bool operation_succeeded,
    bool text_applied) noexcept {
    return operation_succeeded || text_applied;
}

inline constexpr WordReconversionContinuation
DecideWordReconversionContinuation(
    bool reconversion_composition_active,
    bool has_shortcut_modifier,
    bool is_backspace,
    bool is_valid_text_key) noexcept {
    if (!reconversion_composition_active || has_shortcut_modifier) {
        return WordReconversionContinuation::None;
    }
    if (is_backspace) {
        return WordReconversionContinuation::Backspace;
    }
    if (is_valid_text_key) {
        return WordReconversionContinuation::ProcessChar;
    }
    return WordReconversionContinuation::None;
}

} // namespace vn_ime
