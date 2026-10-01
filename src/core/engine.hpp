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

    // The spelling on screen is the user's own: a mark key was pressed twice
    // to give the letter back, or the word was edited with Backspace. The
    // commit must not repair it as a slip (CommitTransformRequest). Nor a
    // free-typing run of several syllables, which the repair would read as
    // one mistyped word: "minhd" committed mình.
    bool KeepsTypedSpelling() const;

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

    // Where the mark goes on oa, oe and uy with nothing after them: hoà,
    // khoẻ, thuỷ (new style, the default) or hòa, khỏe, thủy (old style).
    // The engine works in the new style throughout, because the dictionary
    // does; this decides only what is shown. See rules::ToOldStyleTonePlacement.
    void SetNewStyleTonePlacement(bool enable) noexcept {
        new_style_tone_placement_ = enable;
    }
    bool GetNewStyleTonePlacement() const noexcept {
        return new_style_tone_placement_;
    }
    // What a newly constructed Engine starts with. The text service builds
    // throwaway engines to replay keys - reconversion candidates, the address
    // bar - and every one of them has to show the style the user chose, so
    // the choice is made once for the process rather than passed to each.
    static void SetDefaultNewStyleTonePlacement(bool enable) noexcept;
    static bool DefaultNewStyleTonePlacement() noexcept;

    // UniKey's Quick Telex, off by default: a doubled consonant starting a
    // word is its two-letter onset - tt is th, nn ng, cc ch, kk kh, pp ph,
    // gg gi, qq qu. A process default as well, for the same reason as the
    // tone style: the throwaway engines have to agree with the real one.
    void SetQuickTelex(bool enable) noexcept { quick_telex_ = enable; }
    bool GetQuickTelex() const noexcept { return quick_telex_; }
    static void SetDefaultQuickTelex(bool enable) noexcept;
    static bool DefaultQuickTelex() noexcept;

    // Whether a Telex [ or ] typed now would be the ơ or ư of a word rather
    // than a bracket: only in Telex, only after an onset the word could start
    // with - "t", "nh", "tr" - and at most one ư already typed after it, for
    // "tr][ng". A bracket at the start of a word, after a vowel, or after
    // letters no word starts with - "a[i]", "arr[0]", "[link]" - is a bracket.
    bool AcceptsTelexBracket() const;

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

    // Experimental, off by default, Telex only in effect. Backspace in a word
    // shown as its keys because it is not Vietnamese takes the word back to
    // its letters, marks and mark keys gone: "buowcdk" is "buocd". VNI does
    // this with its digits whatever the setting. See BackspaceRawDisplay.
    void SetStripMarksOnBackspace(bool enable) noexcept {
        strip_marks_on_backspace_ = enable;
    }
    bool GetStripMarksOnBackspace() const noexcept {
        return strip_marks_on_backspace_;
    }

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
    bool new_style_tone_placement_ = true;
    bool quick_telex_ = false;
    bool underscore_starts_new_word_ = false;
    EnglishProtectionLevel english_protection_level_ = EnglishProtectionLevel::Balanced;
    // Everything GetDisplayResult decides, in the engine's own new-style
    // placement; GetDisplayResult applies the chosen style on the way out.
    EngineDisplayResult ComputeDisplayResult() const;
    // Whether the last key, doubled, is the way to an English word that its
    // own keys give to Vietnamese - "ass" for as, "hiss" for his - so the
    // English lists must not keep the doubled spelling instead.
    bool DoubledKeyReachesYieldedEnglish() const;
    // Smart context keeps the keys as typed (a URL, an address, code). In free
    // typing a run of capitalised syllables is a name, not camelCase.
    bool KeptBySmartContext() const;
    bool IsCapitalisedNameRun() const;
    bool smart_context_protection_enabled_ = true;
    bool suppress_auto_correct_ = false;
    bool has_escaped_ = false;
    bool raw_overflow_bypass_ = false;

    // Why the word is on screen as its keys: None when it is not, or when its
    // keys are simply its letters; KeptOnPurpose for English, a URL or code;
    // NotVietnamese when the keys would type nothing valid.
    enum class RawDisplayReason : uint8_t { None, KeptOnPurpose, NotVietnamese };
    RawDisplayReason CurrentRawDisplayReason(bool tell_mistyped_apart) const;
    bool WasShownAsVietnamese() const;
    // Set by the first Backspace on a word shown as its keys, and kept until
    // the next key is typed: while it is set the word is shown as raw_keys_.
    // Literal takes one key off at a time; BaseLetters also drops the marks.
    enum class RawBackspaceMode : uint8_t { None, Literal, BaseLetters };
    RawBackspaceMode raw_backspace_mode_ = RawBackspaceMode::None;
    bool strip_marks_on_backspace_ = false;
    bool BackspaceRawDisplay();
    bool DropVniMarkDigits();
    bool BackspaceBackToVietnamese();
    bool TakeKeysBackTo(const std::wstring& target);
    void ClearBackspaceDisplay() noexcept;
    // What a Backspace left on screen, when the keys rebuilt for it would
    // show something else - "lắm" less the m is lắ, and its rebuilt keys
    // "laws" read as English; "hoặc" less the c is hoặ, and "hoawj" puts the
    // tone on the o. Shown while raw_keys_ is still backspace_display_raw_,
    // so the next key types on from the rebuilt keys as usual.
    std::wstring backspace_display_;
    std::wstring backspace_display_raw_;

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
    mutable bool correction_cache_free_typing_ = false;
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

