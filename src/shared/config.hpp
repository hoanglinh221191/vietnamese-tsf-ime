#pragma once
#include <windows.h>
#include <cstddef>
#include <cwchar>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
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

using core::CorrectionLevel;
using core::EnglishProtectionLevel;

enum class AppInputMode : uint8_t {
    Telex = 0,
    SimpleTelex = 1,
    VNI = 2,
    Off = 3,
};

enum class AppInputProfileOrigin : uint8_t {
    Manual = 0,
    Automatic = 1,
};

struct AppInputProfile {
    std::wstring process_name;
    bool enabled = true;
    core::InputMethod preferred_method = core::InputMethod::VNI;
    AppInputProfileOrigin origin = AppInputProfileOrigin::Manual;

    bool operator==(const AppInputProfile&) const = default;
};

struct IMEConfig {
    core::InputMethod input_method = core::InputMethod::VNI;
    bool enable_auto_correct = true;
    CorrectionLevel auto_correct_level = CorrectionLevel::Normal;
    EnglishProtectionLevel english_protection_level = EnglishProtectionLevel::Balanced;
    bool enable_fuzzy_input = false;
    DWORD fuzzy_input_flags = 0;
    bool enable_log = false;
    bool enable_shorthand = false;
    bool enable_smart_undo = true;
    bool enable_smart_context_protection = true;
    bool enable_auto_word_segmentation = false;
    bool enable_auto_capitalize = false;
    bool enable_app_blocklist = true;
    std::vector<std::wstring> blocked_apps = {};
    bool enable_auto_exclude = true;
    std::vector<std::wstring> auto_blocked_apps = {};
    bool enable_app_input_profiles = true;
    bool enable_auto_app_input_profiles = true;
    std::vector<AppInputProfile> app_input_profiles = {};
    std::vector<std::wstring> direct_apps = {};
    DWORD typing_mode = 0; // 0 = Vietnamese, 1 = English
    DWORD hotkey_mode = 0; // 0 = Ctrl+Shift, 1 = Alt+Z
    bool enable_auto_start = false;
};

inline CorrectionLevel NormalizeCorrectionLevelValue(DWORD value) noexcept {
    switch (value) {
        case 0:
            return CorrectionLevel::Off;
        case 1:
            return CorrectionLevel::Normal;
        case 2:
            return CorrectionLevel::Advanced;
        case 3:
            return CorrectionLevel::Experimental;
        default:
            return CorrectionLevel::Normal;
    }
}

inline DWORD CorrectionLevelToConfigIndex(CorrectionLevel level) noexcept {
    return static_cast<DWORD>(
        NormalizeCorrectionLevelValue(static_cast<DWORD>(level)));
}

inline EnglishProtectionLevel NormalizeEnglishProtectionLevelValue(DWORD value) noexcept {
    switch (value) {
        case 0:
            return EnglishProtectionLevel::Off;
        case 1:
            return EnglishProtectionLevel::Balanced;
        case 2:
            return EnglishProtectionLevel::EnglishFirst;
        default:
            return EnglishProtectionLevel::Balanced;
    }
}

inline EnglishProtectionLevel ResolveEnglishProtectionLevel(
    std::optional<DWORD> level_value,
    std::optional<DWORD> legacy_enabled_value) noexcept {
    if (level_value.has_value()) {
        return NormalizeEnglishProtectionLevelValue(*level_value);
    }
    if (legacy_enabled_value.has_value() && *legacy_enabled_value == 0) {
        return EnglishProtectionLevel::Off;
    }
    return EnglishProtectionLevel::Balanced;
}

inline DWORD EnglishProtectionLevelToConfigIndex(EnglishProtectionLevel level) noexcept {
    return static_cast<DWORD>(NormalizeEnglishProtectionLevelValue(static_cast<DWORD>(level)));
}

inline constexpr DWORD FUZZY_INPUT_FLAG_L_N = 1u << 0;
inline constexpr DWORD FUZZY_INPUT_FLAG_TR_CH = 1u << 1;
inline constexpr DWORD FUZZY_INPUT_FLAG_S_X = 1u << 2;
inline constexpr DWORD FUZZY_INPUT_FLAG_R_D_GI = 1u << 3;
inline constexpr DWORD FUZZY_INPUT_FLAG_HOI_NGA = 1u << 4;
inline constexpr DWORD FUZZY_INPUT_VALID_FLAGS =
    FUZZY_INPUT_FLAG_L_N |
    FUZZY_INPUT_FLAG_TR_CH |
    FUZZY_INPUT_FLAG_S_X |
    FUZZY_INPUT_FLAG_R_D_GI |
    FUZZY_INPUT_FLAG_HOI_NGA;

inline constexpr DWORD NormalizeFuzzyInputFlags(DWORD flags) noexcept {
    return flags & FUZZY_INPUT_VALID_FLAGS;
}

inline constexpr bool IsFuzzyInputEffectivelyEnabled(
    bool enabled, DWORD flags) noexcept {
    return enabled && NormalizeFuzzyInputFlags(flags) != 0;
}

inline bool NormalizeSmartUndoValue(DWORD value) noexcept {
    return value != 0;
}

inline bool ResolveSmartUndoEnabled(
    std::optional<DWORD> stored_value) noexcept {
    return stored_value.has_value()
        ? NormalizeSmartUndoValue(*stored_value)
        : true;
}

inline DWORD SmartUndoEnabledToRegistryValue(bool enabled) noexcept {
    return enabled ? 1u : 0u;
}

inline bool NormalizeSmartContextProtectionValue(DWORD value) noexcept {
    return value != 0;
}

inline bool ResolveSmartContextProtectionEnabled(
    std::optional<DWORD> stored_value) noexcept {
    return stored_value.has_value()
        ? NormalizeSmartContextProtectionValue(*stored_value)
        : true;
}

inline DWORD SmartContextProtectionEnabledToRegistryValue(
    bool enabled) noexcept {
    return enabled ? 1u : 0u;
}

inline bool NormalizeAutoWordSegmentationValue(DWORD value) noexcept {
    return value != 0;
}

inline bool ResolveAutoWordSegmentationEnabled(
    std::optional<DWORD> stored_value) noexcept {
    return stored_value.has_value()
        ? NormalizeAutoWordSegmentationValue(*stored_value)
        : false;
}

inline DWORD AutoWordSegmentationEnabledToRegistryValue(
    bool enabled) noexcept {
    return enabled ? 1u : 0u;
}

inline constexpr bool IsAutoWordSegmentationAvailable(
    CorrectionLevel level) noexcept {
    return level == CorrectionLevel::Experimental;
}

inline constexpr bool NormalizeAutoWordSegmentationEnabled(
    bool enabled,
    CorrectionLevel level) noexcept {
    return enabled && IsAutoWordSegmentationAvailable(level);
}

// Registry path: HKCU\Software\Neokey
inline constexpr const wchar_t* REG_KEY_PATH = L"Software\\Neokey";
inline constexpr const wchar_t* REG_VAL_INPUT_METHOD = L"InputMethod";
inline constexpr const wchar_t* REG_VAL_AUTO_CORRECT = L"EnableAutoCorrect";
inline constexpr const wchar_t* REG_VAL_CORRECTION_LEVEL = L"CorrectionLevel";
inline constexpr const wchar_t* REG_VAL_ENABLE_ENGLISH_PROTECTION = L"EnableEnglishProtection";
inline constexpr const wchar_t* REG_VAL_ENGLISH_PROTECTION_LEVEL = L"EnglishProtectionLevel";
inline constexpr const wchar_t* REG_VAL_ENABLE_FUZZY_INPUT = L"EnableFuzzyInput";
inline constexpr const wchar_t* REG_VAL_FUZZY_INPUT_FLAGS = L"FuzzyInputFlags";
inline constexpr const wchar_t* REG_VAL_ENABLE_LOG = L"EnableLog";
inline constexpr const wchar_t* REG_VAL_ENABLE_SHORTHAND = L"EnableShorthand";
inline constexpr const wchar_t* REG_VAL_ENABLE_SMART_UNDO = L"EnableSmartUndo";
inline constexpr const wchar_t* REG_VAL_ENABLE_SMART_CONTEXT_PROTECTION =
    L"EnableSmartContextProtection";
inline constexpr const wchar_t* REG_VAL_ENABLE_AUTO_WORD_SEGMENTATION =
    L"EnableAutoWordSegmentation";
inline constexpr const wchar_t* REG_VAL_ENABLE_AUTO_CAPITALIZE = L"EnableAutoCapitalize";
inline constexpr const wchar_t* REG_VAL_ENABLE_APP_BLOCKLIST = L"EnableAppBlocklist";
inline constexpr const wchar_t* REG_VAL_BLOCKED_APPS = L"BlockedApps";
inline constexpr const wchar_t* REG_VAL_ENABLE_AUTO_EXCLUDE = L"EnableAutoExclude";
inline constexpr const wchar_t* REG_VAL_AUTO_BLOCKED_APPS = L"AutoBlockedApps";
inline constexpr const wchar_t* REG_VAL_ENABLE_APP_INPUT_PROFILES = L"EnableAppInputProfiles";
inline constexpr const wchar_t* REG_VAL_ENABLE_AUTO_APP_INPUT_PROFILES = L"EnableAutoAppInputProfiles";
inline constexpr const wchar_t* REG_VAL_APP_INPUT_PROFILES = L"AppInputProfiles";
inline constexpr const wchar_t* REG_APP_TYPING_MODE_PREFIX = L"AppTypingMode_";
inline constexpr const wchar_t* REG_VAL_DIRECT_APPS = L"DirectApps";
inline constexpr const wchar_t* REG_VAL_TYPING_MODE = L"TypingMode";
inline constexpr const wchar_t* REG_VAL_HOTKEY_MODE = L"HotkeyMode";
inline constexpr const wchar_t* REG_VAL_CONFIG_REVISION = L"ConfigRevision";
inline constexpr const wchar_t* SHORTHAND_FILE_NAME = L"neokey_shorthand.txt";
inline constexpr std::wstring_view APP_INPUT_PROFILES_SCHEMA_V1 =
    L"neokey.app-input-profiles\t1";
