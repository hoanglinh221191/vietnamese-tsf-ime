#pragma once
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include "speller.hpp"
#include "types.hpp"

namespace vn_ime::core {

inline constexpr size_t kMaxRawKeysPerComposition = 128;

namespace rules {
struct ReconversionSpan;
}

struct ReconversionEdit {
    size_t start = 0;
    size_t end = 0;
    size_t selection_start = 0;
    size_t selection_end = 0;
    std::wstring replacement;
};

struct ReconversionCandidate {
    size_t selection_start = 0;
    size_t selection_end = 0;
    std::wstring replacement;
};

struct EngineDisplayResult {
    std::wstring text;
    speller::CorrectionKind correction_kind = speller::CorrectionKind::None;
    int correction_score = 0;
    bool correction_changed = false;
    bool correction_high_confidence = false;

    bool HasSpellerCorrection() const noexcept {
        return correction_changed &&
               correction_kind != speller::CorrectionKind::None;
    }
};

enum class ExcelFormulaInputKind {
    NotFormula,
    FormulaSyntax,
    QuotedText,
    Unknown,
};

enum class ExcelFormulaSessionState {
    Idle,
    PendingFormulaStart,
    FormulaSyntax,
    QuotedText,
};

enum class SmartContextKind : uint8_t {
    None,
    Email,
    Url,
    Code,
};

// Smart context protection is intentionally narrow: explicit URL/email
// markers, identifier underscores, an internal lower-to-upper transition, or
// a known code-family prefix followed by digits. It never treats arbitrary
// letter+digit text as code.
// underscore_starts_new_word drops the identifier-underscore rule only, for
// typists whose underscores separate words rather than name a variable. It
// defaults to the protective reading so every existing caller is unchanged.
SmartContextKind ClassifySmartContextToken(
    std::wstring_view raw_keys,
    bool underscore_starts_new_word = false) noexcept;
bool ShouldContinueSmartContextToken(
    std::wstring_view raw_keys,
    wchar_t next_char,
    bool underscore_starts_new_word = false) noexcept;

class Engine {
public:
    explicit Engine(InputMethod method = InputMethod::Telex);

    // Process a new character. Returns true if the key is part of the composition.
    bool ProcessKey(wchar_t ch);

    // How long before this keystroke the previous one arrived. Two letters can
    // reach the operating system faster than a person can deliberately order
    // them - a USB keyboard reports everything pressed within one polling
    // interval in a single report, and Windows expands that report in scan
    // order, not press order - so "th" typed as one roll can arrive as "ht".
    // Measured over one session: keys the user meant in that order were
    // 18-135ms apart (median 51), while every transposed pair was 0-20ms.
    // ProcessKey uses this to repair such a pair; see kRolledOnsetWindowMs.
    // Left unknown, no repair ever happens, so callers that do not measure
    // keystroke timing keep the old behaviour exactly.
    static constexpr unsigned kUnknownKeyInterval = 0xFFFFFFFFu;
    void SetLastKeyIntervalMs(unsigned ms) noexcept {
        last_key_interval_ms_ = ms;
    }

    // Handles backspace. Returns true if a character was removed.
    bool Backspace();
    bool BackspaceDisplayChar();

    // Clears the buffer (commits or discards the current word).
    void Clear();
    void SecureClear();

    // Returns the current string to display on the screen
    std::wstring GetDisplayString() const;
    EngineDisplayResult GetDisplayResult() const;
    // Returns the VNI/Telex-normalized surface before spelling correction,
    // while preserving the same URL/code and bilingual-protection gates.
    std::wstring GetPreCorrectionDisplayString() const;

    // Returns the raw keystroke sequence
    std::wstring GetRawString() const;
    bool HasPendingRaw() const noexcept { return !raw_keys_.empty(); }

    // Sets the active input method
    void SetInputMethod(InputMethod method);

    // Returns the active input method
    InputMethod GetInputMethod() const { return method_; }

    // Sets whether auto-correction (speller) is enabled
    void SetAutoCorrect(bool enable);

