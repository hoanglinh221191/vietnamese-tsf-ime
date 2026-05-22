#pragma once
#include <windows.h>
#include <cwchar>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "types.hpp"

namespace vn_ime {

struct IMEConfig {
    core::InputMethod input_method = core::InputMethod::Telex;
    bool enable_auto_correct = true;
    bool enable_log = false;
    bool enable_shorthand = false;
    bool enable_auto_capitalize = false;
    bool enable_app_blocklist = false;
    std::vector<std::wstring> blocked_apps;
};

// Registry path: HKCU\Software\Neokey
inline constexpr const wchar_t* REG_KEY_PATH = L"Software\\Neokey";
inline constexpr const wchar_t* REG_VAL_INPUT_METHOD = L"InputMethod";
inline constexpr const wchar_t* REG_VAL_AUTO_CORRECT = L"EnableAutoCorrect";
inline constexpr const wchar_t* REG_VAL_ENABLE_LOG = L"EnableLog";
inline constexpr const wchar_t* REG_VAL_ENABLE_SHORTHAND = L"EnableShorthand";
inline constexpr const wchar_t* REG_VAL_ENABLE_AUTO_CAPITALIZE = L"EnableAutoCapitalize";
inline constexpr const wchar_t* REG_VAL_ENABLE_APP_BLOCKLIST = L"EnableAppBlocklist";
inline constexpr const wchar_t* REG_VAL_BLOCKED_APPS = L"BlockedApps";
inline constexpr const wchar_t* REG_VAL_CONFIG_REVISION = L"ConfigRevision";

inline std::wstring NormalizeProcessName(std::wstring name) {
    size_t first = name.find_first_not_of(L" \t\r\n\"");
    if (first == std::wstring::npos) return L"";
    size_t last = name.find_last_not_of(L" \t\r\n\"");
    name = name.substr(first, last - first + 1);

    size_t slash = name.find_last_of(L"\\/");
    if (slash != std::wstring::npos) {
        name = name.substr(slash + 1);
    }

    while (!name.empty() && (name.front() == L' ' || name.front() == L'\t' || name.front() == L'"')) {
        name.erase(name.begin());
    }
    while (!name.empty() && (name.back() == L' ' || name.back() == L'\t' || name.back() == L'\r' || name.back() == L'\n' || name.back() == L'"')) {
        name.pop_back();
    }

    for (wchar_t& c : name) {
        if (c >= L'A' && c <= L'Z') {
            c = c - L'A' + L'a';
        }
    }
    return name;
}

inline std::vector<std::wstring> NormalizeProcessList(const std::vector<std::wstring>& apps) {
    std::vector<std::wstring> normalized;
    for (const auto& app : apps) {
        std::wstring name = NormalizeProcessName(app);
        if (name.empty()) continue;

        bool exists = false;
        for (const auto& existing : normalized) {
            if (existing == name) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            normalized.push_back(std::move(name));
        }
    }
    return normalized;
}

inline std::vector<std::wstring> ParseProcessListText(std::wstring_view text) {
    std::vector<std::wstring> apps;
    size_t start = 0;
    while (start <= text.length()) {
        size_t end = text.find(L'\n', start);
        if (end == std::wstring_view::npos) {
            end = text.length();
        }

        apps.emplace_back(text.substr(start, end - start));
        if (end == text.length()) break;
        start = end + 1;
    }
    return NormalizeProcessList(apps);
}

inline std::wstring ProcessListToText(const std::vector<std::wstring>& apps) {
    std::wstring text;
    for (const auto& app : apps) {
        if (!text.empty()) {
            text += L"\r\n";
        }
        text += app;
    }
    return text;
}

inline std::vector<std::wstring> ReadMultiStringValue(HKEY hKey, const wchar_t* valueName) {
    DWORD type = 0;
    DWORD size = 0;
    if (RegQueryValueExW(hKey, valueName, nullptr, &type, nullptr, &size) != ERROR_SUCCESS || type != REG_MULTI_SZ || size == 0) {
        return {};
    }

    std::vector<wchar_t> buffer(size / sizeof(wchar_t) + 1, L'\0');
    if (RegQueryValueExW(hKey, valueName, nullptr, &type, reinterpret_cast<LPBYTE>(buffer.data()), &size) != ERROR_SUCCESS || type != REG_MULTI_SZ) {
        return {};
    }

    std::vector<std::wstring> values;
    const wchar_t* cur = buffer.data();
    while (*cur != L'\0') {
        std::wstring value(cur);
        values.push_back(std::move(value));
        cur += wcslen(cur) + 1;
    }
    return NormalizeProcessList(values);
}

inline void WriteMultiStringValue(HKEY hKey, const wchar_t* valueName, const std::vector<std::wstring>& values) {
    std::vector<std::wstring> normalized = NormalizeProcessList(values);
    std::vector<wchar_t> buffer;
    for (const auto& value : normalized) {
        buffer.insert(buffer.end(), value.begin(), value.end());
        buffer.push_back(L'\0');
    }
    buffer.push_back(L'\0');

    RegSetValueExW(hKey, valueName, 0, REG_MULTI_SZ, reinterpret_cast<const BYTE*>(buffer.data()), static_cast<DWORD>(buffer.size() * sizeof(wchar_t)));
}

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
        DWORD dwEnableAppBlocklist = 0;
        dwSize = sizeof(DWORD);
        if (RegQueryValueExW(hKey, REG_VAL_ENABLE_APP_BLOCKLIST, nullptr, &dwType, reinterpret_cast<LPBYTE>(&dwEnableAppBlocklist), &dwSize) == ERROR_SUCCESS) {
            config.enable_app_blocklist = (dwEnableAppBlocklist != 0);
        }
        config.blocked_apps = ReadMultiStringValue(hKey, REG_VAL_BLOCKED_APPS);
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
        DWORD dwEnableAppBlocklist = config.enable_app_blocklist ? 1 : 0;
        RegSetValueExW(hKey, REG_VAL_ENABLE_APP_BLOCKLIST, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&dwEnableAppBlocklist), sizeof(DWORD));
        WriteMultiStringValue(hKey, REG_VAL_BLOCKED_APPS, config.blocked_apps);
        ULONGLONG revision = GetTickCount64();
        RegSetValueExW(hKey, REG_VAL_CONFIG_REVISION, 0, REG_QWORD, reinterpret_cast<const BYTE*>(&revision), sizeof(revision));
        RegCloseKey(hKey);
    }
}

inline void TouchConfigRevision() {
    HKEY hKey;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, REG_KEY_PATH, 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
        ULONGLONG revision = GetTickCount64();
        RegSetValueExW(hKey, REG_VAL_CONFIG_REVISION, 0, REG_QWORD, reinterpret_cast<const BYTE*>(&revision), sizeof(revision));
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
