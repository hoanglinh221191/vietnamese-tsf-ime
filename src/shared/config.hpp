#pragma once
#include <windows.h>
#include <cwchar>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "types.hpp"

namespace vn_ime {

inline constexpr const wchar_t* DEFAULT_BLOCKED_APP_WINDOWS_TERMINAL = L"windowsterminal.exe";
inline constexpr const wchar_t* DEFAULT_BLOCKED_APP_OPEN_CONSOLE = L"openconsole.exe";
inline constexpr const wchar_t* DEFAULT_BLOCKED_APP_POWERSHELL = L"powershell.exe";
inline constexpr const wchar_t* DEFAULT_BLOCKED_APP_PWSH = L"pwsh.exe";
inline constexpr const wchar_t* DEFAULT_BLOCKED_APP_CMD = L"cmd.exe";
inline constexpr const wchar_t* DEFAULT_BLOCKED_APP_CONHOST = L"conhost.exe";

struct IMEConfig {
    core::InputMethod input_method = core::InputMethod::VNI;
    bool enable_auto_correct = true;
    bool enable_log = false;
    bool enable_shorthand = false;
    bool enable_auto_capitalize = false;
    bool enable_app_blocklist = true;
    std::vector<std::wstring> blocked_apps = {};
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
inline constexpr const wchar_t* SHORTHAND_FILE_NAME = L"neokey_shorthand.txt";

struct ShorthandRule {
    std::wstring key;
    std::wstring value;
};

struct ShorthandParseResult {
    std::vector<ShorthandRule> rules;
    size_t invalid_lines = 0;
    size_t duplicate_lines = 0;
};

inline void TrimView(std::wstring_view& value) {
    while (!value.empty() && (value.front() == L' ' || value.front() == L'\t')) {
        value.remove_prefix(1);
    }
    while (!value.empty() && (value.back() == L' ' || value.back() == L'\t' || value.back() == L'\r')) {
        value.remove_suffix(1);
    }
}

inline std::wstring NormalizeShorthandKey(std::wstring_view key) {
    TrimView(key);
    std::wstring result(key);
    for (wchar_t& c : result) {
        if (c >= L'A' && c <= L'Z') {
            c = c - L'A' + L'a';
        }
    }
    return result;
}

inline bool IsShorthandCommentLine(std::wstring_view line) {
    TrimView(line);
    return line.empty() || line.front() == L'#' || line.front() == L';';
}

inline ShorthandParseResult ParseShorthandRules(std::wstring_view text) {
    ShorthandParseResult result;
    size_t start = 0;
    while (start <= text.length()) {
        size_t end = text.find(L'\n', start);
        if (end == std::wstring_view::npos) {
            end = text.length();
        }

        std::wstring_view line(text.data() + start, end - start);
        if (!IsShorthandCommentLine(line)) {
            size_t eq_pos = line.find(L'=');
            if (eq_pos == std::wstring_view::npos) {
                ++result.invalid_lines;
            } else {
                std::wstring_view key_view = line.substr(0, eq_pos);
                std::wstring_view value_view = line.substr(eq_pos + 1);
                TrimView(value_view);
                std::wstring key = NormalizeShorthandKey(key_view);
                if (key.empty() || value_view.empty()) {
                    ++result.invalid_lines;
                } else {
                    bool replaced = false;
                    for (auto& rule : result.rules) {
                        if (rule.key == key) {
                            rule.value.assign(value_view);
                            ++result.duplicate_lines;
                            replaced = true;
                            break;
                        }
                    }
                    if (!replaced) {
                        result.rules.push_back({std::move(key), std::wstring(value_view)});
                    }
                }
            }
        }

        if (end == text.length()) break;
        start = end + 1;
    }
    return result;
}

inline bool ReadUtf8TextFile(const std::wstring& filePath, std::wstring& content) {
    content.clear();
    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        return false;
    }

    LARGE_INTEGER fileSize;
    if (!GetFileSizeEx(hFile, &fileSize)) {
        CloseHandle(hFile);
        return false;
    }
    if (fileSize.QuadPart <= 0 || fileSize.QuadPart > 16LL * 1024LL * 1024LL) {
        CloseHandle(hFile);
        return fileSize.QuadPart == 0;
    }

    std::string utf8Content(static_cast<size_t>(fileSize.QuadPart), '\0');
    DWORD bytesRead = 0;
    bool ok = ReadFile(hFile, utf8Content.data(), static_cast<DWORD>(utf8Content.size()), &bytesRead, nullptr) && bytesRead > 0;
    CloseHandle(hFile);
    if (!ok) {
        return false;
    }
    utf8Content.resize(bytesRead);

    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8Content.data(), static_cast<int>(utf8Content.length()), nullptr, 0);
    if (wlen <= 0) {
        return false;
    }

    content.resize(wlen);
    MultiByteToWideChar(CP_UTF8, 0, utf8Content.data(), static_cast<int>(utf8Content.length()), content.data(), wlen);
    if (!content.empty() && content.front() == L'\xFEFF') {
        content.erase(content.begin());
    }
    return true;
}

inline bool WriteUtf8TextFileAtomic(const std::wstring& filePath, const std::wstring& content) {
    std::wstring tempPath = filePath + L".tmp." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(GetTickCount64());
    HANDLE hFile = CreateFileW(tempPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        return false;
    }

    const unsigned char bom[] = {0xEF, 0xBB, 0xBF};
    DWORD bytesWritten = 0;
    bool ok = WriteFile(hFile, bom, sizeof(bom), &bytesWritten, nullptr) != FALSE;
    if (ok && !content.empty()) {
        int len = WideCharToMultiByte(CP_UTF8, 0, content.data(), static_cast<int>(content.length()), nullptr, 0, nullptr, nullptr);
        if (len > 0) {
            std::string utf8Content(static_cast<size_t>(len), '\0');
            WideCharToMultiByte(CP_UTF8, 0, content.data(), static_cast<int>(content.length()), utf8Content.data(), len, nullptr, nullptr);
            ok = WriteFile(hFile, utf8Content.data(), static_cast<DWORD>(utf8Content.length()), &bytesWritten, nullptr) != FALSE;
        } else {
            ok = false;
        }
    }

    FlushFileBuffers(hFile);
    CloseHandle(hFile);

    if (!ok || !MoveFileExW(tempPath.c_str(), filePath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tempPath.c_str());
        return false;
    }
    return true;
}

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
        DWORD dwBlockedAppsType = 0;
        DWORD dwBlockedAppsSize = 0;
        if (RegQueryValueExW(hKey, REG_VAL_BLOCKED_APPS, nullptr, &dwBlockedAppsType, nullptr, &dwBlockedAppsSize) == ERROR_SUCCESS &&
            dwBlockedAppsType == REG_MULTI_SZ) {
            config.blocked_apps = ReadMultiStringValue(hKey, REG_VAL_BLOCKED_APPS);
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
        return pathStr.substr(0, pos + 1) + SHORTHAND_FILE_NAME;
    }
    return SHORTHAND_FILE_NAME;
}

} // namespace vn_ime
