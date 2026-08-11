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

inline constexpr BrowserInputScopeCheckDecision
DecideBrowserInputScopeCheck(
    bool check_pending,
    bool request_succeeded,
    bool session_succeeded,
    bool action_executed) noexcept {
    if (!check_pending) {
        return {};
    }
    if (request_succeeded && session_succeeded && action_executed) {
        return {true, true, false};
    }
    return {false, false, true};
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