    // Gets whether auto-correction (speller) is enabled
    bool GetAutoCorrect() const { return correction_level_ != CorrectionLevel::Off; }

    // Sets the correction level used by the speller.
    void SetCorrectionLevel(CorrectionLevel level) noexcept;

    // Free typing: for text that is not prose - file names built from customer
    // names, where syllables run together with no space to separate them. It
    // gives up Telex's late modifier placement so that a following syllable's
    // letter cannot rewrite an earlier one, and stops the display falling back
    // to raw keys just because the result is not a valid Vietnamese syllable.
    void SetFreeTyping(bool enable) noexcept { free_typing_ = enable; }
    bool GetFreeTyping() const noexcept { return free_typing_; }

    // Underscores separate words rather than name a variable, so
    // "nguyeenx_hoafng_linh" becomes three syllables instead of one protected
    // code token. Independent of free typing: it belongs to ordinary typing,
    // where each syllable still gets correction and tones.
    void SetUnderscoreAsSeparator(bool enable) noexcept {
        underscore_starts_new_word_ = enable;
    }
    bool GetUnderscoreAsSeparator() const noexcept {
        return underscore_starts_new_word_;
    }
    CorrectionLevel GetCorrectionLevel() const noexcept { return correction_level_; }

    // Legacy bool API maps enabled protection to the default Balanced policy.
    void SetEnglishProtection(bool enable) noexcept {
        english_protection_level_ = enable
            ? EnglishProtectionLevel::Balanced
            : EnglishProtectionLevel::Off;
    }
    bool GetEnglishProtection() const noexcept {
        return english_protection_level_ != EnglishProtectionLevel::Off;
    }
    void SetEnglishProtectionLevel(EnglishProtectionLevel level) noexcept;
    EnglishProtectionLevel GetEnglishProtectionLevel() const noexcept {
        return english_protection_level_;
    }
    void SetSmartContextProtection(bool enable) noexcept {
        smart_context_protection_enabled_ = enable;
    }
    bool GetSmartContextProtection() const noexcept {
        return smart_context_protection_enabled_;
    }
    bool ShouldContinueSmartContext(wchar_t next_char) const noexcept;

    // Synchronize current key casing based on host-level Auto-Correct updates
    // (for example, MS Word capitalising the first letter of a list item).
    // Returns true only when the host text is the same display text modulo case
    // and the engine can reproduce the host casing exactly.
    bool UpdateCasingFromHost(std::wstring_view host_text);

private:
    // A transposition is only repaired when the two letters arrived closer
    // together than this. Above it, the order is taken as deliberate.
    static constexpr unsigned kRolledOnsetWindowMs = 25;
    // True when raw_keys_ holds exactly two letters that are not a Vietnamese
    // onset, the reverse pair is one, and they arrived within that window - so
    // appending `ch` should type the swapped pair instead. Deliberately also
    // requires `ch` to be a vowel: that is what makes the word Vietnamese-
    // shaped, and it leaves strings like "html" or "htaccess" alone.
    bool ShouldRepairRolledOnset(wchar_t ch) const noexcept;

    InputMethod method_;
    std::wstring raw_keys_;
    unsigned last_key_interval_ms_ = kUnknownKeyInterval;
    // The interval reported when raw_keys_ grew to its second character.
    unsigned onset_pair_interval_ms_ = kUnknownKeyInterval;
    std::wstring processed_word_;
    CorrectionLevel correction_level_ = CorrectionLevel::Normal;
    bool free_typing_ = false;
    bool underscore_starts_new_word_ = false;
    EnglishProtectionLevel english_protection_level_ = EnglishProtectionLevel::Balanced;
    bool smart_context_protection_enabled_ = true;
    bool suppress_auto_correct_ = false;
    bool has_escaped_ = false;
    bool raw_overflow_bypass_ = false;

