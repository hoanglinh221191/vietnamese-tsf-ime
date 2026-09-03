#include "fuzzy_input.hpp"

#include <array>
#include <optional>
#include <utility>

#include "fuzzy_bigrams.hpp"
#include "rules.hpp"
#include "speller.hpp"

namespace vn_ime::core {

namespace {

struct CandidateSet {
    struct Candidate {
        std::wstring value;
        FuzzyInputFlags matched_flags = 0;
    };

    std::array<Candidate, kMaxFuzzyInputCandidates> values;
    size_t size = 0;

    void Add(std::wstring candidate, FuzzyInputFlags matched_flags) {
        if (candidate.empty()) {
            return;
        }
        for (size_t index = 0; index < size; ++index) {
            if (values[index].value == candidate) {
                values[index].matched_flags |= matched_flags;
                return;
            }
        }
        if (size >= values.size()) {
            return;
        }
        values[size++] = Candidate{std::move(candidate), matched_flags};
    }

    const Candidate* Find(std::wstring_view candidate) const noexcept {
        for (size_t index = 0; index < size; ++index) {
            if (values[index].value == candidate) {
                return &values[index];
            }
        }
        return nullptr;
    }
};

std::wstring Lowercase(std::wstring_view token) {
    std::wstring lower;
    lower.reserve(token.length());
    for (wchar_t character : token) {
        lower.push_back(rules::ToLower(character));
    }
    return lower;
}

bool IsEligibleToken(std::wstring_view token) {
    if (token.empty() || token.length() > kMaxFuzzyInputTokenLength) {
        return false;
    }
    for (wchar_t character : token) {
        if (!rules::IsWordChar(character)) {
            return false;
        }
    }
    return true;
}

bool StartsWith(std::wstring_view value, std::wstring_view prefix) {
    return value.length() >= prefix.length() &&
           value.substr(0, prefix.length()) == prefix;
}

std::wstring ReplacePrefix(
    std::wstring_view value,
    size_t prefix_length,
    std::wstring_view replacement) {
    std::wstring result(replacement);
    result.append(value.substr(prefix_length));
    return result;
}

void AddInitialCandidates(
    std::wstring_view lower,
    FuzzyInputFlags flags,
    CandidateSet& candidates) {
    if (HasFuzzyInputFlag(flags, FuzzyInputFlag::LAndN)) {
        if (StartsWith(lower, L"l")) {
            candidates.Add(
                ReplacePrefix(lower, 1, L"n"),
                ToFuzzyInputFlags(FuzzyInputFlag::LAndN));
        } else if (StartsWith(lower, L"n")) {
            candidates.Add(
                ReplacePrefix(lower, 1, L"l"),
                ToFuzzyInputFlags(FuzzyInputFlag::LAndN));
        }
    }

    if (HasFuzzyInputFlag(flags, FuzzyInputFlag::TrAndCh)) {
        if (StartsWith(lower, L"tr")) {
            candidates.Add(
                ReplacePrefix(lower, 2, L"ch"),
                ToFuzzyInputFlags(FuzzyInputFlag::TrAndCh));
        } else if (StartsWith(lower, L"ch")) {
            candidates.Add(
                ReplacePrefix(lower, 2, L"tr"),
                ToFuzzyInputFlags(FuzzyInputFlag::TrAndCh));
        }
    }

    if (HasFuzzyInputFlag(flags, FuzzyInputFlag::SAndX)) {
        if (StartsWith(lower, L"s")) {
            candidates.Add(
                ReplacePrefix(lower, 1, L"x"),
                ToFuzzyInputFlags(FuzzyInputFlag::SAndX));
        } else if (StartsWith(lower, L"x")) {
            candidates.Add(
                ReplacePrefix(lower, 1, L"s"),
                ToFuzzyInputFlags(FuzzyInputFlag::SAndX));
        }
    }

    if (!HasFuzzyInputFlag(flags, FuzzyInputFlag::RAndDAndGi)) {
        return;
    }

    const FuzzyInputFlags rdgi_flag =
        ToFuzzyInputFlags(FuzzyInputFlag::RAndDAndGi);
    rules::VowelData second_vowel{};
    const bool starts_with_g_plus_i =
        lower.length() >= 2 && lower[0] == L'g' &&
        rules::GetVowelData(lower[1], second_vowel) &&
        rules::ToLower(second_vowel.raw) == L'i';

    if (starts_with_g_plus_i) {
        rules::VowelData third_vowel{};
        const bool gi_is_onset_digraph =
            lower.length() >= 3 &&
            rules::GetVowelData(lower[2], third_vowel);
        const size_t prefix_length = gi_is_onset_digraph ? 2 : 1;
        candidates.Add(
            ReplacePrefix(lower, prefix_length, L"r"), rdgi_flag);
        candidates.Add(
            ReplacePrefix(lower, prefix_length, L"d"), rdgi_flag);
    } else if (StartsWith(lower, L"r")) {
        candidates.Add(ReplacePrefix(lower, 1, L"d"), rdgi_flag);
        const std::wstring_view suffix = lower.substr(1);
        rules::VowelData first_suffix_vowel{};
        const bool suffix_starts_with_i =
            !suffix.empty() &&
            rules::GetVowelData(suffix.front(), first_suffix_vowel) &&
            rules::ToLower(first_suffix_vowel.raw) == L'i';
        candidates.Add(
            ReplacePrefix(lower, 1, suffix_starts_with_i ? L"g" : L"gi"),
            rdgi_flag);
    } else if (StartsWith(lower, L"d")) {
        candidates.Add(ReplacePrefix(lower, 1, L"r"), rdgi_flag);
        const std::wstring_view suffix = lower.substr(1);
        rules::VowelData first_suffix_vowel{};
        const bool suffix_starts_with_i =
            !suffix.empty() &&
            rules::GetVowelData(suffix.front(), first_suffix_vowel) &&
            rules::ToLower(first_suffix_vowel.raw) == L'i';
        candidates.Add(
            ReplacePrefix(lower, 1, suffix_starts_with_i ? L"g" : L"gi"),
            rdgi_flag);
    }
}

std::optional<std::wstring> BuildToneSwap(std::wstring_view lower) {
    size_t tone_index = lower.length();
    rules::VowelData marked_vowel{};
    for (size_t index = 0; index < lower.length(); ++index) {
        rules::VowelData vowel{};
        if (!rules::GetVowelData(lower[index], vowel) ||
            (vowel.tone != ToneMark::Hook &&
             vowel.tone != ToneMark::Tilde)) {
            continue;
        }
        if (tone_index != lower.length()) {
            return std::nullopt;
        }
        tone_index = index;
        marked_vowel = vowel;
    }

    if (tone_index == lower.length()) {
        return std::nullopt;
    }

    const ToneMark replacement_tone =
        marked_vowel.tone == ToneMark::Hook
            ? ToneMark::Tilde
            : ToneMark::Hook;
    std::wstring candidate(lower);
    candidate[tone_index] = rules::MakeVowel(
        marked_vowel.raw, replacement_tone, false);
    return candidate;
}

void AddToneCandidates(
    std::wstring_view lower,
    FuzzyInputFlags flags,
    CandidateSet& candidates) {
    if (!HasFuzzyInputFlag(flags, FuzzyInputFlag::HookAndTilde)) {
        return;
    }

    const auto base_tone_swap = BuildToneSwap(lower);
    if (!base_tone_swap) {
        return;
    }

    const FuzzyInputFlags tone_flag =
        ToFuzzyInputFlags(FuzzyInputFlag::HookAndTilde);
    const size_t initial_candidate_count = candidates.size;
    candidates.Add(*base_tone_swap, tone_flag);

    // Also try one enabled initial substitution plus the tone substitution.
    // The snapshot keeps the Cartesian expansion bounded and prevents chaining
    // multiple initial groups.
    for (size_t index = 0; index < initial_candidate_count; ++index) {
        const auto combined = BuildToneSwap(candidates.values[index].value);
        if (combined) {
            candidates.Add(
                *combined,
                candidates.values[index].matched_flags | tone_flag);
        }
    }
}

CandidateSet BuildBigramTokenCandidates(
    std::wstring_view lower,
    FuzzyInputFlags flags) {
    CandidateSet candidates;
    AddInitialCandidates(lower, flags, candidates);
    AddToneCandidates(lower, flags, candidates);
    candidates.Add(std::wstring(lower), 0);
    return candidates;
}

FuzzyInputDecision DecideDirectionalBigram(
    std::wstring_view previous_token,
    std::wstring_view current_token,
    std::wstring_view lower_previous,
    std::wstring_view lower_current,
    FuzzyInputFlags flags) {
    for (const auto& rule : fuzzy_input::data::kDirectionalBigramRules) {
        if (!HasFuzzyInputFlag(flags, rule.required_flag) ||
            lower_previous != rule.source_previous ||
            lower_current != rule.source_current) {
            continue;
        }

        FuzzyInputDecision decision;
        decision.original.assign(previous_token);
        decision.original.push_back(L' ');
        decision.original.append(current_token);
        decision.replacement = speller::PreserveCasing(
            previous_token, rule.target_previous);
        decision.replacement.push_back(L' ');
        decision.replacement.append(speller::PreserveCasing(
            current_token, rule.target_current));
        decision.scope = FuzzyInputScope::PreviousAndCurrent;
        decision.matched_flags = ToFuzzyInputFlags(rule.required_flag);
        return decision;
    }
    return {};
}

FuzzyInputDecision DecideCuratedBigram(
    std::wstring_view previous_token,
    std::wstring_view current_token,
    std::wstring_view lower_previous,
    std::wstring_view lower_current,
    FuzzyInputFlags flags) {
    const CandidateSet previous_candidates =
        BuildBigramTokenCandidates(lower_previous, flags);
    const CandidateSet current_candidates =
        BuildBigramTokenCandidates(lower_current, flags);

    std::wstring_view matched_previous;
    std::wstring_view matched_current;
    FuzzyInputFlags matched_flags = 0;
    size_t match_count = 0;
    for (const std::wstring_view target :
         speller::CuratedVietnameseBigrams()) {
        const size_t separator = target.find(L' ');
        if (separator == std::wstring_view::npos || separator == 0 ||
            separator + 1 >= target.length() ||
            target.find(L' ', separator + 1) != std::wstring_view::npos) {
            continue;
        }

        const std::wstring_view target_previous =
            target.substr(0, separator);
        const std::wstring_view target_current =
            target.substr(separator + 1);
        const CandidateSet::Candidate* previous_match =
            previous_candidates.Find(target_previous);
        const CandidateSet::Candidate* current_match =
            current_candidates.Find(target_current);
        if (!previous_match || !current_match) {
            continue;
        }
        const FuzzyInputFlags target_flags =
            previous_match->matched_flags | current_match->matched_flags;
        if (target_flags == 0) {
            continue;
        }
        ++match_count;
        if (match_count > 1) {
            return {};
        }
        matched_previous = target_previous;
        matched_current = target_current;
        matched_flags = target_flags;
    }

    if (match_count != 1) {
        return {};
    }

    FuzzyInputDecision decision;
    decision.original.assign(previous_token);
    decision.original.push_back(L' ');
    decision.original.append(current_token);
    decision.replacement = speller::PreserveCasing(
        previous_token, matched_previous);
    decision.replacement.push_back(L' ');
    decision.replacement.append(speller::PreserveCasing(
        current_token, matched_current));
    decision.scope = FuzzyInputScope::PreviousAndCurrent;
    decision.matched_flags = matched_flags;
    return decision;
}

} // namespace

FuzzyInputDecision DecideFuzzyInput(
    std::wstring_view previous_token,
    std::wstring_view current_token,
    FuzzyInputFlags enabled_flags) {
    const FuzzyInputFlags flags = SanitizeFuzzyInputFlags(enabled_flags);
    if (flags == 0 || !IsEligibleToken(current_token)) {
        return {};
    }

    const std::wstring lower_current = Lowercase(current_token);
    if (IsEligibleToken(previous_token)) {
        const std::wstring lower_previous = Lowercase(previous_token);
        FuzzyInputDecision bigram = DecideDirectionalBigram(
            previous_token, current_token, lower_previous, lower_current,
            flags);
        if (bigram.Changed()) {
            return bigram;
        }
        if (!speller::IsInDictionary(lower_previous) ||
            !speller::IsInDictionary(lower_current)) {
            bigram = DecideCuratedBigram(
                previous_token, current_token, lower_previous,
                lower_current, flags);
            if (bigram.Changed()) {
                return bigram;
            }
        }
    }

    // A dictionary-valid source is never rewritten from generated candidates.
    // Such ambiguous cases require an explicit directional bigram rule above.
    if (speller::IsInDictionary(lower_current)) {
        return {};
    }

    CandidateSet generated;
    AddInitialCandidates(lower_current, flags, generated);
    AddToneCandidates(lower_current, flags, generated);

    std::wstring unique_dictionary_candidate;
    FuzzyInputFlags unique_candidate_flags = 0;
    size_t dictionary_candidate_count = 0;
    for (size_t index = 0; index < generated.size; ++index) {
        if (!speller::IsInDictionary(generated.values[index].value)) {
            continue;
        }
        ++dictionary_candidate_count;
        if (dictionary_candidate_count > 1) {
            return {};
        }
        unique_dictionary_candidate = generated.values[index].value;
        unique_candidate_flags = generated.values[index].matched_flags;
    }

    if (dictionary_candidate_count != 1) {
        return {};
    }

    FuzzyInputDecision decision;
    decision.original.assign(current_token);
    decision.replacement = speller::PreserveCasing(
        current_token, unique_dictionary_candidate);
    decision.scope = FuzzyInputScope::CurrentToken;
    decision.matched_flags = unique_candidate_flags;

    return decision;
}

} // namespace vn_ime::core
