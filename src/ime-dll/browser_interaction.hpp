#pragma once

#include <inputscope.h>
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
