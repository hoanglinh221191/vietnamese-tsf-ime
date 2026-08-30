#pragma once

#include <windows.h>
#include <array>
#include <utility>

namespace vn_ime {

// Supported by Windows 10 version 1607 and later. Without this flag,
// ToUnicodeEx can mutate the kernel dead-key buffer during OnTestKeyDown and
// change what the later OnKeyDown call observes.
inline constexpr UINT kToUnicodeDoNotChangeKeyboardState = 0x0004;

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

}  // namespace vn_ime