inline constexpr size_t MAX_APP_INPUT_PROFILE_RULES = 256;
inline constexpr size_t MAX_APP_INPUT_PROFILE_PROCESS_NAME_CHARS = 260;
inline constexpr size_t MAX_APP_INPUT_PROFILE_RECORD_CHARS =
    MAX_APP_INPUT_PROFILE_PROCESS_NAME_CHARS + 6;
inline constexpr size_t MAX_APP_INPUT_PROFILES_SERIALIZED_CHARS =
    1 + APP_INPUT_PROFILES_SCHEMA_V1.length() + 1 +
    MAX_APP_INPUT_PROFILE_RULES *
        (MAX_APP_INPUT_PROFILE_RECORD_CHARS + 1);
inline constexpr size_t MAX_LEGACY_APP_TYPING_VALUES_SCANNED = 4096;
inline constexpr size_t MAX_SHORTHAND_RULES = 4096;
inline constexpr size_t MAX_SHORTHAND_KEY_CHARS = 128;
inline constexpr size_t MAX_SHORTHAND_VALUE_CHARS = 16384;
inline constexpr size_t MAX_SHORTHAND_LINE_CHARS =
    MAX_SHORTHAND_KEY_CHARS + 1 + MAX_SHORTHAND_VALUE_CHARS;
static_assert(
    MAX_APP_INPUT_PROFILES_SERIALIZED_CHARS * sizeof(wchar_t) <= MAXDWORD,
    "App input profiles REG_MULTI_SZ must fit in a DWORD-sized registry value");

struct ShorthandRule {
    std::wstring key;
    std::wstring value;
};