// The keys that were actually typed for the word an address bar is holding.
//
// The Chrome and Edge address bars do not compose. Each key reads the word back
// out of the box and writes a replacement, so without this the only record of
// what was typed is the text on screen - and once a correction has rewritten
// that text, the letters the user typed are gone. "gma" corrected to "gam" left
// "gam" to be read against the next key, i read as a slip for the VNI breve
// key turned it into a real word, and "gmail" came out as the breve-a "gaml".
// A composing host never loses the keys, which is why the same correction
// there is overturned by the next keystroke and Opera never showed it.
//
// So the keys are kept here between keystrokes, and trusted only while the box
// still holds exactly the text they were last seen to produce. Any other text -
// a click elsewhere, a suggestion accepted, a paste, a different box - finds
// nothing, and the word is read back from the screen as before.
//
// Each key records what the box should hold after it, and that expectation
// becomes the record only when the next key finds it there. Nothing has to
// know whether the key was written by Neokey or passed to the host.
//
// The text services framework asks about a claimed key twice, once to test it
// and once to act on it, and the second ask must not be read as a new key. By
// text alone it sometimes cannot be told apart: a key the word absorbs leaves
// the box exactly as it was, so "the key landed" and "the same key again" look
// identical. The caller does know, so the second ask says so and is answered
// from the first - see LastAsk.
class BrowserUrlTypedKeys {
public:
    ~BrowserUrlTypedKeys() { Clear(); }

    // The keys that produced `token`, if this word has been followed from its
    // start and the box still holds what they produced. A new key.
    std::optional<std::wstring> KeysFor(std::wstring_view token);

    // The answer KeysFor gave for the key being asked about again. Nothing is
    // promoted or forgotten.
    std::optional<std::wstring> LastAsk(std::wstring_view token) const;

    // What the box should hold once the current key has landed, and the keys
    // that will have produced it.
    void Expect(std::wstring_view text, std::wstring_view keys);

    void Clear() noexcept;

private:
    std::wstring text_;
    std::wstring keys_;
    std::wstring expected_text_;
    std::wstring expected_keys_;
    std::wstring asked_token_;
    std::optional<std::wstring> asked_keys_;
};

// Passed as `key` for Esc. Everywhere else Esc hands back the keys as typed -
// the composition keeps them - and the address bar, which has no composition,
// let the browser have it instead, so a word Telex had made Vietnamese could
// only be taken back by retyping it. With this key the candidate is the keys
// the record holds for the word, when they differ from it. Only the record:
// read back off the screen, a word gives a spelling of itself, "tene" for tên,
// not the keys anybody pressed.
inline constexpr wchar_t kBrowserUrlRestoreKeysKey = L'\x1B';

// `typed_keys`, when given, supplies the keys behind `committed_token` and is
// told what the box will hold afterwards. Without it the word is read back from
// the screen, which is exact for a word that has not been corrected yet.
// `asking_again` marks the act that follows a test of the same key: it is
// answered from that test and changes nothing. `before_token` is the character
// in front of the word, 0 when there is none or it could not be read: a word
// right after one of kAddressSeparators is left as typed.
std::optional<std::wstring> BuildBrowserUrlTypedReconversionCandidate(
    std::wstring_view committed_token,
    wchar_t key,
    InputMethod method,
    CorrectionLevel correction_level,
    EnglishProtectionLevel english_protection_level,
    bool smart_context_protection_enabled = true,
    BrowserUrlTypedKeys* typed_keys = nullptr,
    bool asking_again = false,
    wchar_t before_token = 0);

// In an address bar or a URL field, a word straight after one of these is
// the rest of an address - a domain's next part, a file's extension, a path -
// and not a word of its own: "google" and "com", "docx", "edu".
inline constexpr std::wstring_view kAddressSeparators = L".:/@?=&#";

inline constexpr bool IsAddressSeparator(wchar_t ch) noexcept {
    return ch != 0 && kAddressSeparators.find(ch) != std::wstring_view::npos;
}

// The box holds a word Neokey changed and a dot after it - "dóc." - and a
// letter or digit is typed straight after the dot. The word was the first part
// of an address, typed before the dot could say so, and its keys go back with
// the dot and the new key after them: "docs.g". `word_and_dot` is the word and
// its dot as they stand in the box; only the record knows the keys, so without
// one nothing is claimed. Nothing happens for a word Neokey left alone, or
// when a space follows the dot, as it does after a sentence.
std::optional<std::wstring> BuildBrowserUrlDottedWordCandidate(
    std::wstring_view word_and_dot,
    wchar_t key,
    BrowserUrlTypedKeys* typed_keys,
    bool asking_again = false);

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
