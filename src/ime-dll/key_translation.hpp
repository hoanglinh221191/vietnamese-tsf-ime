#pragma once

#include <windows.h>
#include <array>
#include <utility>

namespace vn_ime {

inline constexpr LANGID kVietnameseLanguageId = 0x042a;

// A TSF profile can keep its Vietnamese language identity while using the
// physical US keyboard layout that Telex/VNI expect.  HKL values encode the
// physical layout in the high word and the input language in the low word;
// RegisterProfile stores this standard US layout handle as its substitute.
inline constexpr ULONG_PTR kUsKeyboardLayoutHandleValue = 0x04090409u;

inline HKL UsKeyboardLayoutHandle() noexcept {
    return reinterpret_cast<HKL>(kUsKeyboardLayoutHandleValue);
}

inline constexpr WORD kUsKeyboardLayoutId = 0x0409;

// Detects whether a keyboard layout handle points to the legacy Vietnamese
// TCVN 6064 layout (0x042a / KBDVNTC.DLL), which remaps the number row to
// ă â ê ô and the tone dead keys.
//
// An HKL carries the physical layout in its high word and the input language in
// its low word, and the two are independent: "English (US)" paired with the
// Vietnamese keyboard is a real Windows configuration and produces 0x042a0409,
// where the language half is US but the keys still type Vietnamese. Testing the
// language half alone let that one through, so the layout half is checked first.
//
// Neokey's own profile is language 0x042a with the US layout substitute 0x0409
// (0x0409042a) and must stay non-legacy.
inline bool IsLegacyVietnameseLayout(HKL layout) noexcept {
    const ULONG_PTR val = reinterpret_cast<ULONG_PTR>(layout);
    const WORD langId = static_cast<WORD>(val & 0xFFFF);
    const WORD layoutId = static_cast<WORD>((val >> 16) & 0xFFFF);
    if (layoutId == kVietnameseLanguageId) {
        return true;
    }
    return langId == kVietnameseLanguageId && layoutId != kUsKeyboardLayoutId;
}

// Sanitizes the keyboard layout so that the legacy TCVN 6064 layout is replaced
// with the standard US keyboard layout handle (0x04090409). This ensures that
// ToUnicodeEx produces standard digits (1..0) and symbols (!@#$%^&*() etc.).
inline HKL SanitizeKeyboardLayoutForInputMethod(HKL layout) noexcept {
    return IsLegacyVietnameseLayout(layout) ? UsKeyboardLayoutHandle() : layout;
}

// Supported by Windows 10 version 1607 and later. Without this flag,
// ToUnicodeEx can mutate the kernel dead-key buffer during OnTestKeyDown and
// change what the later OnKeyDown call observes.
inline constexpr UINT kToUnicodeDoNotChangeKeyboardState = 0x0004;

inline bool IsModifierDown(const BYTE* keyboard_state) noexcept {
    if (!keyboard_state) {
        return true;
    }
    return ((keyboard_state[VK_SHIFT] |
             keyboard_state[VK_LSHIFT] |
             keyboard_state[VK_RSHIFT] |
             keyboard_state[VK_CONTROL] |
             keyboard_state[VK_LCONTROL] |
             keyboard_state[VK_RCONTROL] |
             keyboard_state[VK_MENU] |
             keyboard_state[VK_LMENU] |
             keyboard_state[VK_RMENU] |
             keyboard_state[VK_LWIN] |
             keyboard_state[VK_RWIN]) &
            0x80) != 0;
}

inline wchar_t TranslateUnmodifiedVniNumberRowKey(
    UINT virtual_key,
    const BYTE* keyboard_state) noexcept {
    if (virtual_key < static_cast<UINT>('0') ||
        virtual_key > static_cast<UINT>('9') ||
        IsModifierDown(keyboard_state)) {
        return L'\0';
    }
    return static_cast<wchar_t>(virtual_key);
}

template <typename TranslateFn>
wchar_t TranslateVirtualKeyWithoutStateMutation(
    UINT virtual_key,
    UINT scan_code,
    const BYTE* keyboard_state,
    HKL keyboard_layout,
    bool num_lock_on,
    TranslateFn&& translate) {
    std::array<wchar_t, 4> buffer{};
    const int count = std::forward<TranslateFn>(translate)(
        virtual_key,
        scan_code,
        keyboard_state,
        buffer.data(),
        static_cast<int>(buffer.size()),
        kToUnicodeDoNotChangeKeyboardState,
        keyboard_layout);
    if (count > 0) {
        return buffer[0];
    }
    if (virtual_key >= VK_NUMPAD0 && virtual_key <= VK_NUMPAD9 &&
        num_lock_on) {
        return static_cast<wchar_t>(
            L'0' + (virtual_key - VK_NUMPAD0));
    }
    return L'\0';
}

template <typename TranslateFn>
wchar_t TranslateVirtualKeyForInputMethod(
    UINT virtual_key,
    UINT scan_code,
    const BYTE* keyboard_state,
    HKL keyboard_layout,
    bool num_lock_on,
    bool preserve_vni_number_row,
    TranslateFn&& translate) {
    if (preserve_vni_number_row) {
        const wchar_t digit = TranslateUnmodifiedVniNumberRowKey(
            virtual_key, keyboard_state);
        if (digit != L'\0') {
            return digit;
        }
    }

    return TranslateVirtualKeyWithoutStateMutation(
        virtual_key,
        scan_code,
        keyboard_state,
        keyboard_layout,
        num_lock_on,
        std::forward<TranslateFn>(translate));
}

}  // namespace vn_ime
