#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace vn_ime::core {

enum class FuzzyInputFlag : uint32_t {
    None = 0,
    LAndN = 1u << 0,
    TrAndCh = 1u << 1,
    SAndX = 1u << 2,
    RAndDAndGi = 1u << 3,
    HookAndTilde = 1u << 4,
};

using FuzzyInputFlags = uint32_t;

inline constexpr FuzzyInputFlags ToFuzzyInputFlags(
    FuzzyInputFlag flag) noexcept {
    return static_cast<FuzzyInputFlags>(flag);
}

inline constexpr FuzzyInputFlags kAllFuzzyInputFlags =
    ToFuzzyInputFlags(FuzzyInputFlag::LAndN) |
    ToFuzzyInputFlags(FuzzyInputFlag::TrAndCh) |
    ToFuzzyInputFlags(FuzzyInputFlag::SAndX) |
    ToFuzzyInputFlags(FuzzyInputFlag::RAndDAndGi) |
    ToFuzzyInputFlags(FuzzyInputFlag::HookAndTilde);

inline constexpr FuzzyInputFlags SanitizeFuzzyInputFlags(
    FuzzyInputFlags flags) noexcept {
    return flags & kAllFuzzyInputFlags;
}

inline constexpr bool HasFuzzyInputFlag(
    FuzzyInputFlags flags, FuzzyInputFlag flag) noexcept {
    return (SanitizeFuzzyInputFlags(flags) & ToFuzzyInputFlags(flag)) != 0;
}

inline constexpr size_t kMaxFuzzyInputTokenLength = 32;
inline constexpr size_t kMaxFuzzyInputCandidates = 8;

enum class FuzzyInputScope : uint8_t {
    None,
    CurrentToken,
    PreviousAndCurrent,
};

struct FuzzyInputDecision {
    std::wstring original;
    std::wstring replacement;
    FuzzyInputScope scope = FuzzyInputScope::None;
    FuzzyInputFlags matched_flags = 0;

    bool Changed() const noexcept {
        return scope != FuzzyInputScope::None && replacement != original;
    }
};

// Decides a commit-time fuzzy rewrite from already-processed Unicode text.
// It is intentionally independent of Telex/VNI raw keys and host state.
//
// Reviewed directional exceptions are checked first. The shared Vietnamese
// bigram library can then infer a target only when at least one source token is
// dictionary-invalid. Otherwise only the current token is eligible, and only
// when exactly one enabled fuzzy candidate is present in the dictionary.
FuzzyInputDecision DecideFuzzyInput(
    std::wstring_view previous_token,
    std::wstring_view current_token,
    FuzzyInputFlags enabled_flags);

inline FuzzyInputDecision DecideFuzzyInput(
    std::wstring_view current_token,
    FuzzyInputFlags enabled_flags) {
    return DecideFuzzyInput({}, current_token, enabled_flags);
}

} // namespace vn_ime::core
