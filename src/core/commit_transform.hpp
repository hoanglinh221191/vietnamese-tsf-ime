#pragma once

#include <string>
#include <string_view>
#include <optional>
#include <limits>
#include <utility>
#include <cstdint>

#include "commit_undo.hpp"
#include "engine.hpp"
#include "fuzzy_input.hpp"
#include "rules.hpp"
#include "speller.hpp"

namespace vn_ime::core {

struct CommitTransformRequest {
    std::wstring_view raw_token;
    std::wstring_view display_token;
    InputMethod method = InputMethod::Telex;
    CorrectionLevel correction_level = CorrectionLevel::Normal;
    wchar_t delimiter = L'\0';
    bool enable_auto_word_segmentation = false;
    bool secure_input = false;
    bool shorthand_applied = false;
    bool enable_fuzzy_input = false;
    FuzzyInputFlags fuzzy_input_flags = 0;
    std::wstring_view previous_token;
    bool allow_previous_token_rewrite = false;
    std::wstring_view pre_speller_token;
};

enum class CommitRewriteScope : uint8_t {
    CurrentToken,
    PreviousAndCurrent,
};

struct CommitTransformDecision {
    std::wstring text;
    std::wstring expected_source;
    std::wstring undo_text;
    CommitRewriteScope rewrite_scope = CommitRewriteScope::CurrentToken;
    CommitUndoEntry::TransformKind transform_kind =
        CommitUndoEntry::TransformKind::None;

    bool ChangedFrom(std::wstring_view original) const noexcept {
        return text != original;
    }

    bool RequiresRewrite() const noexcept {
        return !expected_source.empty() && text != expected_source;
    }
};

struct CompositionPairRewritePlan {
    std::wstring_view source_previous;
    std::wstring_view source_current;
    std::wstring_view target_previous;
    std::wstring_view target_current;

