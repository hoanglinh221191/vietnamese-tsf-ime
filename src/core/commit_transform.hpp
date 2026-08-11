#pragma once

#include <string>
#include <string_view>
#include <optional>
#include <limits>
#include <utility>
#include <cstdint>

#include "commit_undo.hpp"
#include "engine.hpp"
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
};

struct CommitTransformDecision {
    std::wstring text;
    CommitUndoEntry::TransformKind transform_kind =
        CommitUndoEntry::TransformKind::None;

    bool ChangedFrom(std::wstring_view original) const noexcept {
        return text != original;
    }
};

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
            HostOwnedSpaceCommitTarget::DirectInline, L' ', L'\0', true,
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

    if (request.shorthand_applied) {
        decision.transform_kind =
            CommitUndoEntry::TransformKind::ShorthandExpansion;
        return decision;
    }
    if (!request.enable_auto_word_segmentation ||
        request.delimiter != L' ' || request.secure_input ||
        request.correction_level != CorrectionLevel::Experimental ||
        request.raw_token.empty() || request.display_token.empty() ||
        IsNarrowSegmentationProtectedToken(
            request.raw_token)) {
        return decision;
    }

    auto candidate = speller::BuildAutoWordSegmentationCandidate(
        request.raw_token, request.display_token, request.method,
        request.correction_level);
    if (!candidate || !candidate->high_confidence ||
        candidate->text == request.display_token) {
        return decision;
    }

    decision.text = std::move(candidate->text);
    decision.transform_kind =
        CommitUndoEntry::TransformKind::WordSegmentation;
    return decision;
}

} // namespace vn_ime::core
