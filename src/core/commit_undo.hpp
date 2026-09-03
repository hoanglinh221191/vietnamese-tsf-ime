#pragma once
#include <algorithm>
#include <optional>
#include <limits>
#include <string>
#include <string_view>
#include <vector>
#include <windows.h>
#include <msctf.h>
#include "com_ptr.hpp"
#include "rules.hpp"
#include "types.hpp"

namespace vn_ime {

enum class CommitCaretPolicy {
    MoveToCompositionEnd,
    PreserveHostSelection,
};

inline constexpr bool ShouldMoveCommitCaretToCompositionEnd(
    CommitCaretPolicy policy) noexcept {
    return policy == CommitCaretPolicy::MoveToCompositionEnd;
}

inline constexpr bool NeedsAutoCapitalizeRewrite(
    wchar_t current_first_character,
    wchar_t uppercase_first_character) noexcept {
    return current_first_character != uppercase_first_character;
}

inline constexpr ULONGLONG kCommitUndoRestoreWindowMs = 10000;
inline constexpr size_t kMaxCommitUndoDisplayChars = 4096;
inline constexpr ULONG_PTR kTelegramNativeTransactionMarker =
    static_cast<ULONG_PTR>(0x4E4B5350u);
inline constexpr ULONGLONG kTelegramSyntheticSelectionSuppressionMs = 100;
inline constexpr ULONGLONG kTelegramSelectionRetryWindowMs = 100;
inline constexpr unsigned kTelegramSelectionMaxProbeAttempts = 6;
inline constexpr ULONG_PTR kTelegramRawReplayMarker =
    static_cast<ULONG_PTR>(0x4E4B5250u);
inline constexpr ULONGLONG kTelegramRawReplayWindowMs = 100;

inline bool IsTelegramNativeTransactionMarker(ULONG_PTR marker) noexcept {
    return marker == kTelegramNativeTransactionMarker;
}

inline bool IsTelegramRawReplayMarker(ULONG_PTR marker) noexcept {
    return marker == kTelegramRawReplayMarker;
}

struct TelegramRawReplayKey {
    WORD virtual_key = 0;
    bool shift_down = false;
};

inline bool IsTelegramRawReplayVirtualKey(
    WPARAM virtual_key,
    const std::vector<TelegramRawReplayKey>& plan) noexcept {
    if (virtual_key == VK_SHIFT || virtual_key == VK_LSHIFT ||
        virtual_key == VK_RSHIFT) {
        return std::any_of(
            plan.begin(), plan.end(),
            [](const TelegramRawReplayKey& key) {
                return key.shift_down;
            });
    }
    return std::any_of(
        plan.begin(), plan.end(),
        [virtual_key](const TelegramRawReplayKey& key) {
            return key.virtual_key == virtual_key;
        });
}

inline std::optional<std::vector<TelegramRawReplayKey>>
BuildTelegramRawReplayPlan(
    std::wstring_view raw_keys,
    bool caps_lock_on,
    size_t max_length) {
    if (raw_keys.empty() || max_length == 0 ||
        raw_keys.length() > max_length) {
        return std::nullopt;
    }

    std::vector<TelegramRawReplayKey> plan;
    plan.reserve(raw_keys.length());
    for (const wchar_t key : raw_keys) {
        if (key >= L'a' && key <= L'z') {
            plan.push_back({
                .virtual_key = static_cast<WORD>(L'A' + (key - L'a')),
                .shift_down = caps_lock_on,
            });
        } else if (key >= L'A' && key <= L'Z') {
            plan.push_back({
                .virtual_key = static_cast<WORD>(key),
                .shift_down = !caps_lock_on,
            });
        } else if (key >= L'0' && key <= L'9') {
            plan.push_back({
                .virtual_key = static_cast<WORD>(key),
                .shift_down = false,
            });
        } else {
            return std::nullopt;
        }
    }
    return plan;
}

inline bool ShouldInvalidateCommitUndoOnTestKeyDown(
    WPARAM virtual_key,
    bool is_modifier,
    bool telegram_boundary_pending,
    bool trusted_synthetic_key) noexcept {
    return !is_modifier && !telegram_boundary_pending &&
           !trusted_synthetic_key && virtual_key != VK_BACK &&
           virtual_key != VK_ESCAPE;
}

struct TelegramRawReplaySendDecision {
    bool complete = false;
    bool cleanup_key_up = false;
    bool cleanup_shift_up = false;
};

inline TelegramRawReplaySendDecision DecideTelegramRawReplaySend(
    const TelegramRawReplayKey& key,
    UINT sent_count) noexcept {
    if (!key.shift_down) {
        const UINT bounded = (std::min)(sent_count, 2u);
        return {
            .complete = bounded == 2,
            .cleanup_key_up = bounded == 1,
            .cleanup_shift_up = false,
        };
    }

    const UINT bounded = (std::min)(sent_count, 4u);
    return {
        .complete = bounded == 4,
        .cleanup_key_up = bounded == 2,
        .cleanup_shift_up = bounded >= 1 && bounded < 4,
    };
}

enum class TelegramRawReplayPhase {
    Idle,
    TimerScheduled,
    Dispatching,
};

struct TelegramRawReplayState {
    TelegramRawReplayPhase phase = TelegramRawReplayPhase::Idle;
    ULONGLONG started_tick = 0;
    size_t key_count = 0;