struct ShorthandParseResult {
    std::vector<ShorthandRule> rules;
    size_t invalid_lines = 0;
    size_t duplicate_lines = 0;
    size_t limit_exceeded_lines = 0;
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
    std::unordered_map<std::wstring, size_t> rule_indices;
    rule_indices.reserve(256);
    size_t start = 0;
    while (start <= text.length()) {
        size_t end = text.find(L'\n', start);
        if (end == std::wstring_view::npos) {
            end = text.length();
        }

        std::wstring_view line(text.data() + start, end - start);
        if (!line.empty() && line.back() == L'\r') {
            line.remove_suffix(1);
        }
        if (!IsShorthandCommentLine(line)) {
            if (line.length() > MAX_SHORTHAND_LINE_CHARS) {
                ++result.invalid_lines;
                ++result.limit_exceeded_lines;
            } else {
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
                    } else if (key.length() > MAX_SHORTHAND_KEY_CHARS ||
                               value_view.length() >
                                   MAX_SHORTHAND_VALUE_CHARS) {
                        ++result.invalid_lines;
                        ++result.limit_exceeded_lines;
                    } else {
                        const auto existing = rule_indices.find(key);
                        if (existing != rule_indices.end()) {
                            result.rules[existing->second].value.assign(value_view);
                            ++result.duplicate_lines;
                        } else if (result.rules.size() >=
                                   MAX_SHORTHAND_RULES) {
                            ++result.invalid_lines;
                            ++result.limit_exceeded_lines;
                        } else {
                            const size_t index = result.rules.size();
                            rule_indices.emplace(key, index);
                            result.rules.push_back(
                                {std::move(key), std::wstring(value_view)});
                        }
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
    const size_t separator = filePath.find_last_of(L"\\/");
    if (separator != std::wstring::npos) {
        const std::wstring parent = filePath.substr(0, separator);
        if (!parent.empty() &&
            !CreateDirectoryW(parent.c_str(), nullptr) &&
            GetLastError() != ERROR_ALREADY_EXISTS) {
            return false;
        }
    }

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

inline bool IsValidAppInputMethod(core::InputMethod method) noexcept {
    return method == core::InputMethod::Telex ||
           method == core::InputMethod::SimpleTelex ||
           method == core::InputMethod::VNI;
}

inline bool IsValidAppInputProfileOrigin(
    AppInputProfileOrigin origin) noexcept {
    return origin == AppInputProfileOrigin::Manual ||
        origin == AppInputProfileOrigin::Automatic;
}

inline core::InputMethod NormalizeAppInputMethod(core::InputMethod method) noexcept {
    return IsValidAppInputMethod(method) ? method : core::InputMethod::VNI;
}

inline bool IsValidAppProfileProcessName(std::wstring_view process_name) noexcept {
    if (process_name.empty() ||
        process_name.length() > MAX_APP_INPUT_PROFILE_PROCESS_NAME_CHARS) {
        return false;
    }
    for (const wchar_t ch : process_name) {
        if (ch < L' ' || ch == L'\t' || ch == L'\\' || ch == L'/') {
            return false;
        }
    }
    return true;
}

inline bool IsConfigurableAppProcessName(std::wstring_view process_name) {
    const std::wstring normalized = NormalizeProcessName(
        std::wstring(process_name));
    if (!IsValidAppProfileProcessName(normalized)) {
        return false;
    }
    if (!normalized.ends_with(L".exe")) {
        return false;
    }
    return normalized != L"explorer.exe" &&
        normalized != L"neokey_config.exe" &&
        normalized != L"searchhost.exe" &&
        normalized != L"startmenuexperiencehost.exe";
}

inline std::optional<size_t> FindAppInputProfileIndex(
    const std::vector<AppInputProfile>& profiles,
    std::wstring_view process_name) {
    const std::wstring normalized = NormalizeProcessName(std::wstring(process_name));
    if (!IsValidAppProfileProcessName(normalized)) {
        return std::nullopt;
    }
    for (size_t i = profiles.size(); i > 0; --i) {
        const AppInputProfile& profile = profiles[i - 1];
        if (!IsValidAppInputMethod(profile.preferred_method) ||
            !IsValidAppInputProfileOrigin(profile.origin)) {
            continue;
        }
        if (NormalizeProcessName(profile.process_name) == normalized) {
            return i - 1;
        }
    }
    return std::nullopt;
}

inline std::optional<AppInputProfile> LookupAppInputProfile(
    const std::vector<AppInputProfile>& profiles,
    std::wstring_view process_name) {
    const auto index = FindAppInputProfileIndex(profiles, process_name);
    if (!index.has_value()) {
        return std::nullopt;
    }
    AppInputProfile profile = profiles[*index];
    profile.process_name = NormalizeProcessName(std::move(profile.process_name));
    return profile;
}

inline std::vector<AppInputProfile> NormalizeAppInputProfiles(
    const std::vector<AppInputProfile>& profiles) {
    std::vector<AppInputProfile> normalized;
    normalized.reserve(
        profiles.size() < MAX_APP_INPUT_PROFILE_RULES
            ? profiles.size()
            : MAX_APP_INPUT_PROFILE_RULES);

    for (const auto& profile : profiles) {
        AppInputProfile item = profile;
        item.process_name = NormalizeProcessName(std::move(item.process_name));
        if (!IsValidAppProfileProcessName(item.process_name) ||
            !IsValidAppInputMethod(item.preferred_method) ||
            !IsValidAppInputProfileOrigin(item.origin)) {
            continue;
        }

        const auto existing = FindAppInputProfileIndex(normalized, item.process_name);
        if (existing.has_value()) {
            normalized.erase(normalized.begin() + static_cast<std::ptrdiff_t>(*existing));
        } else if (normalized.size() >= MAX_APP_INPUT_PROFILE_RULES) {
            continue;
        }
        normalized.push_back(std::move(item));
    }
    return normalized;
}

struct ResolvedAppInputProfile {
    bool has_explicit_profile = false;
    bool enabled = true;
    core::InputMethod input_method = core::InputMethod::VNI;
};

inline ResolvedAppInputProfile ResolveAppInputProfile(
    const std::vector<AppInputProfile>& profiles,
    std::wstring_view process_name,
    bool global_enabled,
    core::InputMethod global_method) {
    const core::InputMethod normalized_global = NormalizeAppInputMethod(global_method);
    const auto profile = LookupAppInputProfile(profiles, process_name);
    if (!profile.has_value() || !IsValidAppInputMethod(profile->preferred_method)) {
        return {false, global_enabled, normalized_global};
    }
    return {true, profile->enabled, profile->preferred_method};
}

inline ResolvedAppInputProfile ResolveAppInputProfile(
    const std::vector<AppInputProfile>& profiles,
    std::wstring_view process_name,
    core::InputMethod global_method) {
    return ResolveAppInputProfile(
        profiles, process_name, true, global_method);
}

inline ResolvedAppInputProfile ResolveEffectiveAppInputProfile(
    bool enable_app_input_profiles,
    const std::vector<AppInputProfile>& profiles,
    std::wstring_view process_name,
    bool global_enabled,
    core::InputMethod global_method) {
    if (!enable_app_input_profiles) {
        return {
            false, global_enabled, NormalizeAppInputMethod(global_method)};
    }
    return ResolveAppInputProfile(
        profiles, process_name, global_enabled, global_method);
}

inline bool IsExplicitAppInputProfileDisabled(
    bool enable_app_input_profiles,
    const ResolvedAppInputProfile& effective) noexcept {
    return enable_app_input_profiles && effective.has_explicit_profile &&
        !effective.enabled;
}

inline std::optional<core::InputMethod> InputMethodForAppInputMode(
    AppInputMode mode) noexcept {
    switch (mode) {
        case AppInputMode::Telex:
            return core::InputMethod::Telex;
        case AppInputMode::SimpleTelex:
            return core::InputMethod::SimpleTelex;
        case AppInputMode::VNI:
            return core::InputMethod::VNI;
        case AppInputMode::Off:
            return std::nullopt;
        default:
            return std::nullopt;
    }
}

inline AppInputMode AppInputModeForMethod(
    core::InputMethod method) noexcept {
    switch (method) {
        case core::InputMethod::Telex:
            return AppInputMode::Telex;
        case core::InputMethod::SimpleTelex:
            return AppInputMode::SimpleTelex;
        case core::InputMethod::VNI:
        default:
            return AppInputMode::VNI;
    }
}

inline AppInputMode AppInputModeForProfile(const AppInputProfile& profile) noexcept {
    return profile.enabled
        ? AppInputModeForMethod(profile.preferred_method)
        : AppInputMode::Off;
}

inline bool UpsertAppInputMode(
    std::vector<AppInputProfile>& profiles,
    std::wstring_view process_name,
    AppInputMode mode,
    core::InputMethod fallback_method,
    std::optional<AppInputProfileOrigin> origin = std::nullopt) {
    profiles = NormalizeAppInputProfiles(profiles);
    const std::wstring normalized = NormalizeProcessName(std::wstring(process_name));
    if (!IsValidAppProfileProcessName(normalized) ||
        (origin.has_value() &&
         !IsValidAppInputProfileOrigin(*origin))) {
        return false;
    }

    const auto method = InputMethodForAppInputMode(mode);
    if (mode != AppInputMode::Off && !method.has_value()) {
        return false;
    }

    const auto existing = FindAppInputProfileIndex(profiles, normalized);
    if (existing.has_value()) {
        AppInputProfile updated = profiles[*existing];
        if (mode == AppInputMode::Off) {
            updated.enabled = false;
        } else {
            updated.enabled = true;
            updated.preferred_method = *method;
        }
        if (origin.has_value()) {
            updated.origin = *origin;
        }
        if (updated == profiles[*existing]) {
            return false;
        }
        profiles[*existing] = std::move(updated);
        return true;
    }

    if (profiles.size() >= MAX_APP_INPUT_PROFILE_RULES) {
        return false;
    }
    profiles.push_back({
        normalized,
        mode != AppInputMode::Off,
        method.value_or(NormalizeAppInputMethod(fallback_method)),
        origin.value_or(AppInputProfileOrigin::Manual),
    });
    return true;
}

inline bool UpsertManualAppInputMode(
    std::vector<AppInputProfile>& profiles,
    std::wstring_view process_name,
    AppInputMode mode,
    core::InputMethod fallback_method) {
    return UpsertAppInputMode(
        profiles, process_name, mode, fallback_method,
        AppInputProfileOrigin::Manual);
}

inline bool SetAppInputProfileEnabled(
    std::vector<AppInputProfile>& profiles,
    std::wstring_view process_name,
    bool enabled,
    core::InputMethod fallback_method,
    std::optional<AppInputProfileOrigin> origin = std::nullopt) {
    profiles = NormalizeAppInputProfiles(profiles);
    const std::wstring normalized = NormalizeProcessName(std::wstring(process_name));
    if (!IsValidAppProfileProcessName(normalized) ||
        (origin.has_value() &&
         !IsValidAppInputProfileOrigin(*origin))) {
        return false;
    }
    const auto existing = FindAppInputProfileIndex(profiles, normalized);
    if (existing.has_value()) {
        AppInputProfile updated = profiles[*existing];
        updated.enabled = enabled;
        if (origin.has_value()) {
            updated.origin = *origin;
        }
        if (updated == profiles[*existing]) {
            return false;
        }
        profiles[*existing] = std::move(updated);
        return true;
    }
    if (profiles.size() >= MAX_APP_INPUT_PROFILE_RULES) {
        return false;
    }
    profiles.push_back({
        normalized, enabled, NormalizeAppInputMethod(fallback_method),
        origin.value_or(AppInputProfileOrigin::Manual)});
    return true;
}

inline bool ToggleAppInputProfileEnabled(
    std::vector<AppInputProfile>& profiles,
    std::wstring_view process_name,
    core::InputMethod fallback_method,
    std::optional<AppInputProfileOrigin> origin = std::nullopt) {
    profiles = NormalizeAppInputProfiles(profiles);
    const std::wstring normalized = NormalizeProcessName(std::wstring(process_name));
    if (!IsValidAppProfileProcessName(normalized) ||
        (origin.has_value() &&
         !IsValidAppInputProfileOrigin(*origin))) {
        return false;
    }
    const auto existing = FindAppInputProfileIndex(profiles, normalized);
    if (existing.has_value()) {
        profiles[*existing].enabled = !profiles[*existing].enabled;
        if (origin.has_value()) {
            profiles[*existing].origin = *origin;
        }
        return true;
    }
    if (profiles.size() >= MAX_APP_INPUT_PROFILE_RULES) {
        return false;
    }
    profiles.push_back({
        normalized, false, NormalizeAppInputMethod(fallback_method),
        origin.value_or(AppInputProfileOrigin::Manual)});
    return true;
}

inline bool RemoveAppInputProfile(
    std::vector<AppInputProfile>& profiles,
    std::wstring_view process_name) {
    profiles = NormalizeAppInputProfiles(profiles);
    const auto existing = FindAppInputProfileIndex(profiles, process_name);
    if (!existing.has_value()) {
        return false;
    }
    profiles.erase(profiles.begin() + static_cast<std::ptrdiff_t>(*existing));
    return true;
}

struct AppInputProfilesParseResult {
    std::vector<AppInputProfile> profiles;
    size_t invalid_records = 0;
    size_t duplicate_records = 0;
    bool schema_valid = false;
    bool limit_exceeded = false;
};

inline wchar_t AppInputMethodPersistenceCode(core::InputMethod method) noexcept {
    switch (method) {
        case core::InputMethod::Telex:
            return L'0';
        case core::InputMethod::SimpleTelex:
            return L'1';
        case core::InputMethod::VNI:
            return L'2';
        default:
            return L'\0';
    }
}

inline std::optional<core::InputMethod> ParseAppInputMethodPersistenceCode(
    std::wstring_view value) noexcept {
    if (value == L"0") return core::InputMethod::Telex;
    if (value == L"1") return core::InputMethod::SimpleTelex;
    if (value == L"2") return core::InputMethod::VNI;
    return std::nullopt;
}

inline wchar_t AppInputProfileOriginPersistenceCode(
    AppInputProfileOrigin origin) noexcept {
    switch (origin) {
        case AppInputProfileOrigin::Manual:
            return L'0';
        case AppInputProfileOrigin::Automatic:
            return L'1';
        default:
            return L'\0';
    }
}

inline std::optional<AppInputProfileOrigin>
ParseAppInputProfileOriginPersistenceCode(
    std::wstring_view value) noexcept {
    if (value == L"0") return AppInputProfileOrigin::Manual;
    if (value == L"1") return AppInputProfileOrigin::Automatic;
    return std::nullopt;
}

inline std::optional<size_t> RawMultiStringCharCount(
    const std::vector<std::wstring>& values) noexcept {
    if (values.empty()) {
        return 2;
    }

    size_t total_chars = 1;
    for (const auto& value : values) {
        if (value.find(L'\0') != std::wstring::npos ||
            value.length() + 1 >
                MAX_APP_INPUT_PROFILES_SERIALIZED_CHARS - total_chars) {
            return std::nullopt;
        }
        total_chars += value.length() + 1;
    }
    return total_chars;
}

inline std::optional<std::vector<AppInputProfile>>
NormalizeAppInputProfilesForPersistence(
    const std::vector<AppInputProfile>& profiles) {
    if (profiles.size() > MAX_APP_INPUT_PROFILE_RULES) {
        return std::nullopt;
    }

    std::vector<AppInputProfile> normalized;
    normalized.reserve(profiles.size());
    for (const auto& profile : profiles) {
        AppInputProfile item = profile;
        item.process_name = NormalizeProcessName(std::move(item.process_name));
        if (!IsValidAppProfileProcessName(item.process_name) ||
            !IsValidAppInputMethod(item.preferred_method) ||
            !IsValidAppInputProfileOrigin(item.origin)) {
            return std::nullopt;
        }
        const auto duplicate = FindAppInputProfileIndex(
            normalized, item.process_name);
        if (duplicate.has_value()) {
            normalized.erase(
                normalized.begin() + static_cast<std::ptrdiff_t>(*duplicate));
        }
        normalized.push_back(std::move(item));
    }
    return normalized;
}

struct AppInputProfilesSerializeResult {
    std::vector<std::wstring> records;
    size_t serialized_chars = 0;
    bool success = false;
};

inline AppInputProfilesSerializeResult SerializeAppInputProfiles(
    const std::vector<AppInputProfile>& profiles) {
    AppInputProfilesSerializeResult result;
    const auto normalized = NormalizeAppInputProfilesForPersistence(profiles);
    if (!normalized.has_value()) {
        return result;
    }

    std::vector<std::wstring> records;
    records.reserve(normalized->size() + 1);
    records.emplace_back(APP_INPUT_PROFILES_SCHEMA_V1);

    for (const auto& profile : *normalized) {
        const wchar_t method_code = AppInputMethodPersistenceCode(profile.preferred_method);
        const wchar_t origin_code =
            AppInputProfileOriginPersistenceCode(profile.origin);
        if (method_code == L'\0' || origin_code == L'\0') {
            return result;
        }
        std::wstring record = profile.process_name;
        record += profile.enabled ? L"\t1\t" : L"\t0\t";
        record.push_back(method_code);
        record.push_back(L'\t');
        record.push_back(origin_code);
        if (record.length() > MAX_APP_INPUT_PROFILE_RECORD_CHARS) {
            return result;
        }
        records.push_back(std::move(record));
    }

    const auto serialized_chars = RawMultiStringCharCount(records);
    if (!serialized_chars.has_value() ||
        *serialized_chars > MAX_APP_INPUT_PROFILES_SERIALIZED_CHARS) {
        return result;
    }
    result.records = std::move(records);
    result.serialized_chars = *serialized_chars;
    result.success = true;
    return result;
}

inline AppInputProfilesParseResult ParseAppInputProfiles(
    const std::vector<std::wstring>& records) {
    AppInputProfilesParseResult result;
    if (records.empty() || records.front() != APP_INPUT_PROFILES_SCHEMA_V1) {
        return result;
    }
    result.schema_valid = true;
    if (records.size() > MAX_APP_INPUT_PROFILE_RULES + 1) {
        result.limit_exceeded = true;
        result.profiles.clear();
        return result;
    }

    size_t total_chars = 1;
    for (const auto& record : records) {
        if (record.length() > MAX_APP_INPUT_PROFILE_RECORD_CHARS ||
            total_chars + record.length() + 1 >
                MAX_APP_INPUT_PROFILES_SERIALIZED_CHARS) {
            result.limit_exceeded = true;
            result.profiles.clear();
            return result;
        }
        total_chars += record.length() + 1;
    }

    for (size_t i = 1; i < records.size(); ++i) {
        const std::wstring_view record(records[i]);
        const size_t first_tab = record.find(L'\t');
        const size_t second_tab = first_tab == std::wstring_view::npos
            ? std::wstring_view::npos
            : record.find(L'\t', first_tab + 1);
        const size_t third_tab = second_tab == std::wstring_view::npos
            ? std::wstring_view::npos
            : record.find(L'\t', second_tab + 1);
        if (first_tab == std::wstring_view::npos ||
            second_tab == std::wstring_view::npos ||
            third_tab == std::wstring_view::npos ||
            record.find(L'\t', third_tab + 1) != std::wstring_view::npos) {
            ++result.invalid_records;
            continue;
        }

        std::wstring process_name = NormalizeProcessName(
            std::wstring(record.substr(0, first_tab)));
        const std::wstring_view enabled_value =
            record.substr(first_tab + 1, second_tab - first_tab - 1);
        const auto method = ParseAppInputMethodPersistenceCode(
            record.substr(second_tab + 1, third_tab - second_tab - 1));
        const auto origin = ParseAppInputProfileOriginPersistenceCode(
            record.substr(third_tab + 1));
        if (!IsValidAppProfileProcessName(process_name) ||
            (enabled_value != L"0" && enabled_value != L"1") ||
            !method.has_value() || !origin.has_value()) {
            ++result.invalid_records;
            continue;
        }

        const auto duplicate = FindAppInputProfileIndex(result.profiles, process_name);
        if (duplicate.has_value()) {
            result.profiles.erase(
                result.profiles.begin() + static_cast<std::ptrdiff_t>(*duplicate));
            ++result.duplicate_records;
        }
        result.profiles.push_back({
            std::move(process_name), enabled_value == L"1", *method,
            *origin});
    }
    return result;
}

struct LegacyAppTypingMode {
    std::wstring process_name;
    DWORD mode = 0;
};

inline std::vector<AppInputProfile> MigrateAppInputProfiles(
    const std::vector<AppInputProfile>& new_profiles,
    const std::vector<std::wstring>& legacy_blocked_apps,
    const std::vector<std::wstring>& legacy_auto_blocked_apps,
    const std::vector<LegacyAppTypingMode>& legacy_typing_modes,
    core::InputMethod global_method) {
    std::vector<AppInputProfile> migrated = NormalizeAppInputProfiles(new_profiles);
    const std::vector<AppInputProfile> explicit_profiles = migrated;
    const core::InputMethod fallback = NormalizeAppInputMethod(global_method);

    std::vector<std::wstring> normalized_legacy_blocked;
    for (const auto& raw_process_name : legacy_blocked_apps) {
        const std::wstring process_name = NormalizeProcessName(raw_process_name);
        if (!IsValidAppProfileProcessName(process_name)) {
            continue;
        }
        bool duplicate = false;
        for (const auto& existing : normalized_legacy_blocked) {
            if (existing == process_name) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            normalized_legacy_blocked.push_back(process_name);
        }
    }
    std::vector<std::wstring> normalized_legacy_auto_blocked;
    for (const auto& raw_process_name : legacy_auto_blocked_apps) {
        const std::wstring process_name = NormalizeProcessName(raw_process_name);
        if (!IsValidAppProfileProcessName(process_name)) {
            continue;
        }
        bool duplicate = false;
        for (const auto& existing : normalized_legacy_auto_blocked) {
            if (existing == process_name) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            normalized_legacy_auto_blocked.push_back(process_name);
        }
    }
    const auto is_automatic_block = [&](std::wstring_view process_name) {
        for (const auto& automatic : normalized_legacy_auto_blocked) {
            if (automatic == process_name) {
                return true;
            }
        }
        return false;
    };
    for (const auto& process_name : normalized_legacy_blocked) {
        if (migrated.size() >= MAX_APP_INPUT_PROFILE_RULES) break;
        if (!FindAppInputProfileIndex(migrated, process_name).has_value()) {
            migrated.push_back({
                process_name, false, fallback,
                is_automatic_block(process_name)
                    ? AppInputProfileOrigin::Automatic
                    : AppInputProfileOrigin::Manual});
        }
    }
    for (const auto& legacy : legacy_typing_modes) {
        if (legacy.mode != 0 && legacy.mode != 1) {
            continue;
        }
        const std::wstring process_name = NormalizeProcessName(legacy.process_name);
        if (!IsValidAppProfileProcessName(process_name) ||
            FindAppInputProfileIndex(explicit_profiles, process_name).has_value()) {
            continue;
        }
        SetAppInputProfileEnabled(
            migrated, process_name, legacy.mode == 0, fallback,
            AppInputProfileOrigin::Automatic);
    }
    return migrated;
}

inline std::vector<AppInputProfile> MigrateAppInputProfiles(
    const std::vector<AppInputProfile>& new_profiles,
    const std::vector<std::wstring>& legacy_blocked_apps,
    const std::vector<LegacyAppTypingMode>& legacy_typing_modes,
    core::InputMethod global_method) {
    return MigrateAppInputProfiles(
        new_profiles, legacy_blocked_apps, {}, legacy_typing_modes,
        global_method);
}

inline std::vector<AppInputProfile> ResolveLoadedAppInputProfiles(
    bool authoritative_source_present,
    const std::vector<AppInputProfile>& persisted_profiles,
    const std::vector<std::wstring>& legacy_blocked_apps,
    const std::vector<std::wstring>& legacy_auto_blocked_apps,
    const std::vector<LegacyAppTypingMode>& legacy_typing_modes,
    core::InputMethod global_method) {
    if (authoritative_source_present) {
        return NormalizeAppInputProfiles(persisted_profiles);
    }
    return MigrateAppInputProfiles(
        {}, legacy_blocked_apps, legacy_auto_blocked_apps,
        legacy_typing_modes, global_method);
}

inline std::vector<std::wstring> DeriveLegacyBlockedApps(
    const std::vector<AppInputProfile>& profiles) {
    std::vector<std::wstring> blocked;
    for (const auto& profile : NormalizeAppInputProfiles(profiles)) {
        if (!profile.enabled) {
            blocked.push_back(profile.process_name);
        }
    }
    return blocked;
}

inline std::vector<std::wstring> DeriveLegacyAutoBlockedApps(
    const std::vector<AppInputProfile>& profiles) {
    std::vector<std::wstring> blocked;
    for (const auto& profile : NormalizeAppInputProfiles(profiles)) {
        if (!profile.enabled &&
            profile.origin == AppInputProfileOrigin::Automatic) {
            blocked.push_back(profile.process_name);
        }
    }
    return blocked;
}

inline bool SyncLegacyAppProfileViews(IMEConfig& config) {
    std::vector<std::wstring> blocked = DeriveLegacyBlockedApps(
        config.app_input_profiles);
    std::vector<std::wstring> automatic = DeriveLegacyAutoBlockedApps(
        config.app_input_profiles);
    const bool changed = blocked != config.blocked_apps ||
        automatic != config.auto_blocked_apps;
    config.blocked_apps = std::move(blocked);
    config.auto_blocked_apps = std::move(automatic);
    return changed;
}

inline bool CanUseAutomaticAppInputProfile(
    const IMEConfig& config,
    std::wstring_view process_name) {
    const std::wstring normalized = NormalizeProcessName(
        std::wstring(process_name));
    return config.enable_app_input_profiles &&
        config.enable_auto_app_input_profiles &&
        IsValidAppProfileProcessName(normalized);
}

inline bool ApplyAutomaticAppInputMode(
    IMEConfig& config,
    std::wstring_view process_name,
    AppInputMode mode) {
    const std::wstring normalized = NormalizeProcessName(
        std::wstring(process_name));
    const auto method = InputMethodForAppInputMode(mode);
    if (!IsValidAppProfileProcessName(normalized) ||
        (mode != AppInputMode::Off && !method.has_value())) {
        return false;
    }

    const std::vector<AppInputProfile> previous_profiles =
        config.app_input_profiles;
    UpsertAppInputMode(
        config.app_input_profiles, normalized, mode, config.input_method,
        AppInputProfileOrigin::Automatic);
    const auto applied = LookupAppInputProfile(
        config.app_input_profiles, normalized);
    if (!applied.has_value() ||
        AppInputModeForProfile(*applied) != mode ||
        applied->origin != AppInputProfileOrigin::Automatic) {
        return false;
    }
    const bool profile_changed =
        previous_profiles != config.app_input_profiles;
    return SyncLegacyAppProfileViews(config) || profile_changed;
}

inline bool RestoreAutomaticAppInputProfileOnActivate(
    IMEConfig& config,
    std::wstring_view process_name) {
    if (!CanUseAutomaticAppInputProfile(config, process_name)) {
        return false;
    }
    const auto profile = LookupAppInputProfile(
        config.app_input_profiles, process_name);
    if (!profile.has_value() || profile->enabled ||
        profile->origin != AppInputProfileOrigin::Automatic) {
        return false;
    }

    const std::vector<AppInputProfile> previous_profiles =
        config.app_input_profiles;
    SetAppInputProfileEnabled(
        config.app_input_profiles, process_name, true,
        config.input_method, AppInputProfileOrigin::Automatic);
    const bool profile_changed =
        previous_profiles != config.app_input_profiles;
    return SyncLegacyAppProfileViews(config) || profile_changed;
}

inline bool ShouldLearnAutomaticOffOnDeactivate(
    bool enable_app_input_profiles,
    bool enable_auto_app_input_profiles,
    bool activation_ready,
    bool process_valid,
    bool foreground_process_matches,
    bool foreground_thread_matches) noexcept {
    return enable_app_input_profiles && enable_auto_app_input_profiles &&
        activation_ready && process_valid && foreground_process_matches &&
        foreground_thread_matches;
}

inline bool LearnAutomaticOffOnDeactivate(
    IMEConfig& config,
    std::wstring_view process_name) {
    if (!CanUseAutomaticAppInputProfile(config, process_name)) {
        return false;
    }

    const auto existing = LookupAppInputProfile(
        config.app_input_profiles, process_name);
    if (existing.has_value() && !existing->enabled &&
        existing->origin == AppInputProfileOrigin::Manual) {
        return false;
    }

    return ApplyAutomaticAppInputMode(
        config, process_name, AppInputMode::Off);
}

enum class AppInputUpdateTarget : uint8_t {
    Global = 0,
    ExistingProfile = 1,
    AutomaticProfile = 2,
};

struct AppInputUpdateResult {
    AppInputUpdateTarget target = AppInputUpdateTarget::Global;
    bool changed = false;
};

inline AppInputUpdateTarget ResolveAppInputUpdateTarget(
    const IMEConfig& config,
    std::wstring_view process_name) {
    const std::wstring normalized = NormalizeProcessName(
        std::wstring(process_name));
    if (!config.enable_app_input_profiles ||
        !IsValidAppProfileProcessName(normalized)) {
        return AppInputUpdateTarget::Global;
    }
    if (LookupAppInputProfile(
            config.app_input_profiles, normalized).has_value()) {
        return AppInputUpdateTarget::ExistingProfile;
    }
    return config.enable_auto_app_input_profiles
        ? AppInputUpdateTarget::AutomaticProfile
        : AppInputUpdateTarget::Global;
}

inline AppInputUpdateResult ApplyUserSelectedInputMode(
    IMEConfig& config,
    std::wstring_view process_name,
    AppInputMode mode) {
    const AppInputUpdateTarget target = ResolveAppInputUpdateTarget(
        config, process_name);
    if (target == AppInputUpdateTarget::AutomaticProfile) {
        return {
            target,
            ApplyAutomaticAppInputMode(config, process_name, mode)};
    }
    if (target == AppInputUpdateTarget::ExistingProfile) {
        const auto existing = LookupAppInputProfile(
            config.app_input_profiles, process_name);
        if (!existing.has_value()) {
            return {target, false};
        }
        const auto method = InputMethodForAppInputMode(mode);
        if (mode != AppInputMode::Off && !method.has_value()) {
            return {target, false};
        }

        const AppInputProfileOrigin origin =
            config.enable_auto_app_input_profiles
            ? AppInputProfileOrigin::Automatic
            : existing->origin;
        const bool profile_changed = UpsertAppInputMode(
            config.app_input_profiles, process_name, mode,
            config.input_method, origin);
        return {
            target,
            SyncLegacyAppProfileViews(config) || profile_changed};
    }

    const auto method = InputMethodForAppInputMode(mode);
    if (mode != AppInputMode::Off && !method.has_value()) {
        return {target, false};
    }
    const DWORD previous_typing_mode = config.typing_mode;
    const core::InputMethod previous_method = config.input_method;
    if (mode == AppInputMode::Off) {
        config.typing_mode = 1;
    } else {
        config.typing_mode = 0;
        config.input_method = *method;
    }
    return {
        target,
        previous_typing_mode != config.typing_mode ||
            previous_method != config.input_method};
}

inline AppInputUpdateResult ToggleUserInputMode(
    IMEConfig& config,
    std::wstring_view process_name) {
    const AppInputUpdateTarget target = ResolveAppInputUpdateTarget(
        config, process_name);
    if (target != AppInputUpdateTarget::Global) {
        const ResolvedAppInputProfile effective =
            ResolveEffectiveAppInputProfile(
                true, config.app_input_profiles, process_name,
                config.typing_mode == 0, config.input_method);
        const AppInputMode next_mode = effective.enabled
            ? AppInputMode::Off
            : AppInputModeForMethod(effective.input_method);
        return ApplyUserSelectedInputMode(config, process_name, next_mode);
    }
    return ApplyUserSelectedInputMode(
        config, process_name,
        config.typing_mode == 0 ? AppInputMode::Off
                                : AppInputModeForMethod(config.input_method));
}

inline bool ResolveAppInputProfileSetting(
    std::optional<DWORD> new_value,
    std::optional<DWORD> legacy_value,
    bool default_value = true) noexcept {
    if (new_value.has_value() && *new_value <= 1) {
        return *new_value != 0;
    }
    if (legacy_value.has_value() && *legacy_value <= 1) {
        return *legacy_value != 0;
    }
    return default_value;
}

inline bool ResolveEnableAppInputProfiles(
    std::optional<DWORD> new_value,
    std::optional<DWORD> legacy_enable_app_blocklist,
    std::optional<DWORD> legacy_enable_auto_exclude) noexcept {
    const bool legacy_default = ResolveAppInputProfileSetting(
        std::nullopt, legacy_enable_auto_exclude, true);
    return ResolveAppInputProfileSetting(
        new_value, legacy_enable_app_blocklist, legacy_default);
}

inline bool IsBuiltInNativeBypassProcess(std::wstring_view process_name) {
    return NormalizeProcessName(std::wstring(process_name)) == L"taskmgr.exe";
}

inline bool ShouldTreatShellSurfaceAsNative(bool focused_win32_edit, bool native_surface_match) noexcept {
    return native_surface_match && !focused_win32_edit;
}

inline bool ShouldUseNotepadPlusPlusDirectInline(std::wstring_view process_name, std::wstring_view class_name) {
    if (NormalizeProcessName(std::wstring(process_name)) != L"notepad++.exe") {
        return false;
    }

    std::wstring normalized_class(class_name);
    for (wchar_t& c : normalized_class) {
        if (c >= L'A' && c <= L'Z') {
            c = c - L'A' + L'a';
        }
    }
    return normalized_class == L"edit" || normalized_class == L"scintilla";
}

inline bool ShouldCommitNotepadPlusPlusDirectInlineBoundary(
    std::wstring_view process_name,
    std::wstring_view class_name,
    wchar_t ch) {
    return ch == L' ' && ShouldUseNotepadPlusPlusDirectInline(process_name, class_name);
}

inline bool CanContinueScintillaDirectInline(
    bool has_inline,
    size_t inline_start,
    size_t selection_start,
    size_t selection_end) noexcept {
    return has_inline && selection_start == selection_end && selection_start >= inline_start;
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

inline std::vector<std::wstring> NormalizeDirectAppsList(const std::vector<std::wstring>& apps) {
    std::vector<std::wstring> normalized;
    for (const auto& app : apps) {
        std::wstring raw_app = app;
        std::wstring mode = L"inline";
        size_t colon = raw_app.find_last_of(L':');
        if (colon != std::wstring::npos && colon > 1) {
            mode = raw_app.substr(colon + 1);
            raw_app = raw_app.substr(0, colon);
        }
        
        std::wstring norm_name = NormalizeProcessName(raw_app);
        if (norm_name.empty()) continue;
        
        // Clean up mode
        for (wchar_t& c : mode) {
            if (c >= L'A' && c <= L'Z') {
                c = c - L'A' + L'a';
            }
        }
        while (!mode.empty() && (mode.front() == L' ' || mode.front() == L'\t')) mode.erase(0, 1);
        while (!mode.empty() && (mode.back() == L' ' || mode.back() == L'\t' || mode.back() == L'\r' || mode.back() == L'\n')) mode.pop_back();
        
        if (mode != L"commit") {
            mode = L"inline";
        }
        
        std::wstring entry = norm_name + L":" + mode;
        
        bool exists = false;
        for (const auto& existing : normalized) {
            size_t ext_colon = existing.find_last_of(L':');
            std::wstring ext_name = (ext_colon != std::wstring::npos) ? existing.substr(0, ext_colon) : existing;
            if (ext_name == norm_name) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            normalized.push_back(entry);
        }
    }
    return normalized;
}

inline std::vector<std::wstring> ParseDirectAppsListText(std::wstring_view text) {
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
    return NormalizeDirectAppsList(apps);
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

inline bool ContainsProcessName(const std::vector<std::wstring>& apps, std::wstring_view process_name) {
    std::wstring name = NormalizeProcessName(std::wstring(process_name));
    if (name.empty()) {
        return false;
    }
    for (const auto& app : apps) {
        if (app == name) {
            return true;
        }
    }
    return false;
}

inline bool EraseProcessName(std::vector<std::wstring>& apps, std::wstring_view process_name) {
    std::wstring name = NormalizeProcessName(std::wstring(process_name));
    if (name.empty()) {
        return false;
    }

    bool erased = false;
    std::vector<std::wstring> kept;
    kept.reserve(apps.size());
    for (const auto& app : apps) {
        if (app == name) {
            erased = true;
        } else {
            kept.push_back(app);
        }
    }
    apps = std::move(kept);
    return erased;
}

inline std::optional<std::vector<AppInputProfile>>
PrepareAppInputProfilesForSave(
    const std::vector<AppInputProfile>& profiles,
    const std::vector<std::wstring>& legacy_blocked_apps,
    const std::vector<std::wstring>& legacy_auto_blocked_apps,
    core::InputMethod global_method) {
    if (!profiles.empty()) {
        return NormalizeAppInputProfilesForPersistence(profiles);
    }

    std::vector<AppInputProfile> migrated;
    const std::vector<std::wstring> blocked = NormalizeProcessList(
        legacy_blocked_apps);
    const std::vector<std::wstring> automatic = NormalizeProcessList(
        legacy_auto_blocked_apps);
    if (blocked.size() > MAX_APP_INPUT_PROFILE_RULES ||
        automatic.size() > MAX_APP_INPUT_PROFILE_RULES) {
        return std::nullopt;
    }
    migrated.reserve(blocked.size());
    for (const auto& process_name : blocked) {
        if (!IsValidAppProfileProcessName(process_name)) {
            return std::nullopt;
        }
        migrated.push_back({
            process_name, false, NormalizeAppInputMethod(global_method),
            ContainsProcessName(automatic, process_name)
                ? AppInputProfileOrigin::Automatic
                : AppInputProfileOrigin::Manual});
    }
    return migrated;
}

inline std::optional<std::vector<AppInputProfile>>
PrepareAppInputProfilesForSave(
    const std::vector<AppInputProfile>& profiles,
    const std::vector<std::wstring>& legacy_blocked_apps,
    core::InputMethod global_method) {
    return PrepareAppInputProfilesForSave(
        profiles, legacy_blocked_apps, {}, global_method);
}

inline bool AutoExcludeApp(IMEConfig& config, std::wstring_view process_name) {
    if (!config.enable_auto_exclude) {
        return false;
    }

    std::wstring name = NormalizeProcessName(std::wstring(process_name));
    if (name.empty()) {
        return false;
    }

    std::vector<std::wstring> normalized_blocked = NormalizeProcessList(config.blocked_apps);
    std::vector<std::wstring> normalized_auto = NormalizeProcessList(config.auto_blocked_apps);
    bool changed = normalized_blocked != config.blocked_apps || normalized_auto != config.auto_blocked_apps;
    config.blocked_apps = std::move(normalized_blocked);
    config.auto_blocked_apps = std::move(normalized_auto);

    const auto existing = LookupAppInputProfile(
        config.app_input_profiles, name);
    if (existing.has_value() &&
        existing->origin == AppInputProfileOrigin::Manual &&
        !ContainsProcessName(config.blocked_apps, name)) {
        return changed;
    }

    if (ContainsProcessName(config.blocked_apps, name)) {
        const std::vector<AppInputProfile> previous_profiles =
            config.app_input_profiles;
        const bool automatic_owner = existing.has_value()
            ? existing->origin == AppInputProfileOrigin::Automatic
            : ContainsProcessName(config.auto_blocked_apps, name);
        if (automatic_owner &&
            !ContainsProcessName(config.auto_blocked_apps, name)) {
            config.auto_blocked_apps.push_back(name);
            changed = true;
        } else if (!automatic_owner) {
            changed = EraseProcessName(
                config.auto_blocked_apps, name) || changed;
        }
        SetAppInputProfileEnabled(
            config.app_input_profiles, name, false, config.input_method,
            automatic_owner
                ? AppInputProfileOrigin::Automatic
                : AppInputProfileOrigin::Manual);
        return changed || previous_profiles != config.app_input_profiles;
    }

    config.blocked_apps.push_back(name);
    SetAppInputProfileEnabled(
        config.app_input_profiles, name, false, config.input_method,
        AppInputProfileOrigin::Automatic);
    if (!ContainsProcessName(config.auto_blocked_apps, name)) {
        config.auto_blocked_apps.push_back(std::move(name));
    }
    return true;
}

inline bool AutoIncludeApp(IMEConfig& config, std::wstring_view process_name) {
    if (!config.enable_auto_exclude) {
        return false;
    }

    std::wstring name = NormalizeProcessName(std::wstring(process_name));
    if (name.empty()) {
        return false;
    }

    std::vector<std::wstring> normalized_blocked = NormalizeProcessList(config.blocked_apps);
    std::vector<std::wstring> normalized_auto = NormalizeProcessList(config.auto_blocked_apps);
    bool changed = normalized_blocked != config.blocked_apps || normalized_auto != config.auto_blocked_apps;
    config.blocked_apps = std::move(normalized_blocked);
    config.auto_blocked_apps = std::move(normalized_auto);

    const auto existing = LookupAppInputProfile(
        config.app_input_profiles, name);
    if (existing.has_value() &&
        existing->origin == AppInputProfileOrigin::Manual) {
        changed = EraseProcessName(
            config.auto_blocked_apps, name) || changed;
        return changed;
    }

    const bool automatic_owner = ContainsProcessName(
        config.auto_blocked_apps, name) ||
        (existing.has_value() &&
         existing->origin == AppInputProfileOrigin::Automatic);
    if (!automatic_owner) {
        if (ContainsProcessName(config.blocked_apps, name)) {
            const std::vector<AppInputProfile> previous_profiles =
                config.app_input_profiles;
            SetAppInputProfileEnabled(
                config.app_input_profiles, name, false,
                config.input_method, AppInputProfileOrigin::Manual);
            changed = previous_profiles != config.app_input_profiles || changed;
        }
        return changed;
    }

    changed = EraseProcessName(config.auto_blocked_apps, name) || changed;
    changed = EraseProcessName(config.blocked_apps, name) || changed;
    const std::vector<AppInputProfile> previous_profiles =
        config.app_input_profiles;
    SetAppInputProfileEnabled(
        config.app_input_profiles, name, true, config.input_method,
        AppInputProfileOrigin::Automatic);
    changed = previous_profiles != config.app_input_profiles || changed;
    return changed;
}

inline std::vector<std::wstring> PreserveAutoBlockedAppsForBlocklist(
    const std::vector<std::wstring>& auto_blocked_apps,
    const std::vector<std::wstring>& blocked_apps) {
    std::vector<std::wstring> normalized_blocked = NormalizeProcessList(blocked_apps);
    std::vector<std::wstring> preserved;
    for (const auto& app : NormalizeProcessList(auto_blocked_apps)) {
        if (ContainsProcessName(normalized_blocked, app)) {
            preserved.push_back(app);
        }
    }
    return preserved;
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

inline bool WriteMultiStringValue(HKEY hKey, const wchar_t* valueName, const std::vector<std::wstring>& values) {
    std::vector<std::wstring> normalized = NormalizeProcessList(values);
    std::vector<wchar_t> buffer;
    for (const auto& value : normalized) {
        buffer.insert(buffer.end(), value.begin(), value.end());
        buffer.push_back(L'\0');
    }
    if (normalized.empty()) {
        buffer.push_back(L'\0');
    }
    buffer.push_back(L'\0');

    return RegSetValueExW(
               hKey, valueName, 0, REG_MULTI_SZ,
               reinterpret_cast<const BYTE*>(buffer.data()),
               static_cast<DWORD>(buffer.size() * sizeof(wchar_t))) ==
        ERROR_SUCCESS;
}

inline std::optional<DWORD> ReadRegistryDword(HKEY hKey, const wchar_t* value_name) {
    DWORD type = 0;
    DWORD value = 0;
    DWORD size = sizeof(value);
    if (RegQueryValueExW(
            hKey, value_name, nullptr, &type,
            reinterpret_cast<LPBYTE>(&value), &size) != ERROR_SUCCESS ||
        type != REG_DWORD || size != sizeof(value)) {
        return std::nullopt;
    }
    return value;
}

inline std::optional<std::vector<std::wstring>> ReadBoundedRawMultiStringValue(
    HKEY hKey,
    const wchar_t* value_name) {
    DWORD type = 0;
    DWORD size = 0;
    if (RegQueryValueExW(hKey, value_name, nullptr, &type, nullptr, &size) !=
            ERROR_SUCCESS ||
        type != REG_MULTI_SZ || size < 2 * sizeof(wchar_t) ||
        size % sizeof(wchar_t) != 0 ||
        size > MAX_APP_INPUT_PROFILES_SERIALIZED_CHARS * sizeof(wchar_t)) {
        return std::nullopt;
    }

    std::vector<wchar_t> buffer(size / sizeof(wchar_t), L'\0');
    if (RegQueryValueExW(
            hKey, value_name, nullptr, &type,
            reinterpret_cast<LPBYTE>(buffer.data()), &size) != ERROR_SUCCESS ||
        type != REG_MULTI_SZ) {
        return std::nullopt;
    }

    const size_t char_count = size / sizeof(wchar_t);
    if (char_count < 2 || buffer[char_count - 1] != L'\0' ||
        buffer[char_count - 2] != L'\0') {
        return std::nullopt;
    }

    std::vector<std::wstring> records;
    size_t offset = 0;
    while (offset < char_count - 1 && buffer[offset] != L'\0') {
        size_t end = offset;
        while (end < char_count && buffer[end] != L'\0') {
            ++end;
        }
        if (end == char_count) {
            return std::nullopt;
        }
        records.emplace_back(buffer.data() + offset, end - offset);
        if (records.size() > MAX_APP_INPUT_PROFILE_RULES + 1) {
            return std::nullopt;
        }
        offset = end + 1;
    }
    return records;
}

inline bool WriteRawMultiStringValue(
    HKEY hKey,
    const wchar_t* value_name,
    const std::vector<std::wstring>& values) {
    const auto total_chars = RawMultiStringCharCount(values);
    if (!total_chars.has_value() ||
        *total_chars > MAX_APP_INPUT_PROFILES_SERIALIZED_CHARS) {
        return false;
    }

    std::vector<wchar_t> buffer;
    buffer.reserve(*total_chars);
    for (const auto& value : values) {
        buffer.insert(buffer.end(), value.begin(), value.end());
        buffer.push_back(L'\0');
    }
    if (values.empty()) {
        buffer.push_back(L'\0');
    }
    buffer.push_back(L'\0');
    if (buffer.size() != *total_chars) {
        return false;
    }
    return RegSetValueExW(
               hKey, value_name, 0, REG_MULTI_SZ,
               reinterpret_cast<const BYTE*>(buffer.data()),
               static_cast<DWORD>(buffer.size() * sizeof(wchar_t))) == ERROR_SUCCESS;
}

inline bool WriteAppInputProfilesToRegistry(
    HKEY hKey,
    const std::vector<AppInputProfile>& profiles) {
    const AppInputProfilesSerializeResult serialized =
        SerializeAppInputProfiles(profiles);
    if (!serialized.success ||
        !WriteRawMultiStringValue(
            hKey, REG_VAL_APP_INPUT_PROFILES, serialized.records)) {
        return false;
    }
    if (!WriteMultiStringValue(
            hKey, REG_VAL_BLOCKED_APPS,
            DeriveLegacyBlockedApps(profiles))) {
        return false;
    }
    return WriteMultiStringValue(
        hKey, REG_VAL_AUTO_BLOCKED_APPS,
        DeriveLegacyAutoBlockedApps(profiles));
}

inline std::vector<LegacyAppTypingMode> EnumerateLegacyAppTypingModes(HKEY hKey) {
    std::vector<LegacyAppTypingMode> modes;
    constexpr size_t value_name_capacity =
        MAX_APP_INPUT_PROFILE_PROCESS_NAME_CHARS + 64;
    const std::wstring_view prefix(REG_APP_TYPING_MODE_PREFIX);

    for (DWORD index = 0;
         index < MAX_LEGACY_APP_TYPING_VALUES_SCANNED;
         ++index) {
        wchar_t value_name[value_name_capacity] = {};
        DWORD value_name_length = static_cast<DWORD>(value_name_capacity);
        DWORD type = 0;
        DWORD value = 0;
        DWORD value_size = sizeof(value);
        const LONG status = RegEnumValueW(
            hKey, index, value_name, &value_name_length, nullptr, &type,
            reinterpret_cast<LPBYTE>(&value), &value_size);
        if (status == ERROR_NO_MORE_ITEMS) {
            break;
        }
        if (status != ERROR_SUCCESS || type != REG_DWORD ||
            value_size != sizeof(value) || value_name_length <= prefix.length()) {
            continue;
        }

        const std::wstring_view full_name(value_name, value_name_length);
        if (full_name.substr(0, prefix.length()) != prefix) {
            continue;
        }
        std::wstring process_name = NormalizeProcessName(
            std::wstring(full_name.substr(prefix.length())));
        if (!IsValidAppProfileProcessName(process_name)) {
            continue;
        }
        modes.push_back({std::move(process_name), value});
        if (modes.size() >= MAX_APP_INPUT_PROFILE_RULES) {
            break;
        }
    }
    return modes;
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
        DWORD dwCorrectionLevel = 1; // Default to Normal (1)
        dwSize = sizeof(DWORD);
        if (RegQueryValueExW(hKey, REG_VAL_CORRECTION_LEVEL, nullptr, &dwType, reinterpret_cast<LPBYTE>(&dwCorrectionLevel), &dwSize) == ERROR_SUCCESS) {
            config.auto_correct_level = NormalizeCorrectionLevelValue(dwCorrectionLevel);
            config.enable_auto_correct = (config.auto_correct_level != CorrectionLevel::Off);
        } else {
            DWORD dwAutoCorrect = 1;
            dwSize = sizeof(DWORD);
            if (RegQueryValueExW(hKey, REG_VAL_AUTO_CORRECT, nullptr, &dwType, reinterpret_cast<LPBYTE>(&dwAutoCorrect), &dwSize) == ERROR_SUCCESS) {
                config.enable_auto_correct = (dwAutoCorrect != 0);
                config.auto_correct_level = config.enable_auto_correct ? CorrectionLevel::Normal : CorrectionLevel::Off;
            }
        }
        std::optional<DWORD> englishProtectionLevel;
        DWORD dwEnglishProtectionLevel = 1;
        dwSize = sizeof(DWORD);
        if (RegQueryValueExW(hKey, REG_VAL_ENGLISH_PROTECTION_LEVEL, nullptr, &dwType, reinterpret_cast<LPBYTE>(&dwEnglishProtectionLevel), &dwSize) == ERROR_SUCCESS) {
            englishProtectionLevel = dwEnglishProtectionLevel;
        }
        std::optional<DWORD> legacyEnglishProtection;
        DWORD dwEnableEnglishProtection = 1;
        dwSize = sizeof(DWORD);
        if (RegQueryValueExW(hKey, REG_VAL_ENABLE_ENGLISH_PROTECTION, nullptr, &dwType, reinterpret_cast<LPBYTE>(&dwEnableEnglishProtection), &dwSize) == ERROR_SUCCESS) {
            legacyEnglishProtection = dwEnableEnglishProtection;
        }
        config.english_protection_level = ResolveEnglishProtectionLevel(
            englishProtectionLevel, legacyEnglishProtection);
        config.fuzzy_input_flags = NormalizeFuzzyInputFlags(
            ReadRegistryDword(hKey, REG_VAL_FUZZY_INPUT_FLAGS).value_or(0));
        config.enable_fuzzy_input = IsFuzzyInputEffectivelyEnabled(
            ReadRegistryDword(hKey, REG_VAL_ENABLE_FUZZY_INPUT).value_or(0) != 0,
            config.fuzzy_input_flags);
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
        config.enable_smart_undo = ResolveSmartUndoEnabled(
            ReadRegistryDword(hKey, REG_VAL_ENABLE_SMART_UNDO));
        config.enable_smart_context_protection =
            ResolveSmartContextProtectionEnabled(ReadRegistryDword(
                hKey, REG_VAL_ENABLE_SMART_CONTEXT_PROTECTION));
        config.enable_auto_word_segmentation =
            NormalizeAutoWordSegmentationEnabled(
                ResolveAutoWordSegmentationEnabled(ReadRegistryDword(
                    hKey, REG_VAL_ENABLE_AUTO_WORD_SEGMENTATION)),
                config.auto_correct_level);
        DWORD dwEnableAutoCapitalize = 0;
        dwSize = sizeof(DWORD);
        if (RegQueryValueExW(hKey, REG_VAL_ENABLE_AUTO_CAPITALIZE, nullptr, &dwType, reinterpret_cast<LPBYTE>(&dwEnableAutoCapitalize), &dwSize) == ERROR_SUCCESS) {
            config.enable_auto_capitalize = (dwEnableAutoCapitalize != 0);
        }
        const std::optional<DWORD> legacyEnableAppBlocklist =
            ReadRegistryDword(hKey, REG_VAL_ENABLE_APP_BLOCKLIST);
        if (legacyEnableAppBlocklist.has_value()) {
            config.enable_app_blocklist = (*legacyEnableAppBlocklist != 0);
        }
        DWORD dwBlockedAppsType = 0;
        DWORD dwBlockedAppsSize = 0;
        if (RegQueryValueExW(hKey, REG_VAL_BLOCKED_APPS, nullptr, &dwBlockedAppsType, nullptr, &dwBlockedAppsSize) == ERROR_SUCCESS &&
            dwBlockedAppsType == REG_MULTI_SZ) {
            config.blocked_apps = ReadMultiStringValue(hKey, REG_VAL_BLOCKED_APPS);
        }
        const std::optional<DWORD> legacyEnableAutoExclude =
            ReadRegistryDword(hKey, REG_VAL_ENABLE_AUTO_EXCLUDE);
        if (legacyEnableAutoExclude.has_value()) {
            config.enable_auto_exclude = (*legacyEnableAutoExclude != 0);
        }
        DWORD dwAutoBlockedAppsType = 0;
        DWORD dwAutoBlockedAppsSize = 0;
        if (RegQueryValueExW(hKey, REG_VAL_AUTO_BLOCKED_APPS, nullptr, &dwAutoBlockedAppsType, nullptr, &dwAutoBlockedAppsSize) == ERROR_SUCCESS &&
            dwAutoBlockedAppsType == REG_MULTI_SZ) {
            config.auto_blocked_apps = ReadMultiStringValue(hKey, REG_VAL_AUTO_BLOCKED_APPS);
        }
        config.enable_app_input_profiles = ResolveAppInputProfileSetting(
            ReadRegistryDword(hKey, REG_VAL_ENABLE_APP_INPUT_PROFILES),
            legacyEnableAppBlocklist, true);
        config.enable_auto_app_input_profiles = ResolveAppInputProfileSetting(
            ReadRegistryDword(
                hKey, REG_VAL_ENABLE_AUTO_APP_INPUT_PROFILES),
            legacyEnableAutoExclude, true);

        bool authoritativeProfilesPresent = false;
        std::vector<AppInputProfile> persistedProfiles;
        const auto persistedProfileRecords =
            ReadBoundedRawMultiStringValue(hKey, REG_VAL_APP_INPUT_PROFILES);
        if (persistedProfileRecords.has_value()) {
            AppInputProfilesParseResult parsedProfiles =
                ParseAppInputProfiles(*persistedProfileRecords);
            if (parsedProfiles.schema_valid &&
                !parsedProfiles.limit_exceeded &&
                parsedProfiles.invalid_records == 0) {
                authoritativeProfilesPresent = true;
                persistedProfiles = std::move(parsedProfiles.profiles);
            }
        }
        config.app_input_profiles = ResolveLoadedAppInputProfiles(
            authoritativeProfilesPresent,
            persistedProfiles,
            config.blocked_apps,
            config.auto_blocked_apps,
            EnumerateLegacyAppTypingModes(hKey),
            config.input_method);
        config.blocked_apps = DeriveLegacyBlockedApps(
            config.app_input_profiles);
        config.auto_blocked_apps = DeriveLegacyAutoBlockedApps(
            config.app_input_profiles);
        DWORD dwDirectAppsType = 0;
        DWORD dwDirectAppsSize = 0;
        if (RegQueryValueExW(hKey, REG_VAL_DIRECT_APPS, nullptr, &dwDirectAppsType, nullptr, &dwDirectAppsSize) == ERROR_SUCCESS &&
            dwDirectAppsType == REG_MULTI_SZ) {
            config.direct_apps = ReadMultiStringValue(hKey, REG_VAL_DIRECT_APPS);
        }
        DWORD dwTypingMode = 0;
        dwSize = sizeof(DWORD);
        if (RegQueryValueExW(hKey, REG_VAL_TYPING_MODE, nullptr, &dwType, reinterpret_cast<LPBYTE>(&dwTypingMode), &dwSize) == ERROR_SUCCESS) {
            config.typing_mode = dwTypingMode;
        }
        DWORD dwHotkeyMode = 0;
        dwSize = sizeof(DWORD);
        if (RegQueryValueExW(hKey, REG_VAL_HOTKEY_MODE, nullptr, &dwType, reinterpret_cast<LPBYTE>(&dwHotkeyMode), &dwSize) == ERROR_SUCCESS) {
            config.hotkey_mode = dwHotkeyMode;
        }
        RegCloseKey(hKey);
    }

    // Check if auto-start is enabled
    config.enable_auto_start = false;
    HKEY hRunKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ, &hRunKey) == ERROR_SUCCESS) {
        if (RegQueryValueExW(hRunKey, L"Neokey", nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
            config.enable_auto_start = true;
        }
        RegCloseKey(hRunKey);
    }

    return config;
}

inline bool WriteRegistryDwordValue(
    HKEY key, const wchar_t* value_name, DWORD value) {
    return RegSetValueExW(
               key, value_name, 0, REG_DWORD,
               reinterpret_cast<const BYTE*>(&value), sizeof(value)) ==
        ERROR_SUCCESS;
}

inline bool SaveConfigToRegistry(
    const IMEConfig& config,
    bool update_auto_start = false) {
    HKEY hKey = nullptr;
    if (RegCreateKeyExW(
            HKEY_CURRENT_USER, REG_KEY_PATH, 0, nullptr,
            REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey,
            nullptr) != ERROR_SUCCESS) {
        return false;
    }

    bool success = true;
    DWORD dwInputMethod = 0;
    if (config.input_method == core::InputMethod::SimpleTelex) {
        dwInputMethod = 1;
    } else if (config.input_method == core::InputMethod::VNI) {
        dwInputMethod = 2;
    }
    success = WriteRegistryDwordValue(
                  hKey, REG_VAL_INPUT_METHOD, dwInputMethod) && success;

    const CorrectionLevel normalizedCorrectionLevel =
        NormalizeCorrectionLevelValue(
            static_cast<DWORD>(config.auto_correct_level));
    const DWORD dwAutoCorrect =
        normalizedCorrectionLevel != CorrectionLevel::Off ? 1 : 0;
    const DWORD dwCorrectionLevel =
        static_cast<DWORD>(normalizedCorrectionLevel);
    success = WriteRegistryDwordValue(
                  hKey, REG_VAL_AUTO_CORRECT, dwAutoCorrect) && success;
    success = WriteRegistryDwordValue(
                  hKey, REG_VAL_CORRECTION_LEVEL, dwCorrectionLevel) &&
        success;

    const EnglishProtectionLevel normalizedEnglishProtection =
        NormalizeEnglishProtectionLevelValue(
            static_cast<DWORD>(config.english_protection_level));
    const DWORD dwEnglishProtectionLevel =
        static_cast<DWORD>(normalizedEnglishProtection);
    const DWORD dwEnableEnglishProtection =
        normalizedEnglishProtection != EnglishProtectionLevel::Off ? 1 : 0;
    success = WriteRegistryDwordValue(
                  hKey, REG_VAL_ENGLISH_PROTECTION_LEVEL,
                  dwEnglishProtectionLevel) && success;
    success = WriteRegistryDwordValue(
                  hKey, REG_VAL_ENABLE_ENGLISH_PROTECTION,
                  dwEnableEnglishProtection) && success;

    const DWORD normalizedFuzzyInputFlags =
        NormalizeFuzzyInputFlags(config.fuzzy_input_flags);
    success = WriteRegistryDwordValue(
                  hKey, REG_VAL_ENABLE_FUZZY_INPUT,
                  IsFuzzyInputEffectivelyEnabled(
                      config.enable_fuzzy_input, normalizedFuzzyInputFlags)
                      ? 1u
                      : 0u) && success;
    success = WriteRegistryDwordValue(
                  hKey, REG_VAL_FUZZY_INPUT_FLAGS,
                  normalizedFuzzyInputFlags) && success;

    const auto write_bool = [&](const wchar_t* name, bool enabled) {
        success = WriteRegistryDwordValue(
                      hKey, name, enabled ? 1u : 0u) && success;
    };
    write_bool(REG_VAL_ENABLE_LOG, config.enable_log);
    write_bool(REG_VAL_ENABLE_SHORTHAND, config.enable_shorthand);
    success = WriteRegistryDwordValue(
                  hKey, REG_VAL_ENABLE_SMART_UNDO,
                  SmartUndoEnabledToRegistryValue(
                      config.enable_smart_undo)) && success;
    success = WriteRegistryDwordValue(
                  hKey, REG_VAL_ENABLE_SMART_CONTEXT_PROTECTION,
                  SmartContextProtectionEnabledToRegistryValue(
                      config.enable_smart_context_protection)) && success;
    success = WriteRegistryDwordValue(
                  hKey, REG_VAL_ENABLE_AUTO_WORD_SEGMENTATION,
                  AutoWordSegmentationEnabledToRegistryValue(
                      NormalizeAutoWordSegmentationEnabled(
                          config.enable_auto_word_segmentation,
                          normalizedCorrectionLevel))) && success;
    write_bool(
        REG_VAL_ENABLE_AUTO_CAPITALIZE,
        config.enable_auto_capitalize);
    write_bool(REG_VAL_ENABLE_APP_BLOCKLIST, config.enable_app_blocklist);
    write_bool(REG_VAL_ENABLE_AUTO_EXCLUDE, config.enable_auto_exclude);
    write_bool(
        REG_VAL_ENABLE_APP_INPUT_PROFILES,
        config.enable_app_input_profiles);
    write_bool(
        REG_VAL_ENABLE_AUTO_APP_INPUT_PROFILES,
        config.enable_auto_app_input_profiles);

    const auto profilesToSave = PrepareAppInputProfilesForSave(
        config.app_input_profiles, config.blocked_apps,
        config.auto_blocked_apps, config.input_method);
    success = profilesToSave.has_value() && success;
    if (profilesToSave.has_value()) {
        success = WriteAppInputProfilesToRegistry(
                      hKey, *profilesToSave) && success;
    }
    success = WriteMultiStringValue(
                  hKey, REG_VAL_DIRECT_APPS, config.direct_apps) && success;
    success = WriteRegistryDwordValue(
                  hKey, REG_VAL_TYPING_MODE, config.typing_mode) && success;
    success = WriteRegistryDwordValue(
                  hKey, REG_VAL_HOTKEY_MODE, config.hotkey_mode) && success;

    // Publish the revision last so readers do not intentionally reload a
    // partially written configuration.
    if (success) {
        const ULONGLONG revision = GetTickCount64();
        success = RegSetValueExW(
                      hKey, REG_VAL_CONFIG_REVISION, 0, REG_QWORD,
                      reinterpret_cast<const BYTE*>(&revision),
                      sizeof(revision)) == ERROR_SUCCESS;
    }
    success = RegCloseKey(hKey) == ERROR_SUCCESS && success;
    if (!success) {
        return false;
    }

    // The IME DLL runs inside arbitrary host processes. Only the standalone
    // config app may derive its executable path and update the Run entry.
    if (!update_auto_start) {
        return true;
    }

    // Save auto-start separately under the standard per-user Run key.
    HKEY hRunKey = nullptr;
    if (RegCreateKeyExW(
            HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
            0, nullptr, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr,
            &hRunKey, nullptr) != ERROR_SUCCESS) {
        return false;
    }

    bool run_success = true;
    if (config.enable_auto_start) {
        wchar_t path[MAX_PATH]{};
        const DWORD path_length =
            GetModuleFileNameW(nullptr, path, MAX_PATH);
        if (path_length == 0 || path_length >= MAX_PATH) {
            run_success = false;
        } else {
            const std::wstring run_command =
                L"\"" + std::wstring(path, path_length) + L"\" -silent";
            run_success = RegSetValueExW(
                              hRunKey, L"Neokey", 0, REG_SZ,
                              reinterpret_cast<const BYTE*>(
                                  run_command.c_str()),
                              static_cast<DWORD>(
                                  (run_command.length() + 1) *
                                  sizeof(wchar_t))) == ERROR_SUCCESS;
        }
    } else {
        const LONG delete_status = RegDeleteValueW(hRunKey, L"Neokey");
        run_success = delete_status == ERROR_SUCCESS ||
            delete_status == ERROR_FILE_NOT_FOUND;
    }
    return RegCloseKey(hRunKey) == ERROR_SUCCESS && run_success;
}

inline bool SaveBlocklistConfigToRegistry(const IMEConfig& config) {
    HKEY hKey;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, REG_KEY_PATH, 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr) != ERROR_SUCCESS) {
        return false;
    }

    const auto profiles_to_save =
        PrepareAppInputProfilesForSave(
            config.app_input_profiles, config.blocked_apps,
            config.auto_blocked_apps,
            config.input_method);
    bool success = profiles_to_save.has_value() &&
        WriteAppInputProfilesToRegistry(hKey, *profiles_to_save);
    ULONGLONG revision = GetTickCount64();
    success = RegSetValueExW(
        hKey, REG_VAL_CONFIG_REVISION, 0, REG_QWORD,
        reinterpret_cast<const BYTE*>(&revision), sizeof(revision)) ==
        ERROR_SUCCESS && success;
    RegCloseKey(hKey);
    return success;
}

inline void TouchConfigRevision() {
    HKEY hKey;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, REG_KEY_PATH, 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
        ULONGLONG revision = GetTickCount64();
        RegSetValueExW(hKey, REG_VAL_CONFIG_REVISION, 0, REG_QWORD, reinterpret_cast<const BYTE*>(&revision), sizeof(revision));
        RegCloseKey(hKey);
    }
}

inline std::wstring BuildUserShorthandFilePath(std::wstring_view localAppData) {
    if (localAppData.empty()) {
        return L"";
    }

    std::wstring path(localAppData);
    if (path.back() != L'\\' && path.back() != L'/') {
        path.push_back(L'\\');
    }
    path += L"Neokey\\";
    path += SHORTHAND_FILE_NAME;
    return path;
}

inline std::wstring GetLegacyShorthandFilePath(HINSTANCE hInst = nullptr) {
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

inline std::wstring GetShorthandFilePath(HINSTANCE hInst = nullptr) {
    const DWORD required =
        GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    if (required > 1 && required <= 32768) {
        std::wstring local_app_data(required, L'\0');
        const DWORD length = GetEnvironmentVariableW(
            L"LOCALAPPDATA", local_app_data.data(), required);
        if (length > 0 && length < required) {
            local_app_data.resize(length);
            return BuildUserShorthandFilePath(local_app_data);
        }
    }
    return GetLegacyShorthandFilePath(hInst);
}

} // namespace vn_ime
