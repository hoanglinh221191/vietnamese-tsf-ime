#pragma once

#include <inputscope.h>
#include <cstddef>
#include <span>
#include <string_view>

namespace vn_ime {

inline constexpr bool IsBrowserExecutableName(
    std::wstring_view process_name) noexcept {
    return process_name.contains(L"chrome") ||
           process_name.contains(L"edge") ||
           process_name.contains(L"firefox") ||
           process_name.contains(L"brave") ||
           process_name.contains(L"opera") ||
           process_name.contains(L"vivaldi");
}

// Chromium's window class: browsers, and every app built on Chromium -
// Electron, CEF, Edge WebView2. Both Zalo builds are among them. Chromium
// draws its own composition in the page. Until its renderer has laid the text
// out, it answers GetTextExt with TS_E_NOLAYOUT or an empty rectangle, which
// is not the "nobody can say where the text is" that NoteCompositionPlacement
// looks for.
inline constexpr bool IsChromiumWindowClassName(
    std::wstring_view class_name) noexcept {
    return class_name == L"Chrome_WidgetWin_1";
}

// A surface measured unable to place a composition goes over to synthetic keys
// from the next word on. The word being composed when that was found is
// finished as a composition. Switching under it handed the next key to the
// synthetic path with a composition still open, and that path commits the
// composition and gives the key to the host as typed. In Zalo, "co" and then 1
// came out "co1", and "để" came out "d9e63". Once a digit is in the word,
// nothing after it is Vietnamese.
inline constexpr bool ShouldTypeUnplaceableSurfaceSynthetically(
    bool cannot_place_composition, bool composing) noexcept {
    return cannot_place_composition && !composing;
}

inline constexpr bool IsWebRichTextHostExecutableName(
    std::wstring_view process_name) noexcept {
    return IsBrowserExecutableName(process_name) ||
           process_name == L"codex.exe";
}

inline constexpr bool ShouldPassWebRichTextBoundaryToHost(
    bool is_web_rich_text_host,
    bool has_active_composition,
    bool is_space_key,
    bool is_backspace_key,
    bool is_valid_composition_key,
    bool is_smart_context_continuation,
    wchar_t translated_character) noexcept {
    if (!is_web_rich_text_host || !has_active_composition) {
        return false;
    }
    if (is_space_key) {
        return true;
    }
    return !is_backspace_key &&
           !is_valid_composition_key &&
           !is_smart_context_continuation &&
           translated_character >= L' ';
}

enum class BrowserTextInputMode : unsigned char {
    NativeComposition,
    UrlNativeReconversion,
};

enum class InputScopeFocusRefreshPolicy : unsigned char {
    ImmediateSyncWithLegacyFallback,
    DeferToTextKeySyncOnly,
};

inline constexpr InputScopeFocusRefreshPolicy
SelectInputScopeFocusRefreshPolicy(bool is_browser) noexcept {
    return is_browser
        ? InputScopeFocusRefreshPolicy::DeferToTextKeySyncOnly
        : InputScopeFocusRefreshPolicy::ImmediateSyncWithLegacyFallback;
}

inline constexpr bool ShouldRequestBrowserInputScopeCheck(
    bool is_browser,
    bool is_text_key,
    bool check_pending,
    bool same_context,
    bool already_attempted_for_key) noexcept {
    return is_browser && is_text_key &&
           (check_pending || !same_context) &&
           (!already_attempted_for_key || !same_context);
}

struct BrowserInputScopeCheckDecision {
    bool continue_key = true;
    bool clear_pending = false;
    bool clear_sensitive_state = false;
};

// `composing_here`: this service has a composition open in the very context the
// key is for, so the field was checked when the word began and has not changed
// under it. A refused check then leaves the word alone and asks again at the
// next key. Clearing it here wiped the engine under a composition still on
// screen and handed the key to the browser: after a native resume in Opera,
// which Chromium answers by refocusing a dozen times while still busy, "vậy",
// Space, Backspace came back as "6y5".
inline constexpr BrowserInputScopeCheckDecision
DecideBrowserInputScopeCheck(
    bool check_pending,
    bool request_succeeded,
    bool session_succeeded,
    bool action_executed,
    bool composing_here = false) noexcept {
    if (!check_pending) {
        return {};
    }
    if (request_succeeded && session_succeeded && action_executed) {
        return {true, true, false};
    }
    if (composing_here) {
        return {true, false, false};
    }
    return {false, false, true};
}

// A focus notification that leaves the focus on the document it was already
// on - the same document manager, or the one holding this service's open
// composition - changes nothing. Chromium sends a burst of these while keys
// it was sent are still arriving.
inline constexpr bool IsFocusStayingOnDocument(
    bool has_focus,
    bool same_document_manager,
    bool focus_holds_open_composition) noexcept {
    return has_focus && (same_document_manager || focus_holds_open_composition);
}

// Whether a word reopened without a composition (a passive word) goes on at
// this key: only in its own context, while it is in flight, or for the
// Backspace that reopens it after its Space. A new word or another field ends
// it, and that key takes the ordinary path.
inline constexpr bool ShouldPassiveWordContinue(
    bool same_context,
    bool word_in_flight,
    bool is_backspace,
    bool reopenable_after_space) noexcept {
    if (!same_context) {
        return false;
    }
    return word_in_flight || (is_backspace && reopenable_after_space);
}

inline constexpr bool IsPasswordBrowserInputScope(
    InputScope scope) noexcept {
    return scope == IS_PASSWORD ||
           scope == IS_NUMERIC_PASSWORD ||
           scope == IS_NUMERIC_PIN ||
           scope == IS_ALPHANUMERIC_PIN ||
           scope == IS_ALPHANUMERIC_PIN_SET;
}

inline constexpr BrowserTextInputMode SelectBrowserTextInputMode(
    bool is_browser,
    bool is_secure,
    std::span<const InputScope> scopes) noexcept {
    if (!is_browser || is_secure) {
        return BrowserTextInputMode::NativeComposition;
    }

    bool has_url_scope = false;
    for (const InputScope scope : scopes) {
        if (IsPasswordBrowserInputScope(scope)) {
            return BrowserTextInputMode::NativeComposition;
        }
        has_url_scope = has_url_scope || scope == IS_URL;
    }
    return has_url_scope
        ? BrowserTextInputMode::UrlNativeReconversion
        : BrowserTextInputMode::NativeComposition;
}

// A field for an email address, a phone number or an amount - what a sign-up
// form marks with type="email", "tel" or "number", and what Chromium passes on
// as these scopes. Nothing typed there is Vietnamese, and a mark in it is only
// ever a mistake: "hus" in an address made "hú", "tuans" made "tuấn".
inline constexpr bool IsPlainKeysInputScope(InputScope scope) noexcept {
    switch (scope) {
        case IS_EMAIL_USERNAME:
        case IS_EMAIL_SMTPEMAILADDRESS:
        case IS_TELEPHONE_FULLTELEPHONENUMBER:
        case IS_TELEPHONE_COUNTRYCODE:
        case IS_TELEPHONE_AREACODE:
        case IS_TELEPHONE_LOCALNUMBER:
        case IS_NUMBER:
        case IS_NUMBER_FULLWIDTH:
        case IS_DIGITS:
        case IS_CURRENCY_AMOUNT:
        case IS_CURRENCY_AMOUNTANDSYMBOL:
            return true;
        default:
            return false;
    }
}

// Every scope the field gives is one of those. A field that also says it
// takes ordinary text keeps Vietnamese.
inline constexpr bool InputScopesTakePlainKeys(
    std::span<const InputScope> scopes) noexcept {
    if (scopes.empty()) {
        return false;
    }
    for (const InputScope scope : scopes) {
        if (!IsPlainKeysInputScope(scope)) {
            return false;
        }
    }
    return true;
}

// Chromium hands a field that opts out of learning IS_PRIVATE and nothing
// else, whatever kind of field it is (CreateInputScope in tsf_input_scope.cc),
// so IS_URL never reaches us from one. Every incognito field is like that, and
// so is any browser-UI text box that never says either way - a views Textfield
// answers "no learning" by default, which is how Opera's address field reports.
inline constexpr bool InputScopesHideFieldType(
    std::span<const InputScope> scopes) noexcept {
    return scopes.size() == 1 && scopes[0] == IS_PRIVATE;
}

// The UI Automation class of an address bar, for when the input scope cannot
// say. Chrome, Edge, Brave and Vivaldi share Chromium's omnibox; Opera has its
// own, read off its window as an "Address field" (AddressTextfieldView) inside
// an "Address bar" (AddressBarView), both exposed as edit controls. All of them
// are views text fields, which draw their own underline under a composition
// whatever display attribute it carries - the dashed line Opera showed while
// Chrome's address bar, typed into without a composition, had none.
inline constexpr bool IsBrowserAddressBarClassName(
    std::wstring_view class_name) noexcept {
    return class_name == L"OmniboxViewViews" ||
           class_name == L"AddressTextfieldView" ||
           class_name == L"AddressBarView";
}

// What became of the real keys a word was to be rewritten with in a
// transitory store (VietnameseIME::DispatchNativeResume): Backspaces to take
// the old word out, characters to put the new one in. Counted in keystrokes,
// one per Backspace, Delete, Left or character.
enum class NativeResumeOutcome : unsigned char {
    NoPlan,       // nothing was waiting to go out
    Complete,     // every keystroke went out
    NothingSent,  // not one did: the document is exactly as it was
    Partial,      // some did: the document changed, and the rest is lost
};

inline constexpr NativeResumeOutcome ClassifyNativeResume(
    size_t planned_keystrokes, size_t sent_keystrokes) noexcept {
    if (sent_keystrokes >= planned_keystrokes) {
        return NativeResumeOutcome::Complete;
    }
    return sent_keystrokes == 0 ? NativeResumeOutcome::NothingSent
                                : NativeResumeOutcome::Partial;
}

// Whether the press of a press-and-release pair went out. SendInput counts
// events, not keystrokes, so one of two is the press - and the press is what
// deletes or types. Its release is then sent on its own.
inline constexpr bool PairPressReachedHost(unsigned events_sent) noexcept {
    return events_sent >= 1;
}

// Counts a rewrite's keystrokes as they go out. A keystroke whose press went
// counts, whole or not: the host has acted on it. A Backspace sent as one
// event of two had deleted a character, and counted as nothing sent the key
// that asked for the rewrite was typed into the page as well. The first
// keystroke that does not go out whole stops the rest.
struct NativeResumeTally {
    size_t reached = 0;

