#pragma once
#include <windows.h>
#include <string>
#include "types.hpp"

namespace vn_ime {

struct IMEConfig {
    core::InputMethod input_method = core::InputMethod::Telex;
    bool enable_auto_correct = true;
    bool enable_log = false;
    bool enable_shorthand = false;
    bool enable_auto_capitalize = false;
};

// Registry path: HKCU\Software\Neokey
inline constexpr const wchar_t* REG_KEY_PATH = L"Software\\Neokey";
inline constexpr const wchar_t* REG_VAL_INPUT_METHOD = L"InputMethod";
inline constexpr const wchar_t* REG_VAL_AUTO_CORRECT = L"EnableAutoCorrect";
inline constexpr const wchar_t* REG_VAL_ENABLE_LOG = L"EnableLog";
inline constexpr const wchar_t* REG_VAL_ENABLE_SHORTHAND = L"EnableShorthand";
inline constexpr const wchar_t* REG_VAL_ENABLE_AUTO_CAPITALIZE = L"EnableAutoCapitalize";

inline IMEConfig LoadConfigFromRegistry() {
    IMEConfig config;
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_KEY_PATH, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD dwType;
        DWORD dwSize = sizeof(DWORD);
        DWORD dwInputMethod = 0;
        if (RegQueryValueExW(hKey, REG_VAL_INPUT_METHOD, nullptr, &dwType, reinterpret_cast<LPBYTE>(&dwInputMethod), &dwSize) == ERROR_SUCCESS) {
            if (dwInputMethod == 0) {
                config.input_method = core::InputMethod::Telex;
            } else if (dwInputMethod == 1) {
                config.input_method = core::InputMethod::SimpleTelex;
            } else if (dwInputMethod == 2) {
                config.input_method = core::InputMethod::VNI;
            }
        }
        DWORD dwAutoCorrect = 1;
        dwSize = sizeof(DWORD);
        if (RegQueryValueExW(hKey, REG_VAL_AUTO_CORRECT, nullptr, &dwType, reinterpret_cast<LPBYTE>(&dwAutoCorrect), &dwSize) == ERROR_SUCCESS) {
            config.enable_auto_correct = (dwAutoCorrect != 0);
        }
        DWORD dwEnableLog = 0;
        dwSize = sizeof(DWORD);
        if (RegQueryValueExW(hKey, REG_VAL_ENABLE_LOG, nullptr, &dwType, reinterpret_cast<LPBYTE>(&dwEnableLog), &dwSize) == ERROR_SUCCESS) {
            config.enable_log = (dwEnableLog != 0);
        }
        DWORD dwEnableShorthand = 0;
        dwSize = sizeof(DWORD);
        if (RegQueryValueExW(hKey, REG_VAL_ENABLE_SHORTHAND, nullptr, &dwType, reinterpret_cast<LPBYTE>(&dwEnableShorthand), &dwSize) == ERROR_SUCCESS) {
            config.enable_shorthand = (dwEnableShorthand != 0);
        }
        DWORD dwEnableAutoCapitalize = 0;
        dwSize = sizeof(DWORD);
        if (RegQueryValueExW(hKey, REG_VAL_ENABLE_AUTO_CAPITALIZE, nullptr, &dwType, reinterpret_cast<LPBYTE>(&dwEnableAutoCapitalize), &dwSize) == ERROR_SUCCESS) {
            config.enable_auto_capitalize = (dwEnableAutoCapitalize != 0);
        }
        RegCloseKey(hKey);
    }
    return config;
}

inline void SaveConfigToRegistry(const IMEConfig& config) {
    HKEY hKey;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, REG_KEY_PATH, 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
        DWORD dwInputMethod = 0;
        if (config.input_method == core::InputMethod::Telex) {
            dwInputMethod = 0;
        } else if (config.input_method == core::InputMethod::SimpleTelex) {
            dwInputMethod = 1;
        } else if (config.input_method == core::InputMethod::VNI) {
            dwInputMethod = 2;
        }
        RegSetValueExW(hKey, REG_VAL_INPUT_METHOD, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&dwInputMethod), sizeof(DWORD));
        DWORD dwAutoCorrect = config.enable_auto_correct ? 1 : 0;
        RegSetValueExW(hKey, REG_VAL_AUTO_CORRECT, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&dwAutoCorrect), sizeof(DWORD));
        DWORD dwEnableLog = config.enable_log ? 1 : 0;
        RegSetValueExW(hKey, REG_VAL_ENABLE_LOG, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&dwEnableLog), sizeof(DWORD));
        DWORD dwEnableShorthand = config.enable_shorthand ? 1 : 0;
        RegSetValueExW(hKey, REG_VAL_ENABLE_SHORTHAND, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&dwEnableShorthand), sizeof(DWORD));
        DWORD dwEnableAutoCapitalize = config.enable_auto_capitalize ? 1 : 0;
        RegSetValueExW(hKey, REG_VAL_ENABLE_AUTO_CAPITALIZE, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&dwEnableAutoCapitalize), sizeof(DWORD));
        RegCloseKey(hKey);
    }
}

inline std::wstring GetShorthandFilePath(HINSTANCE hInst = nullptr) {
    wchar_t path[MAX_PATH];
    if (GetModuleFileNameW(hInst, path, MAX_PATH) == 0) {
        return L"";
    }
    std::wstring pathStr(path);
    size_t pos = pathStr.find_last_of(L"\\/");
    if (pos != std::wstring::npos) {
        return pathStr.substr(0, pos + 1) + L"neokey_shorthand.txt";
    }
    return L"neokey_shorthand.txt";
}

} // namespace vn_ime
