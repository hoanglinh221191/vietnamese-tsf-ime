#pragma once

#include <windows.h>

#include <iterator>
#include <string_view>

namespace vn_ime {

// The physical keyboards Neokey can be registered over.
//
// Neokey's profile is filed under Vietnamese with a keyboard layout
// substituted for the legacy Vietnamese one (KBDVNTC), which would otherwise
// turn the number row into ă â ê ô before Neokey saw a key. That substitute
// used to be the US layout, always, which left anyone with another keyboard
// typing on QWERTY positions: on a French keyboard the key marked A typed q.
// So the substitute is a choice, made from this list. Each entry is a layout
// Windows ships, with its KLID as the Keyboard Layouts key names it.
struct KeyboardLayoutChoice {
    WORD id;
    std::wstring_view klid;
    std::wstring_view name_vi;
    std::wstring_view name_en;
};

inline constexpr KeyboardLayoutChoice kKeyboardLayoutChoices[] = {
    {0x0409, L"00000409", L"Mỹ (QWERTY)", L"US (QWERTY)"},
    {0x0809, L"00000809", L"Anh (QWERTY)", L"United Kingdom (QWERTY)"},
    {0x040C, L"0000040c", L"Pháp (AZERTY)", L"French (AZERTY)"},
    {0x080C, L"0000080c", L"Bỉ - Pháp (AZERTY)", L"Belgian French (AZERTY)"},
    {0x100C, L"0000100c", L"Thụy Sĩ - Pháp (QWERTZ)", L"Swiss French (QWERTZ)"},
    {0x0407, L"00000407", L"Đức (QWERTZ)", L"German (QWERTZ)"},
    {0x0807, L"00000807", L"Thụy Sĩ - Đức (QWERTZ)", L"Swiss German (QWERTZ)"},
};

inline constexpr WORD kDefaultKeyboardLayoutId = 0x0409;

// The value under Software\Neokey. Per user (HKCU) for what the settings
// window shows and what this user's input is switched to; the machine copy
// (HKLM, 64-bit view) is what registration reads, since the profile it
// registers is the machine's.
inline constexpr wchar_t kKeyboardLayoutValueName[] = L"KeyboardLayout";

inline const KeyboardLayoutChoice* FindKeyboardLayoutChoice(DWORD id) noexcept {
    for (const KeyboardLayoutChoice& choice : kKeyboardLayoutChoices) {
        if (choice.id == id) {
            return &choice;
        }
    }
    return nullptr;
}

inline bool IsSupportedKeyboardLayoutId(DWORD id) noexcept {
    return FindKeyboardLayoutChoice(id) != nullptr;
}

// Anything that is not on the list is the default: a value written by hand or
// by a later version must not register a layout nobody has tested.
inline WORD SanitizeKeyboardLayoutId(DWORD id) noexcept {
    return IsSupportedKeyboardLayoutId(id) ? static_cast<WORD>(id)
                                           : kDefaultKeyboardLayoutId;
}

// The layout itself, as an HKL whose two halves are both its id - what
// RegisterProfile takes as the substitute (0x04090409 for US).
inline HKL KeyboardLayoutHandle(WORD id) noexcept {
    const ULONG_PTR value =
        (static_cast<ULONG_PTR>(id) << 16) | static_cast<ULONG_PTR>(id);
    return reinterpret_cast<HKL>(value);
}

// Neokey's own input: Vietnamese language over the chosen layout
// (0x0409042a for US).
inline HKL NeokeyInputHandle(WORD id) noexcept {
    const ULONG_PTR value =
        (static_cast<ULONG_PTR>(id) << 16) | 0x042aU;
    return reinterpret_cast<HKL>(value);
}

// The stored choice, sanitized; false when there is none.
inline bool TryReadKeyboardLayoutId(HKEY root, const wchar_t* key_path,
                                    WORD& id, DWORD extra_flags = 0) noexcept {
    DWORD value = 0;
    DWORD size = sizeof(value);
    const LSTATUS status = ::RegGetValueW(
        root, key_path, kKeyboardLayoutValueName, RRF_RT_REG_DWORD | extra_flags,
        nullptr, &value, &size);
    if (status != ERROR_SUCCESS) {
        return false;
    }
    id = SanitizeKeyboardLayoutId(value);
    return true;
}

// The stored choice, sanitized. A missing value is the default.
inline WORD ReadKeyboardLayoutId(HKEY root, const wchar_t* key_path,
                                 DWORD extra_flags = 0) noexcept {
    WORD id = kDefaultKeyboardLayoutId;
    TryReadKeyboardLayoutId(root, key_path, id, extra_flags);
    return id;
}

}  // namespace vn_ime
