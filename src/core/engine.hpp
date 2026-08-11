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
SmartContextKind ClassifySmartContextToken(
    std::wstring_view raw_keys) noexcept;
bool ShouldContinueSmartContextToken(
    std::wstring_view raw_keys,
    wchar_t next_char) noexcept;

class Engine {
public:
    explicit Engine(InputMethod method = InputMethod::Telex);

    // Process a new character. Returns true if the key is part of the composition.
    bool ProcessKey(wchar_t ch);

    // Handles backspace. Returns true if a character was removed.
    bool Backspace();
    bool BackspaceDisplayChar();

    // Clears the buffer (commits or discards the current word).
    void Clear();
    void SecureClear();

    // Returns the current string to display on the screen
    std::wstring GetDisplayString() const;
    EngineDisplayResult GetDisplayResult() const;

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
    InputMethod method_;
    std::wstring raw_keys_;
    std::wstring processed_word_;
    CorrectionLevel correction_level_ = CorrectionLevel::Normal;
    EnglishProtectionLevel english_protection_level_ = EnglishProtectionLevel::Balanced;
    bool smart_context_protection_enabled_ = true;
    bool suppress_auto_correct_ = false;
    bool has_escaped_ = false;
    bool raw_overflow_bypass_ = false;
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

} // namespace vn_ime::core