    bool IsPending() const noexcept {
        return phase != TelegramRawReplayPhase::Idle;
    }

    bool Begin(size_t count, ULONGLONG now, size_t max_length) noexcept {
        if (phase != TelegramRawReplayPhase::Idle || count == 0 ||
            count > max_length) {
            return false;
        }
        phase = TelegramRawReplayPhase::TimerScheduled;
        started_tick = now;
        key_count = count;
        return true;
    }

    bool MarkDispatching(
        ULONGLONG now,
        ULONGLONG window_ms = kTelegramRawReplayWindowMs) noexcept {
        if (phase != TelegramRawReplayPhase::TimerScheduled ||
            now < started_tick || now - started_tick > window_ms) {
            return false;
        }
        phase = TelegramRawReplayPhase::Dispatching;
        return true;
    }

    bool Complete() noexcept {
        if (phase != TelegramRawReplayPhase::Dispatching) {
            return false;
        }
        Reset();
        return true;
    }

    bool Cancel() noexcept {
        if (phase == TelegramRawReplayPhase::Idle) {
            return false;
        }
        Reset();
        return true;
    }

    void Reset() noexcept {
        phase = TelegramRawReplayPhase::Idle;
        started_tick = 0;
        key_count = 0;
    }
};

struct TelegramNativeSelectionSendDecision {
    bool consume_physical_backspace = false;
    bool selection_complete = false;
    bool cleanup_required = false;
    bool partial_selection_may_be_active = false;
};

inline TelegramNativeSelectionSendDecision DecideTelegramNativeSelectionSend(
    UINT sent_count) noexcept {
    const UINT bounded_count = (std::min)(sent_count, 8u);
    return {
        .consume_physical_backspace = bounded_count >= 1,
        .selection_complete = bounded_count == 8,
        .cleanup_required = bounded_count >= 1 && bounded_count < 8,
        .partial_selection_may_be_active =
            bounded_count >= 5 && bounded_count < 8,
    };
}

enum class TelegramBoundaryResumePhase {
    Idle,
    TimerScheduled,
    ResumeRequested,
    SelectionVerified,
    TextDeleted,
    CompositionStarted,
};

inline bool IsTelegramSyntheticSelectionVirtualKey(WPARAM virtual_key) noexcept {
    return virtual_key == VK_BACK || virtual_key == VK_CONTROL ||
           virtual_key == VK_LCONTROL || virtual_key == VK_RCONTROL ||
           virtual_key == VK_SHIFT || virtual_key == VK_LSHIFT ||
           virtual_key == VK_RSHIFT || virtual_key == VK_LEFT;
}

struct TelegramSyntheticSelectionSuppressionState {
    bool active = false;
    ULONGLONG deadline_tick = 0;