    // Whether the next keystroke may go.
    constexpr bool Note(bool pressed, bool whole) noexcept {
        if (pressed) {
            ++reached;
        }
        return whole;
    }
};

// The key that asked for the rewrite is typed the ordinary way - into a
// composition - when none of the rewrite reached the document. Eaten instead,
// it was lost with the rewrite: an Opera that refused every SendInput call
// swallowed each h typed after a committed "n", nine of them in a row, and
// each e after "nh".
//
// Not after a partial rewrite. The document has already changed under the
// key, and a Backspace handed on now would take another character with it.
inline constexpr bool ShouldTypeKeyAfterNativeResume(
    NativeResumeOutcome outcome) noexcept {
    return outcome == NativeResumeOutcome::NothingSent;
}

// SendInput delivered nothing for a call that asked for something. That is a
// host refusing injected input outright - every call in Opera on one machine,
// four days running, while Zalo and WhatsApp beside it took every one - not a
// keystroke lost to timing.
inline constexpr bool IsInjectedInputRefused(
    unsigned requested, unsigned sent) noexcept {
    return requested != 0 && sent == 0;
}

// Whether a rewrite with real keys is not even tried: a transitory store,
// where real keys are the only way a committed word is rewritten, in a host
// that has refused them. It is not asked again - the keys compose, as in any
// host, and a word already committed stays as it is. Elsewhere nothing
// changes: other hosts rewrite through the document, not through keys.
inline constexpr bool SkipsRealKeyRewrite(
    bool transitory_store, bool injected_input_refused) noexcept {
    return transitory_store && injected_input_refused;
}

// A key left to the host that ends the word before the caret: Space, a
// punctuation mark, Enter, Tab. A letter - or a VNI digit - goes on with a
// word, and a key that types nothing leaves the text as it is.
inline constexpr bool EndsWordWhenHostTypesIt(
    wchar_t translated, bool is_composition_key,
    bool is_enter_or_tab) noexcept {
    if (is_enter_or_tab) {
        return true;
    }
    return !is_composition_key && translated >= L' ' && translated != L'\x7f';
}

// How long such a key is taken to be on its way. Chromium types it in its
// renderer and only then tells the store, so a key pressed right behind it
// still finds the word at the caret. Once the key has landed there is no word
// at the caret to rewrite anyway, so waiting longer costs nothing.
inline constexpr unsigned long long kHostBoundaryInFlightMs = 500;

// Whether the word before the caret is left alone for this key: a key that
// ends it was handed to the host a moment ago, in this document, and may not
// be in the text yet. Rewritten with real keys now, the word took that key
// with it: "kh", Space and i 2 ms later read "kh" at the caret, and the two
// Backspaces meant for "kh" took the space and the h - kkhi instead of kh i.
inline constexpr bool IsHostBoundaryInFlight(
    bool boundary_handed_on, bool same_context,
    unsigned long long handed_on_tick, unsigned long long now) noexcept {
    return boundary_handed_on && same_context && now >= handed_on_tick &&
           now - handed_on_tick < kHostBoundaryInFlightMs;
}

enum class BrowserUrlKeyAction : unsigned char {
    NativeComposition,
    NativeHostKey,
    ApplyTypedReconversion,
};

inline constexpr BrowserUrlKeyAction DecideBrowserUrlKeyAction(
    BrowserTextInputMode mode,
    bool has_active_composition,
    bool is_valid_composition_key,
    bool has_transformed_candidate) noexcept {
    if (mode != BrowserTextInputMode::UrlNativeReconversion ||
        has_active_composition) {
        return BrowserUrlKeyAction::NativeComposition;
    }
    if (is_valid_composition_key && has_transformed_candidate) {
        return BrowserUrlKeyAction::ApplyTypedReconversion;
    }
    return BrowserUrlKeyAction::NativeHostKey;
}

} // namespace vn_ime