    bool CurrentChanges() const noexcept {
        return source_current != target_current;
    }
};

inline std::optional<CompositionPairRewritePlan>
BuildCompositionPairRewritePlan(
    const CommitTransformDecision& decision,
    std::wstring_view previous_token,
    std::wstring_view current_token) noexcept {
    if (decision.rewrite_scope != CommitRewriteScope::PreviousAndCurrent ||
        previous_token.empty() || current_token.empty()) {
        return std::nullopt;
    }

    const auto split_pair = [](std::wstring_view pair)
        -> std::optional<std::pair<std::wstring_view, std::wstring_view>> {
        const size_t separator = pair.find(L' ');
        if (separator == std::wstring_view::npos || separator == 0 ||
            separator + 1 >= pair.length() ||
            pair.find(L' ', separator + 1) != std::wstring_view::npos) {
            return std::nullopt;
        }
        return std::pair{
            pair.substr(0, separator), pair.substr(separator + 1)};
    };
    const auto source = split_pair(decision.expected_source);
    const auto target = split_pair(decision.text);
    if (!source || !target || source->first != previous_token ||
        source->second != current_token ||
        source->first.length() > kMaxFuzzyInputTokenLength ||
        source->second.length() > kMaxFuzzyInputTokenLength ||
        target->first.length() > kMaxFuzzyInputTokenLength ||
        target->second.length() > kMaxFuzzyInputTokenLength ||
        source->first == target->first) {
        return std::nullopt;
    }
    return CompositionPairRewritePlan{
        source->first, source->second, target->first, target->second};
}

inline std::optional<std::wstring_view>
ExtractImmediatePreviousToken(
    std::wstring_view text_before_caret,
    std::wstring_view current_token,
    bool truncated_left = false) noexcept {
    if (current_token.empty() ||
        text_before_caret.length() <= current_token.length() ||
        !text_before_caret.ends_with(current_token)) {
        return std::nullopt;
    }

    const size_t current_start =
        text_before_caret.length() - current_token.length();
    if (current_start == 0 || text_before_caret[current_start - 1] != L' ') {
        return std::nullopt;
    }

    const size_t previous_end = current_start - 1;
    size_t previous_start = previous_end;
    while (previous_start > 0 &&
           rules::IsWordChar(text_before_caret[previous_start - 1])) {
        --previous_start;
    }
    if (previous_start == previous_end ||
        previous_end - previous_start > kMaxFuzzyInputTokenLength ||
        (truncated_left && previous_start == 0)) {
        return std::nullopt;
    }
    return text_before_caret.substr(
        previous_start, previous_end - previous_start);
}

enum class HostOwnedSpaceCommitTarget : uint8_t {
    None,
    Composition,
    DirectInline,
};

struct HostOwnedSpaceCommitPlan {
    HostOwnedSpaceCommitTarget target =
        HostOwnedSpaceCommitTarget::None;
    wchar_t host_owned_commit_delimiter = L'\0';
    wchar_t ime_insertion_character = L'\0';
    bool pass_key_to_host = true;
};

inline constexpr HostOwnedSpaceCommitPlan
DecideHostOwnedSpaceCommit(
    bool is_space_key,
    bool is_web_rich_text_host,
    bool is_word_inline_host,
    bool has_active_composition,
    bool has_word_direct_inline) noexcept {
    if (!is_space_key) {
        return {};
    }
    if (has_active_composition &&
        (is_web_rich_text_host || is_word_inline_host)) {
        return {
            HostOwnedSpaceCommitTarget::Composition, L' ', L'\0', true,
        };
    }
    if (is_word_inline_host && has_word_direct_inline) {
        return {
            HostOwnedSpaceCommitTarget::DirectInline, L'\0', L' ', false,
        };
    }
    return {};
}

inline constexpr wchar_t ResolveCommitTransformDelimiter(
    wchar_t ime_insertion_character,
    wchar_t host_owned_commit_delimiter) noexcept {
    return ime_insertion_character != L'\0'
        ? ime_insertion_character
        : host_owned_commit_delimiter;
}

struct DirectCommitRewriteSpan {
    size_t start = 0;
    size_t old_end = 0;
    size_t new_caret = 0;
};

inline std::optional<DirectCommitRewriteSpan>
ComputeDirectCommitRewriteSpan(
    size_t caret, size_t old_length, size_t new_length) noexcept {
    if (old_length == 0 || old_length > caret ||
        new_length > (std::numeric_limits<size_t>::max)() -
                         (caret - old_length)) {
        return std::nullopt;
    }
    const size_t start = caret - old_length;
    return DirectCommitRewriteSpan{
        start, caret, start + new_length,
    };
}

inline bool IsNarrowSegmentationProtectedToken(
    std::wstring_view raw_token) {
    if (ClassifySmartContextToken(raw_token) != SmartContextKind::None) {
        return true;
    }
    return speller::IsCommonEnglishWord(raw_token);
}

inline CommitTransformDecision DecideCommitTransform(
    const CommitTransformRequest& request) {
    CommitTransformDecision decision;
    decision.text.assign(request.display_token);
    decision.expected_source.assign(request.display_token);

    if (request.shorthand_applied) {
        decision.transform_kind =
            CommitUndoEntry::TransformKind::ShorthandExpansion;
        return decision;
    }
    const bool protected_token = request.raw_token.empty() ||
        request.display_token.empty() ||
        IsNarrowSegmentationProtectedToken(request.raw_token);
    // Fuzzy Input evaluates the VNI/Telex-normalized surface before the
    // spelling engine. The host source remains display_token because that is
    // the exact text that must be verified before any replacement.
    if (request.enable_fuzzy_input && request.delimiter == L' ' &&
        !request.secure_input && !protected_token &&
        SanitizeFuzzyInputFlags(request.fuzzy_input_flags) != 0) {
        const std::wstring_view fuzzy_source =
            request.pre_speller_token.empty()
                ? request.display_token
                : request.pre_speller_token;
        FuzzyInputDecision fuzzy = DecideFuzzyInput(
            request.previous_token, fuzzy_source,
            request.fuzzy_input_flags);
        if (fuzzy.Changed()) {
            if (fuzzy.scope == FuzzyInputScope::CurrentToken) {
                if (fuzzy.replacement != request.display_token) {
                    decision.text = std::move(fuzzy.replacement);
                    decision.undo_text = std::move(fuzzy.original);
                    decision.transform_kind =
                        CommitUndoEntry::TransformKind::FuzzyInput;
                    return decision;
                }
            } else {
                const size_t replacement_separator =
                    fuzzy.replacement.find(L' ');
                const bool exactly_two_replacement_tokens =
                    replacement_separator != std::wstring::npos &&
                    replacement_separator > 0 &&
                    replacement_separator + 1 <
                        fuzzy.replacement.length() &&
                    fuzzy.replacement.find(
                        L' ', replacement_separator + 1) ==
                        std::wstring::npos;
                if (exactly_two_replacement_tokens) {
                    const std::wstring_view replacement_previous(
                        fuzzy.replacement.data(), replacement_separator);
                    const std::wstring_view replacement_current(
                        fuzzy.replacement.data() + replacement_separator + 1,
                        fuzzy.replacement.length() -
                            replacement_separator - 1);
                    const bool previous_changed =
                        replacement_previous != request.previous_token;
                    const bool current_changed =
                        replacement_current != request.display_token;

                    if (!previous_changed && current_changed) {
                        decision.text.assign(replacement_current);
                        decision.undo_text.assign(fuzzy_source);
                        decision.transform_kind =
                            CommitUndoEntry::TransformKind::FuzzyInput;
                        return decision;
                    }
                    if (request.allow_previous_token_rewrite &&
                        previous_changed) {
                        decision.expected_source.assign(
                            request.previous_token);
                        decision.expected_source.push_back(L' ');
                        decision.expected_source.append(
                            request.display_token);
                        decision.text = std::move(fuzzy.replacement);
                        decision.undo_text = std::move(fuzzy.original);
                        decision.rewrite_scope =
                            CommitRewriteScope::PreviousAndCurrent;
                        decision.transform_kind =
                            CommitUndoEntry::TransformKind::FuzzyInput;
                        return decision;
                    }
                }
            }
        }
    }

    if (request.enable_auto_word_segmentation &&
        request.delimiter == L' ' && !request.secure_input &&
        request.correction_level == CorrectionLevel::Experimental &&
        !protected_token) {
        auto candidate = speller::BuildAutoWordSegmentationCandidate(
            request.raw_token, request.display_token, request.method,
            request.correction_level);
        if (candidate && candidate->high_confidence &&
            candidate->text != request.display_token) {
            decision.text = std::move(candidate->text);
            decision.transform_kind =
                CommitUndoEntry::TransformKind::WordSegmentation;
            return decision;
        }
    }

    return decision;
}

} // namespace vn_ime::core