    void Begin(
        ULONGLONG now,
        ULONGLONG duration_ms =
            kTelegramSyntheticSelectionSuppressionMs) noexcept {
        active = true;
        deadline_tick = now + duration_ms;
    }

    bool ShouldPassThrough(
        TelegramBoundaryResumePhase phase,
        ULONGLONG now,
        WPARAM virtual_key) const noexcept {
        return active &&
               (phase == TelegramBoundaryResumePhase::TimerScheduled ||
                phase == TelegramBoundaryResumePhase::ResumeRequested) &&
               now <= deadline_tick &&
               IsTelegramSyntheticSelectionVirtualKey(virtual_key);
    }

    void Clear() noexcept {
        active = false;
        deadline_tick = 0;
    }
};

struct TelegramBoundaryResumeState {
    TelegramBoundaryResumePhase phase = TelegramBoundaryResumePhase::Idle;
    ULONGLONG started_tick = 0;
    unsigned selection_probe_attempts = 0;

    bool IsPending() const noexcept {
        return phase != TelegramBoundaryResumePhase::Idle;
    }

    bool Begin(ULONGLONG now) noexcept {
        if (phase != TelegramBoundaryResumePhase::Idle) {
            return false;
        }
        phase = TelegramBoundaryResumePhase::TimerScheduled;
        started_tick = now;
        selection_probe_attempts = 0;
        return true;
    }

    bool MarkResumeRequested(
        unsigned max_attempts =
            kTelegramSelectionMaxProbeAttempts) noexcept {
        if (phase != TelegramBoundaryResumePhase::TimerScheduled ||
            selection_probe_attempts >= max_attempts) {
            return false;
        }
        phase = TelegramBoundaryResumePhase::ResumeRequested;
        ++selection_probe_attempts;
        return true;
    }

    bool MarkSelectionRetryScheduled(
        ULONGLONG now,
        ULONGLONG retry_window_ms = kTelegramSelectionRetryWindowMs,
        unsigned max_attempts =
            kTelegramSelectionMaxProbeAttempts) noexcept {
        if (phase != TelegramBoundaryResumePhase::ResumeRequested ||
            selection_probe_attempts >= max_attempts ||
            now < started_tick || now - started_tick > retry_window_ms) {
            return false;
        }
        phase = TelegramBoundaryResumePhase::TimerScheduled;
        return true;
    }

    bool MarkSelectionVerified() noexcept {
        if (phase != TelegramBoundaryResumePhase::ResumeRequested) {
            return false;
        }
        phase = TelegramBoundaryResumePhase::SelectionVerified;
        return true;
    }

    bool MarkTextDeleted() noexcept {
        if (phase != TelegramBoundaryResumePhase::SelectionVerified) {
            return false;
        }
        phase = TelegramBoundaryResumePhase::TextDeleted;
        return true;
    }

    bool MarkCompositionStarted() noexcept {
        if (phase != TelegramBoundaryResumePhase::TextDeleted) {
            return false;
        }
        phase = TelegramBoundaryResumePhase::CompositionStarted;
        return true;
    }

    bool Complete() noexcept {
        if (phase != TelegramBoundaryResumePhase::CompositionStarted) {
            return false;
        }
        Reset();
        return true;
    }

    bool Cancel() noexcept {
        if (phase == TelegramBoundaryResumePhase::Idle) {
            return false;
        }
        Reset();
        return true;
    }

