#pragma once

#include <string_view>

namespace vn_ime::password_context {

constexpr wchar_t FoldAsciiCase(wchar_t ch) noexcept {
    return ch >= L'A' && ch <= L'Z' ? static_cast<wchar_t>(ch + (L'a' - L'A')) : ch;
}

inline bool EqualsIgnoreCase(
    std::wstring_view left,
    std::wstring_view right) noexcept {
    if (left.size() != right.size()) {
        return false;
    }
    for (size_t index = 0; index < left.size(); ++index) {
        if (FoldAsciiCase(left[index]) != FoldAsciiCase(right[index])) {
            return false;
        }
    }
    return true;
}

inline bool SupportsPasswordCharacterMessage(
    std::wstring_view class_name) noexcept {
    return EqualsIgnoreCase(class_name, L"Edit") ||
           EqualsIgnoreCase(class_name, L"RichEdit") ||
           EqualsIgnoreCase(class_name, L"RichEdit20A") ||
           EqualsIgnoreCase(class_name, L"RichEdit20W") ||
           EqualsIgnoreCase(class_name, L"RichEdit50A") ||
           EqualsIgnoreCase(class_name, L"RichEdit50W") ||
           EqualsIgnoreCase(class_name, L"RichEdit60A") ||
           EqualsIgnoreCase(class_name, L"RichEdit60W");
}

struct SecureInputDecisionInput {
    bool secure_desktop = false;
    bool password_input_scope = false;
    bool has_window = false;
    bool class_name_available = false;
    bool password_message_control = false;
    bool password_style = false;
    bool password_query_succeeded = false;
    unsigned long long password_character = 0;
};

inline bool IsSecureInputContext(
    const SecureInputDecisionInput& input) noexcept {
    if (input.secure_desktop || input.password_input_scope) {
        return true;
    }
    if (!input.has_window || !input.class_name_available) {
        return true;
    }
    if (!input.password_message_control) {
        return false;
    }
    if (input.password_style || !input.password_query_succeeded) {
        return true;
    }
    return input.password_character != 0;
}

} // namespace vn_ime::password_context
