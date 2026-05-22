#pragma once
#include <windows.h>
#include "types.hpp"

namespace vn_ime {

struct IMEConfig {
    core::InputMethod input_method = core::InputMethod::Telex;
    bool enable_auto_correct = true;
    bool enable_log = false;
};

// Registry path: HKCU\Software\VietnameseTSFIME
inline constexpr const wchar_t* REG_KEY_PATH = L"Software\\VietnameseTSFIME";
inline constexpr const wchar_t* REG_VAL_INPUT_METHOD = L"InputMethod";
inline constexpr const wchar_t* REG_VAL_AUTO_CORRECT = L"EnableAutoCorrect";
inline constexpr const wchar_t* REG_VAL_ENABLE_LOG = L"EnableLog";

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
        RegCloseKey(hKey);
    }
}

} // namespace vn_ime