    void Reset() noexcept {
        phase = TelegramBoundaryResumePhase::Idle;
        started_tick = 0;
        selection_probe_attempts = 0;
    }
};

inline bool IsVerifiedTelegramNativeSelection(
    std::wstring_view selected_text,
    std::wstring_view expected_display,
    bool selection_nonempty,
    size_t max_token_length) noexcept {
    return selection_nonempty &&
           !expected_display.empty() &&
           expected_display.length() <= max_token_length &&
           selected_text.length() == expected_display.length() &&
           selected_text == expected_display;
}

enum class TelegramVerifiedTransactionRecovery {
    CollapseSelectionToEnd,
    ReplaceTransactionRangeWithDisplay,
    KeepComposition,
};

inline TelegramVerifiedTransactionRecovery DecideTelegramVerifiedTransactionRecovery(
    bool selection_verified,
    bool text_deleted,
    bool composition_started,
    bool update_succeeded,
    bool active_composition,
    bool caret_positioned) noexcept {
    if (!selection_verified || !text_deleted) {
        return TelegramVerifiedTransactionRecovery::CollapseSelectionToEnd;
    }
    if (composition_started && update_succeeded &&
        active_composition && caret_positioned) {
        return TelegramVerifiedTransactionRecovery::KeepComposition;
    }
    return TelegramVerifiedTransactionRecovery::ReplaceTransactionRangeWithDisplay;
}

struct CommitUndoEntry {
    std::wstring raw_keys;
    // Optional literal source text for commit-time transforms that span more
    // than the current raw-key composition (for example, a two-word fuzzy
    // correction). When present, Smart Undo restores this text directly and
    // must not replay it as Telex/VNI keystrokes.
    std::wstring original_text;
    std::wstring display_text;
    core::InputMethod method = core::InputMethod::Telex;
    enum class TransformKind : uint8_t {
        None,
        SpellerCorrection,
        ShorthandExpansion,
        WordSegmentation,
        FuzzyInput,
    } transform_kind = TransformKind::None;
    unsigned long long selection_generation = 0;
    ULONGLONG committed_tick = 0;
    HWND hwnd = nullptr;
    ComPtr<ITfContext> expected_context;
    ComPtr<ITfRange> committed_text_range;
    ComPtr<ITfRange> expected_caret_range;
    size_t expected_caret_offset = 0;
    bool is_tsf = false;
    bool committed_with_ascii_space = false;
};

struct VerifiedTextSpan {
    size_t start = 0;
    size_t end = 0;
    bool has_trailing_space = false;
};

inline void SecureEraseCommitUndoString(std::wstring& value) noexcept {
    if (!value.empty()) {
        SecureZeroMemory(value.data(), value.size() * sizeof(wchar_t));
        value.clear();
    }
}

inline void SecureClearCommitUndoEntry(CommitUndoEntry& entry) noexcept {
    SecureEraseCommitUndoString(entry.raw_keys);
    SecureEraseCommitUndoString(entry.original_text);
    SecureEraseCommitUndoString(entry.display_text);
    entry.method = core::InputMethod::Telex;
    entry.transform_kind = CommitUndoEntry::TransformKind::None;
    entry.selection_generation = 0;
    entry.committed_tick = 0;
    entry.hwnd = nullptr;
    entry.expected_context.Reset();
    entry.committed_text_range.Reset();
    entry.expected_caret_range.Reset();
    entry.expected_caret_offset = 0;
    entry.is_tsf = false;
    entry.committed_with_ascii_space = false;
}

inline std::wstring_view CommitUndoRestoreText(
    const CommitUndoEntry& entry) noexcept {
    return entry.original_text.empty()
        ? std::wstring_view(entry.raw_keys)
        : std::wstring_view(entry.original_text);
}

inline bool ShouldCaptureCommitUndo(std::wstring_view raw,
                                    std::wstring_view display) noexcept {
    if (raw.empty() || display.empty()) {
        return false;
    }
    if (raw.length() > 128 ||
        display.length() > kMaxCommitUndoDisplayChars) {
        return false;
    }
    return true;
}

inline bool IsSmartUndoTransform(
    CommitUndoEntry::TransformKind transform_kind) noexcept {
    return transform_kind ==
               CommitUndoEntry::TransformKind::SpellerCorrection ||
           transform_kind ==
               CommitUndoEntry::TransformKind::ShorthandExpansion ||
           transform_kind ==
               CommitUndoEntry::TransformKind::WordSegmentation ||
           transform_kind ==
               CommitUndoEntry::TransformKind::FuzzyInput;
}

inline bool ShouldCaptureSmartUndo(const CommitUndoEntry& entry) noexcept {
    const std::wstring_view restore_text = CommitUndoRestoreText(entry);
    return IsSmartUndoTransform(entry.transform_kind) &&
           restore_text != entry.display_text &&
           ShouldCaptureCommitUndo(restore_text, entry.display_text);
}

inline bool IsCommitUndoRestoreWindowValid(
    ULONGLONG now,
    ULONGLONG committed_tick) noexcept {
    return committed_tick != 0 &&
           now >= committed_tick &&
           now - committed_tick <= kCommitUndoRestoreWindowMs;
}

inline bool ShouldRouteSmartUndoBackspace(
    const CommitUndoEntry& entry,
    bool enabled,
    ULONGLONG now,
    bool has_active_composition,
    bool no_modifier,
    bool focus_matches,
    bool context_matches,
    bool selection_valid,
    bool secure_context,
    bool host_supported) noexcept {
    return enabled &&
           !has_active_composition &&
           no_modifier &&
           focus_matches &&
           context_matches &&
           selection_valid &&
           !secure_context &&
           host_supported &&
           entry.committed_with_ascii_space &&
           ShouldCaptureSmartUndo(entry) &&
           IsCommitUndoRestoreWindowValid(now, entry.committed_tick);
}

enum class CommitUndoFocusMode {
    ExactWindow,
    TelegramTsfContext,
};

inline bool IsCommitUndoFocusValid(
    const CommitUndoEntry& entry,
    bool focus_matches,
    bool same_tsf_context,
    CommitUndoFocusMode mode) noexcept {
    if (mode == CommitUndoFocusMode::TelegramTsfContext) {
        return entry.is_tsf && same_tsf_context;
    }
    return focus_matches;
}

inline bool ShouldRouteCommitUndoBackspace(
    const CommitUndoEntry& entry,
    ULONGLONG now,
    bool has_active_composition,
    bool no_modifier,
    bool focus_matches,
    bool host_supported,
    CommitUndoFocusMode focus_mode = CommitUndoFocusMode::ExactWindow,
    bool same_tsf_context = false) noexcept {
    return !has_active_composition &&
           no_modifier &&
           host_supported &&
           entry.original_text.empty() &&
           IsCommitUndoFocusValid(entry, focus_matches, same_tsf_context, focus_mode) &&
           ShouldCaptureCommitUndo(entry.raw_keys, entry.display_text) &&
           IsCommitUndoRestoreWindowValid(now, entry.committed_tick);
}

inline bool CanUseStoredTsfRangeFallback(
    bool is_telegram,
    bool selection_path_unreadable,
    bool stored_word_matches,
    bool boundary_is_space,
    bool caret_at_boundary_end) noexcept {
    return is_telegram &&
           selection_path_unreadable &&
           stored_word_matches &&
           boundary_is_space &&
           caret_at_boundary_end;
}

inline bool ShouldRouteTelegramNativeBoundaryBackspace(
    const CommitUndoEntry& entry,
    ULONGLONG now,
    bool has_active_composition,
    bool no_modifier,
    bool is_telegram,
    bool same_tsf_context,
    bool safe_context,
    bool has_stored_range) noexcept {
    return !has_active_composition &&
           no_modifier &&
           is_telegram &&
           same_tsf_context &&
           safe_context &&
           entry.is_tsf &&
           entry.committed_with_ascii_space &&
           has_stored_range &&
           entry.original_text.empty() &&
           ShouldCaptureCommitUndo(entry.raw_keys, entry.display_text) &&
           IsCommitUndoRestoreWindowValid(now, entry.committed_tick);
}

enum class TelegramBoundaryResumeDisposition {
    PreserveNativeResult,
    ResumeComposition,
};

inline TelegramBoundaryResumeDisposition DecideTelegramBoundaryResumeDisposition(
    bool verification_succeeded,
    bool composition_started,
    bool update_succeeded,
    bool active_composition,
    bool caret_positioned) noexcept {
    return verification_succeeded &&
               composition_started &&
               update_succeeded &&
               active_composition &&
               caret_positioned
        ? TelegramBoundaryResumeDisposition::ResumeComposition
        : TelegramBoundaryResumeDisposition::PreserveNativeResult;
}

enum class CommitUndoResumeDisposition {
    Rollback,
    ResumeComposition,
};

inline CommitUndoResumeDisposition DecideCommitUndoResumeDisposition(
    bool composition_started,
    bool update_succeeded,
    bool active_composition,
    bool caret_positioned) noexcept {
    return composition_started &&
               update_succeeded &&
               active_composition &&
               caret_positioned
        ? CommitUndoResumeDisposition::ResumeComposition
        : CommitUndoResumeDisposition::Rollback;
}

inline bool IsCommitUndoDocumentCleanupSuccessful(
    bool text_cleared,
    bool composition_ended,
    bool active_composition_cleared) noexcept {
    return text_cleared && composition_ended && active_composition_cleared;
}

enum class CommitUndoRollbackDisposition {
    PassThrough,
    ConsumeBackspace,
};

inline CommitUndoRollbackDisposition DecideCommitUndoRollbackDisposition(
    bool rollback_text_verified,
    bool rollback_selection_verified,
    bool composition_cleanup_succeeded,
    bool has_trailing_space,
    bool from_backspace) noexcept {
    const bool rollback_verified =
        rollback_text_verified &&
        rollback_selection_verified &&
        composition_cleanup_succeeded;
    // A Backspace rollback may pass through only when it is undoing the
    // delimiter that was verified immediately after the committed word.
    return rollback_verified &&
               (!from_backspace || has_trailing_space)
        ? CommitUndoRollbackDisposition::PassThrough
        : CommitUndoRollbackDisposition::ConsumeBackspace;
}

inline bool CanConsumeCommitUndoBackspace(
    bool resumed_composition,
    bool rollback_verified,
    bool has_trailing_space,
    bool boundary_removed,
    bool native_replay_succeeded) noexcept {
    if (resumed_composition || boundary_removed || native_replay_succeeded) {
        return true;
    }
    // An exact no-boundary rollback intentionally protects the final word
    // character from the host Backspace. A post-Space rollback needs an
    // actual boundary removal or replay before the routed key is consumed.
    return !has_trailing_space && rollback_verified;
}

inline std::optional<VerifiedTextSpan> FindVerifiedTokenAtLookbehindEnd(
    std::wstring_view lookbehind,
    std::wstring_view expected_display,
    bool truncated_left,
    size_t max_token_length) {
    if (expected_display.empty() || max_token_length == 0 ||
        max_token_length == (std::numeric_limits<size_t>::max)() ||
        expected_display.length() > max_token_length ||
        lookbehind.empty() || lookbehind.length() > max_token_length + 1 ||
        !core::rules::IsWordChar(lookbehind.back())) {
        return std::nullopt;
    }

    size_t start = lookbehind.length();
    while (start > 0 && core::rules::IsWordChar(lookbehind[start - 1])) {
        --start;
    }

    if (start == 0 && truncated_left) {
        return std::nullopt;
    }

    const size_t token_length = lookbehind.length() - start;
    if (token_length == 0 || token_length > max_token_length ||
        token_length != expected_display.length() ||
        lookbehind.compare(start, token_length, expected_display) != 0) {
        return std::nullopt;
    }

    return VerifiedTextSpan{start, lookbehind.length()};
}

inline std::optional<VerifiedTextSpan> FindVerifiedTextBeforeCaret(
    std::wstring_view text,
    size_t caret,
    std::wstring_view expected_display) {
    if (expected_display.empty() || caret > text.length() || caret < expected_display.length()) {
        return std::nullopt;
    }

    const size_t start = caret - expected_display.length();
    if (text.compare(start, expected_display.length(), expected_display) != 0) {
        return std::nullopt;
    }
    return VerifiedTextSpan{start, caret};
}

inline std::optional<VerifiedTextSpan> FindVerifiedTextBeforeCaretWithOptionalTrailingSpace(
    std::wstring_view text,
    size_t caret,
    std::wstring_view expected_display) {
    if (expected_display.empty() || caret > text.length()) {
        return std::nullopt;
    }

    const size_t display_length = expected_display.length();
    if (display_length < (std::numeric_limits<size_t>::max)() &&
        caret >= display_length + 1) {
        const size_t start = caret - display_length - 1;
        if (text[start + display_length] == L' ' &&
            text.compare(start, display_length, expected_display) == 0) {
            return VerifiedTextSpan{start, caret, true};
        }
    }

    if (caret < display_length) {
        return std::nullopt;
    }

    const size_t start = caret - display_length;
    if (text.compare(start, display_length, expected_display) != 0) {
        return std::nullopt;
    }
    return VerifiedTextSpan{start, caret, false};
}

inline std::optional<VerifiedTextSpan> FindVerifiedSmartUndoTextBeforeCaret(
    std::wstring_view text,
    size_t caret,
    const CommitUndoEntry& entry) {
    if (!entry.committed_with_ascii_space ||
        !ShouldCaptureSmartUndo(entry)) {
        return std::nullopt;
    }
    auto span = FindVerifiedTextBeforeCaretWithOptionalTrailingSpace(
        text, caret, entry.display_text);
    if (!span || !span->has_trailing_space) {
        return std::nullopt;
    }
    return span;
}

inline std::optional<VerifiedTextSpan> FindVerifiedBytesBeforeCaret(
    std::string_view text,
    size_t caret,
    std::string_view expected_display) {
    if (expected_display.empty() || caret > text.length() || caret < expected_display.length()) {
        return std::nullopt;
    }

    const size_t start = caret - expected_display.length();
    if (text.compare(start, expected_display.length(), expected_display) != 0) {
        return std::nullopt;
    }
    return VerifiedTextSpan{start, caret};
}

inline std::optional<VerifiedTextSpan> FindVerifiedBytesBeforeCaretWithOptionalTrailingSpace(
    std::string_view text,
    size_t caret,
    std::string_view expected_display) {
    if (expected_display.empty() || caret > text.length()) {
        return std::nullopt;
    }

    const size_t display_length = expected_display.length();
    if (display_length < (std::numeric_limits<size_t>::max)() &&
        caret >= display_length + 1) {
        const size_t start = caret - display_length - 1;
        if (text[start + display_length] == ' ' &&
            text.compare(start, display_length, expected_display) == 0) {
            return VerifiedTextSpan{start, caret, true};
        }
    }

    if (caret < display_length) {
        return std::nullopt;
    }

    const size_t start = caret - display_length;
    if (text.compare(start, display_length, expected_display) != 0) {
        return std::nullopt;
    }
    return VerifiedTextSpan{start, caret, false};
}

inline std::optional<VerifiedTextSpan> FindVerifiedSmartUndoBytesBeforeCaret(
    std::string_view text,
    size_t caret,
    std::string_view expected_display,
    const CommitUndoEntry& entry) {
    if (!entry.committed_with_ascii_space ||
        !ShouldCaptureSmartUndo(entry)) {
        return std::nullopt;
    }
    auto span = FindVerifiedBytesBeforeCaretWithOptionalTrailingSpace(
        text, caret, expected_display);
    if (!span || !span->has_trailing_space) {
        return std::nullopt;
    }
    return span;
}

} // namespace vn_ime