    // GetDisplayResult() is const and runs the whole speller, and the TSF layer
    // calls it several times for one keystroke - OnEndEdit, then again on each
    // commit path - always on the same buffer. Cache the last speller result
    // against the inputs that produced it so the repeats cost a string compare
    // instead of a dictionary scan. Wiped with the buffer in SecureClear().
    const speller::CorrectionResult& CachedCorrection() const;
    void ClearCorrectionCache() noexcept;
    mutable speller::CorrectionResult correction_cache_result_;
    mutable std::wstring correction_cache_word_;
    mutable std::wstring correction_cache_raw_;
    mutable CorrectionLevel correction_cache_level_ = CorrectionLevel::Off;
    mutable InputMethod correction_cache_method_ = InputMethod::Telex;
    mutable EnglishProtectionLevel correction_cache_protection_ =
        EnglishProtectionLevel::Off;
    mutable bool correction_cache_valid_ = false;
};

std::optional<std::wstring> BuildReconversionCandidate(
    std::wstring_view committed_word,
    wchar_t key,
    InputMethod method);

std::optional<ReconversionCandidate> BuildReconversionCandidateWithSelection(
    std::wstring_view committed_word,
    size_t selection_start,
    size_t selection_end,
    wchar_t key,
    InputMethod method);

bool ShouldAttemptTypedReconversion(
    const rules::ReconversionSpan& span,
    wchar_t key,
    InputMethod method) noexcept;

std::optional<ReconversionEdit> BuildReconversionEdit(
    std::wstring_view text,
    size_t selection_start,
    size_t selection_end,
    wchar_t key,
    InputMethod method,
    bool truncated_left = false,
    bool truncated_right = false);

std::optional<std::wstring> BuildBrowserUrlTypedReconversionCandidate(
    std::wstring_view committed_token,
    wchar_t key,
    InputMethod method,
    CorrectionLevel correction_level,
    EnglishProtectionLevel english_protection_level,
    bool smart_context_protection_enabled = true);

ExcelFormulaInputKind ClassifyExcelFormulaPrefix(
    std::wstring_view prefix,
    bool truncated = false);

ExcelFormulaSessionState AdvanceExcelFormulaSessionState(
    ExcelFormulaSessionState state,
    wchar_t observed_char,
    bool reset = false) noexcept;

ExcelFormulaSessionState AdoptPendingExcelFormulaSession(
    ExcelFormulaSessionState state) noexcept;

ExcelFormulaSessionState MergeExcelFormulaSessionProbe(
    ExcelFormulaSessionState state,
    ExcelFormulaInputKind probe) noexcept;

bool ShouldStartExcelFormulaAtEntry(
    bool local_start_eligible) noexcept;

// Whether '=' opens a formula depends on the caret sitting at the start of the
// cell, and Excel will not answer that question: its TSF document is a
// transitory window onto the cell editor that reads back empty however much
// text the cell holds - a range cannot even be shifted back over it. The count
// has to come from the keys this service itself sent.
//
// Counted in displayed characters rather than keystrokes, because that is what
// Backspace removes: "Do65c" is five keys and three characters, and three
// Backspaces are what empty the cell again.
size_t AdvanceExcelCellChars(
    size_t committed_chars,
    size_t composing_chars,
    bool is_backspace,
    bool produces_character,
    bool is_composition_key) noexcept;

bool IsExcelCaretAtCellStart(
    size_t committed_chars,
    size_t composing_chars) noexcept;

// Whether Excel should be left to type the first character of a cell itself.
//
// Excel opens its in-cell editor on that first character and moves the TSF
// focus to it, and the character travels into the editor on Excel's own
// schedule - not on any message this service can see. Anything sent to correct
// it therefore lands either side of that transfer at random: too early and the
// character arrives afterwards and is doubled ("ggo"), too late and the
// correction eats it. Letting the host have the keystroke removes the transfer
// entirely - Excel inserts the character the ordinary way - and the second key
// can then take the word over, because both keys and everything sent between
// them travel the same message queue in order.
bool ShouldExcelHostTypeFirstChar(
    bool is_composition_key,
    bool has_composition,
    bool in_formula_session,
    bool has_native_prefix,
    bool cell_editor_open,
    size_t committed_chars) noexcept;

bool ShouldReenterExcelQuotedTextOnBackspace(
    bool has_closed_quote,
    size_t formula_chars_after_closed_quote) noexcept;

} // namespace vn_ime::core
