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
