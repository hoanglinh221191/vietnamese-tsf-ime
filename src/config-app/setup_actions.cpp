#include "setup_actions.hpp"

#include <aclapi.h>
#include <hstring.h>
#include <sddl.h>
#include <shellapi.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <utility>

#include "tray_ipc.hpp"

namespace vn_ime::setup {
namespace {

constexpr wchar_t kSettingsKey[] = L"Software\\Neokey";
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kUserProfileKey[] = L"Control Panel\\International\\User Profile";
constexpr wchar_t kSubstitutesKey[] = L"Keyboard Layout\\Substitutes";
constexpr wchar_t kPreloadKey[] = L"Keyboard Layout\\Preload";
constexpr wchar_t kCtfOrderKey[] = L"Software\\Microsoft\\CTF\\SortOrder\\Language";
constexpr wchar_t kVietnameseLayout[] = L"0000042a";
constexpr wchar_t kUsLayout[] = L"00000409";

// What Neokey remembers about Vietnamese from before it first changed it, so
// the uninstall can put it back. Absent means an older build recorded nothing;
// an empty string means there was nothing there.
constexpr wchar_t kRecordedTips[] = L"PreviousVietnameseInputMethods";
constexpr wchar_t kRecordedAddedLanguage[] = L"AddedVietnameseLanguage";
constexpr wchar_t kRecordedSubstitute[] = L"PreviousVietnameseLayoutSubstitute";

// The package asks two groups of app sandboxes to read it: every AppContainer
// app, and the restricted ones (Edge, the new Notepad).
constexpr wchar_t kAllAppPackages[] = L"S-1-15-2-1";
constexpr wchar_t kAllRestrictedAppPackages[] = L"S-1-15-2-2";

std::wstring HexCode(DWORD value) {
    wchar_t text[16] = {};
    swprintf_s(text, L"0x%08lX", value);
    return text;
}

std::wstring EnvironmentValue(const wchar_t* name) {
    const DWORD length = GetEnvironmentVariableW(name, nullptr, 0);
    if (length == 0) {
        return {};
    }
    std::wstring value(length, L'\0');
    value.resize(GetEnvironmentVariableW(name, value.data(), length));
    return value;
}

std::wstring ExecutablePath() {
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (length == 0) {
            return {};
        }
        if (length < path.size()) {
            path.resize(length);
            return path;
        }
        path.resize(path.size() * 2);
    }
}

// A long spelling of a path that may have come in 8.3 form, so it compares
// against TEMP and OneDrive the way a person would read it.
std::wstring LongPath(const std::wstring& path) {
    if (path.empty()) {
        return path;
    }
    const DWORD length = GetLongPathNameW(path.c_str(), nullptr, 0);
    if (length == 0) {
        return path;
    }
    std::wstring result(length, L'\0');
    const DWORD written = GetLongPathNameW(path.c_str(), result.data(), length);
    if (written == 0 || written >= length) {
        return path;
    }
    result.resize(written);
    return result;
}

bool FileExists(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

// ---------------------------------------------------------------------------
// Registry
// ---------------------------------------------------------------------------

class Key {
public:
    Key() = default;
    ~Key() { Close(); }
    Key(const Key&) = delete;
    Key& operator=(const Key&) = delete;

    bool Open(HKEY root, const std::wstring& path, REGSAM access) {
        Close();
        return RegOpenKeyExW(root, path.c_str(), 0, access | KEY_WOW64_64KEY, &key_) == ERROR_SUCCESS;
    }
    bool Create(HKEY root, const std::wstring& path, REGSAM access) {
        Close();
        return RegCreateKeyExW(root, path.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, access | KEY_WOW64_64KEY,
                               nullptr, &key_, nullptr) == ERROR_SUCCESS;
    }
    void Close() {
        if (key_) {
            RegCloseKey(key_);
            key_ = nullptr;
        }
    }
    HKEY get() const noexcept { return key_; }
    explicit operator bool() const noexcept { return key_ != nullptr; }

private:
    HKEY key_ = nullptr;
};

std::optional<std::wstring> GetString(HKEY root, const std::wstring& path, const wchar_t* name) {
    Key key;
    if (!key.Open(root, path, KEY_QUERY_VALUE)) {
        return std::nullopt;
    }
    DWORD type = 0;
    DWORD size = 0;
    if (RegQueryValueExW(key.get(), name, nullptr, &type, nullptr, &size) != ERROR_SUCCESS ||
        (type != REG_SZ && type != REG_EXPAND_SZ)) {
        return std::nullopt;
    }
    std::wstring value(size / sizeof(wchar_t) + 1, L'\0');
    DWORD bytes = static_cast<DWORD>(value.size() * sizeof(wchar_t));
    if (RegQueryValueExW(key.get(), name, nullptr, &type, reinterpret_cast<BYTE*>(value.data()), &bytes) !=
        ERROR_SUCCESS) {
        return std::nullopt;
    }
    value.resize(bytes / sizeof(wchar_t));
    while (!value.empty() && value.back() == L'\0') {
        value.pop_back();
    }
    return value;
}

std::optional<DWORD> GetNumber(HKEY root, const std::wstring& path, const wchar_t* name) {
    Key key;
    if (!key.Open(root, path, KEY_QUERY_VALUE)) {
        return std::nullopt;
    }
    DWORD type = 0;
    DWORD value = 0;
    DWORD size = sizeof(value);
    if (RegQueryValueExW(key.get(), name, nullptr, &type, reinterpret_cast<BYTE*>(&value), &size) == ERROR_SUCCESS &&
        type == REG_DWORD) {
        return value;
    }
    const auto text = GetString(root, path, name);
    if (!text || TrimWhitespace(*text).empty()) {
        return std::nullopt;
    }
    return static_cast<DWORD>(std::wcstoul(text->c_str(), nullptr, 10));
}

bool ValuePresent(HKEY root, const std::wstring& path, const wchar_t* name) {
    Key key;
    return key.Open(root, path, KEY_QUERY_VALUE) &&
           RegQueryValueExW(key.get(), name, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
}

bool SetString(HKEY root, const std::wstring& path, const wchar_t* name, const std::wstring& value) {
    Key key;
    if (!key.Create(root, path, KEY_SET_VALUE)) {
        return false;
    }
    return RegSetValueExW(key.get(), name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                          static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
}

bool SetNumber(HKEY root, const std::wstring& path, const wchar_t* name, DWORD value) {
    Key key;
    if (!key.Create(root, path, KEY_SET_VALUE)) {
        return false;
    }
    return RegSetValueExW(key.get(), name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value)) ==
           ERROR_SUCCESS;
}

// True when the value is gone afterwards, whether or not it was there.
bool DeleteValue(HKEY root, const std::wstring& path, const wchar_t* name) {
    Key key;
    if (!key.Open(root, path, KEY_SET_VALUE)) {
        return true;
    }
    const LSTATUS status = RegDeleteValueW(key.get(), name);
    return status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND;
}

bool KeyPresent(HKEY root, const std::wstring& path) {
    Key key;
    return key.Open(root, path, KEY_QUERY_VALUE);
}

// True when the key is gone afterwards.
bool DeleteKeyTree(HKEY root, const std::wstring& path) {
    if (!KeyPresent(root, path)) {
        return true;
    }
    const size_t slash = path.find_last_of(L'\\');
    const std::wstring parent = slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
    const std::wstring leaf = slash == std::wstring::npos ? path : path.substr(slash + 1);
    Key parent_key;
    if (!parent_key.Open(root, parent, KEY_ALL_ACCESS)) {
        return false;
    }
    const LSTATUS status = RegDeleteTreeW(parent_key.get(), leaf.c_str());
    if (status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND) {
        return false;
    }
    RegDeleteKeyExW(parent_key.get(), leaf.c_str(), KEY_WOW64_64KEY, 0);
    return !KeyPresent(root, path);
}

struct OrderEntry {
    std::wstring name;
    unsigned long index = 0;
    std::wstring value;
};

std::vector<OrderEntry> ReadOrder(HKEY root, const std::wstring& path) {
    std::vector<OrderEntry> entries;
    Key key;
    if (!key.Open(root, path, KEY_QUERY_VALUE)) {
        return entries;
    }
    for (DWORD index = 0;; ++index) {
        wchar_t name[256] = {};
        DWORD name_length = static_cast<DWORD>(std::size(name));
        wchar_t data[256] = {};
        DWORD data_length = sizeof(data) - sizeof(wchar_t);
        DWORD type = 0;
        const LSTATUS status = RegEnumValueW(key.get(), index, name, &name_length, nullptr, &type,
                                             reinterpret_cast<BYTE*>(data), &data_length);
        if (status == ERROR_NO_MORE_ITEMS) {
            break;
        }
        if (status != ERROR_SUCCESS || name_length == 0 || name_length > 8 || type != REG_SZ) {
            continue;
        }
        wchar_t* end = nullptr;
        const unsigned long order = std::wcstoul(name, &end, 16);
        if (end == nullptr || *end != L'\0' || name[0] == L'-' || name[0] == L'+') {
            continue;
        }
        entries.push_back({name, order, ToLowerAscii(TrimWhitespace(data))});
    }
    std::stable_sort(entries.begin(), entries.end(),
                     [](const OrderEntry& left, const OrderEntry& right) { return left.index < right.index; });
    return entries;
}

std::vector<std::wstring> OrderValues(const std::vector<OrderEntry>& entries) {
    std::vector<std::wstring> values;
    for (const OrderEntry& entry : entries) {
        values.push_back(entry.value);
    }
    return values;
}

// Preload numbers its entries from 1, CTF from 0 with 8 hex digits. Names it
// did not write are removed, since deduplicating can shorten the list.
bool WriteOrder(HKEY root, const std::wstring& path, const std::vector<std::wstring>& values, bool preload_style) {
    const std::vector<OrderEntry> existing = ReadOrder(root, path);
    std::vector<std::wstring> written;
    for (size_t index = 0; index < values.size(); ++index) {
        wchar_t name[16] = {};
        if (preload_style) {
            swprintf_s(name, L"%zu", index + 1);
        } else {
            swprintf_s(name, L"%08zx", index);
        }
        if (!SetString(root, path, name, values[index])) {
            return false;
        }
        written.push_back(name);
    }
    for (const OrderEntry& entry : existing) {
        if (std::find(written.begin(), written.end(), entry.name) == written.end()) {
            DeleteValue(root, path, entry.name.c_str());
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Access for app sandboxes
// ---------------------------------------------------------------------------

bool ApplyAccess(const std::wstring& object, SE_OBJECT_TYPE type, const wchar_t* sid_text, DWORD access,
                 DWORD inheritance, ACCESS_MODE mode, std::wstring& error) {
    PSID sid = nullptr;
    if (!ConvertStringSidToSidW(sid_text, &sid)) {
        error = L"cannot read SID " + std::wstring(sid_text);
        return false;
    }
    PACL old_dacl = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    DWORD status = GetNamedSecurityInfoW(object.c_str(), type, DACL_SECURITY_INFORMATION, nullptr, nullptr,
                                         &old_dacl, nullptr, &descriptor);
    PACL new_dacl = nullptr;
    if (status == ERROR_SUCCESS) {
        EXPLICIT_ACCESSW entry{};
        entry.grfAccessPermissions = access;
        entry.grfAccessMode = mode;
        entry.grfInheritance = inheritance;
        entry.Trustee.TrusteeForm = TRUSTEE_IS_SID;
        entry.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
        entry.Trustee.ptstrName = static_cast<LPWSTR>(sid);
        status = SetEntriesInAclW(1, &entry, old_dacl, &new_dacl);
    }
    if (status == ERROR_SUCCESS) {
        status = SetNamedSecurityInfoW(const_cast<LPWSTR>(object.c_str()), type, DACL_SECURITY_INFORMATION, nullptr,
                                       nullptr, new_dacl, nullptr);
    }
    if (new_dacl) {
        LocalFree(new_dacl);
    }
    if (descriptor) {
        LocalFree(descriptor);
    }
    LocalFree(sid);
    if (status != ERROR_SUCCESS) {
        error = L"error " + std::to_wstring(status);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Processes
// ---------------------------------------------------------------------------

std::optional<DWORD> RunAndWait(const std::wstring& executable, const std::wstring& arguments,
                                const std::wstring& directory = {}) {
    std::wstring command_line = QuoteArgument(executable);
    if (!arguments.empty()) {
        command_line += L" " + arguments;
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(executable.c_str(), command_line.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                        nullptr, directory.empty() ? nullptr : directory.c_str(), &startup, &process)) {
        return std::nullopt;
    }
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exit_code = 1;
    GetExitCodeProcess(process.hProcess, &exit_code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return exit_code;
}

std::wstring SystemDirectoryPath(bool wow64) {
    wchar_t path[MAX_PATH] = {};
    const UINT length = wow64 ? GetSystemWow64DirectoryW(path, MAX_PATH) : GetSystemDirectoryW(path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return {};
    }
    return path;
}

// regsvr32 of the DLL's own architecture: the 64-bit one from System32, the
// 32-bit one from SysWOW64. /s keeps it from showing a dialog of its own.
bool RunRegsvr32(const std::wstring& dll, bool thirty_two_bit, bool unregister, SetupReport& report,
                 std::wstring& failure) {
    const std::wstring directory = SystemDirectoryPath(thirty_two_bit);
    const wchar_t* label = thirty_two_bit ? L"32-bit" : L"64-bit";
    const wchar_t* operation = unregister ? L"Unregister" : L"Register";
    if (directory.empty()) {
        failure = std::wstring(label) + L" " + operation + L": no system directory for it";
        report.Line(failure);
        return false;
    }
    std::wstring arguments = unregister ? L"/u /s " : L"/s ";
    arguments += QuoteArgument(dll);
    const std::optional<DWORD> code = RunAndWait(JoinPath(directory, L"regsvr32.exe"), arguments);
    if (!code) {
        failure = std::wstring(label) + L" " + operation + L" regsvr32 could not be started (error " +
                  std::to_wstring(GetLastError()) + L")";
        report.Line(failure);
        return false;
    }
    report.Line(std::wstring(label) + L" " + operation + L" regsvr32 exit code: " + std::to_wstring(*code));
    if (*code != 0) {
        failure = std::wstring(label) + L" " + operation + L" regsvr32 failed with exit code " + std::to_wstring(*code);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// The tray app
// ---------------------------------------------------------------------------

// The tray writes settings back, so it has to be gone before they are removed,
// and an older copy has to be gone before a new one starts: a second copy
// started with -silent sees the first and exits. Asked first, so it takes its
// icon out of the notification area; ended only if it does not go.
void StopTrayApp(SetupReport& report) {
    for (int attempt = 0; attempt < 20; ++attempt) {
        HWND window = FindWindowW(tray_ipc::kWindowClass, nullptr);
        if (window == nullptr) {
            break;
        }
        PostMessageW(window, WM_CLOSE, 0, 0);
        Sleep(150);
    }

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return;
    }
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    const DWORD self = GetCurrentProcessId();
    for (BOOL more = Process32FirstW(snapshot, &entry); more; more = Process32NextW(snapshot, &entry)) {
        if (entry.th32ProcessID == self || !EqualsIgnoreCase(entry.szExeFile, L"neokey_config.exe")) {
            continue;
        }
        HANDLE process = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, entry.th32ProcessID);
        if (process == nullptr) {
            continue;
        }
        report.Line(L"Closing the Neokey tray app (pid " + std::to_wstring(entry.th32ProcessID) + L")...");
        if (TerminateProcess(process, 1)) {
            WaitForSingleObject(process, 5000);
        } else {
            report.Warning(L"Could not close the Neokey tray app (pid " + std::to_wstring(entry.th32ProcessID) +
                           L").");
        }
        CloseHandle(process);
    }
    CloseHandle(snapshot);
}

void StartTrayApp(SetupReport& report) {
    const std::wstring executable = ExecutablePath();
    std::wstring command_line = QuoteArgument(executable) + L" -silent";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const std::wstring directory = ExecutableDirectory();
    if (CreateProcessW(executable.c_str(), command_line.data(), nullptr, nullptr, FALSE, 0, nullptr,
                       directory.c_str(), &startup, &process)) {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        report.Line(L"Started the Neokey tray app.");
    } else {
        report.Warning(L"Could not start the Neokey tray app; it will start at the next sign-in.");
    }
}

// ---------------------------------------------------------------------------
// The language settings, through the functions Set-WinUserLanguageList calls
// ---------------------------------------------------------------------------

// Read out of the International module rather than guessed: the same exports,
// the same order, the same flags. Get-WinUserLanguageList and its setter are
// what register.ps1 used, so doing exactly what they do keeps the two
// installers' results the same.
class LanguageSettings {
public:
    LanguageSettings() = default;
    ~LanguageSettings() {
        for (HMODULE module : {combase_, bcp47_, winlangdb_, input_, wgi_, cloud_}) {
            if (module) {
                FreeLibrary(module);
            }
        }
    }
    LanguageSettings(const LanguageSettings&) = delete;
    LanguageSettings& operator=(const LanguageSettings&) = delete;

    bool Load(std::wstring& error) {
        // From System32 only: the package folder is first in the ordinary search
        // order, and it is a folder the user can write to.
        auto load = [](const wchar_t* name) { return LoadLibraryExW(name, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32); };
        combase_ = load(L"combase.dll");
        bcp47_ = load(L"bcp47langs.dll");
        winlangdb_ = load(L"winlangdb.dll");
        input_ = load(L"input.dll");
        wgi_ = load(L"ext-ms-win-globalization-input-l1-1-2.dll");
        cloud_ = load(L"coreglobconfig.dll");

        create_ = Proc<CreateStringFn>(combase_, "WindowsCreateString");
        delete_ = Proc<DeleteStringFn>(combase_, "WindowsDeleteString");
        buffer_ = Proc<RawBufferFn>(combase_, "WindowsGetStringRawBuffer");
        get_languages_ = Proc<GetUserLanguagesFn>(bcp47_, "GetUserLanguages");
        get_methods_ = Proc<GetUserLanguageInputMethodsFn>(bcp47_, "GetUserLanguageInputMethods");
        lcid_from_tag_ = Proc<LcidFromBcp47Fn>(bcp47_, "LcidFromBcp47");
        remove_inputs_ = Proc<VoidFn>(bcp47_, "RemoveInputsForAllLanguagesInternal");
        set_override_ = Proc<SetOverrideFn>(bcp47_, "SetInputMethodOverride");
        set_languages_ = Proc<SetUserLanguagesFn>(winlangdb_, "SetUserLanguages");
        ensure_profile_ = Proc<VoidFn>(winlangdb_, "EnsureLanguageProfileExists");
        install_ = Proc<LayoutOrTipFn>(input_, "InstallLayoutOrTip");
        set_default_ = Proc<LayoutOrTipFn>(input_, "SetDefaultLayoutOrTip");
        default_method_ = Proc<DefaultMethodFn>(wgi_, "WGIGetDefaultInputMethodForLanguage");
        transform_ = Proc<TransformFn>(wgi_, "WGITransformInputMethodsForLanguage");
        cloud_sync_ = Proc<VoidFn>(cloud_, "SyncLanguageDataToCloudSynchronous");

        std::wstring missing;
        auto need = [&](bool present, const wchar_t* name) {
            if (!present) {
                missing += (missing.empty() ? L"" : L", ") + std::wstring(name);
            }
        };
        need(create_ != nullptr, L"WindowsCreateString");
        need(delete_ != nullptr, L"WindowsDeleteString");
        need(buffer_ != nullptr, L"WindowsGetStringRawBuffer");
        need(get_languages_ != nullptr, L"GetUserLanguages");
        need(get_methods_ != nullptr, L"GetUserLanguageInputMethods");
        need(set_languages_ != nullptr, L"SetUserLanguages");
        need(remove_inputs_ != nullptr, L"RemoveInputsForAllLanguagesInternal");
        need(set_override_ != nullptr, L"SetInputMethodOverride");
        need(install_ != nullptr, L"InstallLayoutOrTip");
        need(set_default_ != nullptr, L"SetDefaultLayoutOrTip");
        if (!missing.empty()) {
            error = L"This Windows lacks the language functions Neokey needs: " + missing + L".";
            return false;
        }
        return true;
    }

    std::optional<std::vector<LanguageEntry>> Read(std::wstring& error) {
        if (ensure_profile_) {
            ensure_profile_();
        }
        HSTRING list = nullptr;
        const HRESULT hr = get_languages_(L';', &list);
        if (FAILED(hr)) {
            error = L"GetUserLanguages failed: " + HexCode(static_cast<DWORD>(hr));
            return std::nullopt;
        }
        const std::wstring tags = Take(list);
        std::vector<LanguageEntry> languages;
        for (const std::wstring& tag : Split(tags)) {
            LanguageEntry entry;
            entry.tag = tag;
            HSTRING methods = nullptr;
            if (SUCCEEDED(get_methods_(tag.c_str(), L';', &methods))) {
                entry.tips = Split(Take(methods));
            }
            languages.push_back(std::move(entry));
        }
        return languages;
    }

    // What New-WinUserLanguageList puts into a new entry for `tag`.
    std::vector<std::wstring> DefaultInputMethods(const std::wstring& tag) {
        if (!default_method_ || !transform_) {
            return {};
        }
        HSTRING language = Make(tag);
        HSTRING defaults = nullptr;
        std::wstring result;
        if (SUCCEEDED(default_method_(language, &defaults)) && defaults != nullptr) {
            HSTRING transformed = nullptr;
            if (SUCCEEDED(transform_(defaults, language, &transformed))) {
                result = Take(transformed);
            }
        }
        Free(defaults);
        Free(language);
        return Split(result);
    }

    // Set-WinUserLanguageList: the language list first, then every input
    // method taken away and each language's put back, the first with 0x100.
    bool Write(const std::vector<LanguageEntry>& languages, SetupReport& report) {
        std::wstring tags;
        for (const LanguageEntry& language : languages) {
            if (!tags.empty()) {
                tags.push_back(L';');
            }
            tags += language.tag;
        }
        HSTRING list = Make(tags);
        HRESULT hr = set_languages_(L';', list);
        Free(list);
        report.Line(L"SetUserLanguages(" + tags + L"): " + HexCode(static_cast<DWORD>(hr)));
        if (FAILED(hr)) {
            report.Error(L"Windows refused the language list.");
            return false;
        }
        hr = remove_inputs_();
        if (FAILED(hr) && hr != E_NOTIMPL) {
            report.Error(L"RemoveInputsForAllLanguagesInternal failed: " + HexCode(static_cast<DWORD>(hr)));
            return false;
        }

        bool ok = true;
        bool first = true;
        for (const LanguageEntry& language : languages) {
            if (lcid_from_tag_) {
                HSTRING tag = Make(language.tag);
                LCID lcid = 0;
                const HRESULT valid = lcid_from_tag_(tag, &lcid);
                Free(tag);
                if (FAILED(valid) || lcid == 0) {
                    report.Warning(L"Skipped a language Windows does not recognise: " + language.tag);
                    continue;
                }
            }
            std::wstring tips = JoinTips(language.tips);
            if (tips.empty()) {
                tips = JoinTips(DefaultInputMethods(language.tag));
            }
            const DWORD flags = first ? 0x100 : 0;
            first = false;
            const HRESULT installed = install_(tips.c_str(), flags);
            report.Line(L"InstallLayoutOrTip(" + tips + L", " + HexCode(flags) + L"): " +
                        HexCode(static_cast<DWORD>(installed)));
            if (FAILED(installed)) {
                ok = false;
            }
        }
        SyncToCloud();
        if (!ok) {
            report.Error(L"Windows refused some of the input methods.");
        }
        return ok;
    }

    // Set-WinDefaultInputMethodOverride -InputTip.
    bool SetOverride(const std::wstring& tip, SetupReport& report) {
        const HRESULT hr = set_override_(tip.c_str());
        const HRESULT hr_default = set_default_(tip.c_str(), 0);
        report.Line(L"SetInputMethodOverride: " + HexCode(static_cast<DWORD>(hr)) + L", SetDefaultLayoutOrTip: " +
                    HexCode(static_cast<DWORD>(hr_default)));
        SyncToCloud();
        return SUCCEEDED(hr);
    }

    // Set-WinDefaultInputMethodOverride with no argument: no override, and the
    // first language's first input method as the default.
    bool ClearOverride(SetupReport& report) {
        const HRESULT hr = set_override_(L"");
        std::wstring error;
        const auto languages = Read(error);
        if (languages && !languages->empty() && !languages->front().tips.empty()) {
            set_default_(languages->front().tips.front().c_str(), 0);
        }
        report.Line(L"SetInputMethodOverride(\"\"): " + HexCode(static_cast<DWORD>(hr)));
        SyncToCloud();
        return SUCCEEDED(hr);
    }

private:
    using CreateStringFn = HRESULT(WINAPI*)(PCNZWCH, UINT32, HSTRING*);
    using DeleteStringFn = HRESULT(WINAPI*)(HSTRING);
    using RawBufferFn = PCWSTR(WINAPI*)(HSTRING, UINT32*);
    using GetUserLanguagesFn = HRESULT(WINAPI*)(wchar_t, HSTRING*);
    using GetUserLanguageInputMethodsFn = HRESULT(WINAPI*)(PCWSTR, wchar_t, HSTRING*);
    using LcidFromBcp47Fn = HRESULT(WINAPI*)(HSTRING, LCID*);
    using SetUserLanguagesFn = HRESULT(WINAPI*)(wchar_t, HSTRING);
    using SetOverrideFn = HRESULT(WINAPI*)(PCWSTR);
    using LayoutOrTipFn = HRESULT(WINAPI*)(PCWSTR, DWORD);
    using DefaultMethodFn = HRESULT(WINAPI*)(HSTRING, HSTRING*);
    using TransformFn = HRESULT(WINAPI*)(HSTRING, HSTRING, HSTRING*);
    using VoidFn = HRESULT(WINAPI*)();

    template <typename Fn>
    static Fn Proc(HMODULE module, const char* name) {
        return module ? reinterpret_cast<Fn>(GetProcAddress(module, name)) : nullptr;
    }

    HSTRING Make(const std::wstring& text) {
        HSTRING result = nullptr;
        create_(text.c_str(), static_cast<UINT32>(text.size()), &result);
        return result;
    }

    void Free(HSTRING text) {
        if (text) {
            delete_(text);
        }
    }

    // The text of an HSTRING the callee returned, which is then released.
    std::wstring Take(HSTRING text) {
        if (!text) {
            return {};
        }
        UINT32 length = 0;
        const PCWSTR buffer = buffer_(text, &length);
        std::wstring result = buffer ? std::wstring(buffer, length) : std::wstring();
        delete_(text);
        return result;
    }

    static std::vector<std::wstring> Split(const std::wstring& text) {
        std::vector<std::wstring> parts;
        size_t start = 0;
        while (start <= text.size()) {
            const size_t end = text.find(L';', start);
            std::wstring part = text.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
            if (!part.empty()) {
                parts.push_back(std::move(part));
            }
            if (end == std::wstring::npos) {
                break;
            }
            start = end + 1;
        }
        return parts;
    }

    void SyncToCloud() {
        if (cloud_sync_) {
            cloud_sync_();
        }
    }

    HMODULE combase_ = nullptr;
    HMODULE bcp47_ = nullptr;
    HMODULE winlangdb_ = nullptr;
    HMODULE input_ = nullptr;
    HMODULE wgi_ = nullptr;
    HMODULE cloud_ = nullptr;
    CreateStringFn create_ = nullptr;
    DeleteStringFn delete_ = nullptr;
    RawBufferFn buffer_ = nullptr;
    GetUserLanguagesFn get_languages_ = nullptr;
    GetUserLanguageInputMethodsFn get_methods_ = nullptr;
    LcidFromBcp47Fn lcid_from_tag_ = nullptr;
    VoidFn remove_inputs_ = nullptr;
    SetOverrideFn set_override_ = nullptr;
    SetUserLanguagesFn set_languages_ = nullptr;
    VoidFn ensure_profile_ = nullptr;
    LayoutOrTipFn install_ = nullptr;
    LayoutOrTipFn set_default_ = nullptr;
    DefaultMethodFn default_method_ = nullptr;
    TransformFn transform_ = nullptr;
    VoidFn cloud_sync_ = nullptr;
};

std::wstring DescribeList(const std::vector<LanguageEntry>& languages) {
    std::wstring text;
    for (const LanguageEntry& language : languages) {
        if (!text.empty()) {
            text += L" | ";
        }
        text += language.tag + L": " + JoinTips(language.tips);
    }
    return text.empty() ? L"<empty>" : text;
}

std::wstring CurrentOverride() {
    return TrimWhitespace(GetString(HKEY_CURRENT_USER, kUserProfileKey, L"InputMethodOverride").value_or(L""));
}

bool DisplayLanguageIsVietnamese() {
    const LANGID language = GetUserDefaultUILanguage();
    return PRIMARYLANGID(language) == LANG_VIETNAMESE;
}

// ---------------------------------------------------------------------------
// The per-user steps, one register.ps1 function each
// ---------------------------------------------------------------------------

// Set-NeokeyProfilePreference: the DLL reads this when it registers, in the
// account that runs regsvr32.
bool WriteProfilePreference(bool english_profile, SetupReport& report) {
    if (!SetNumber(HKEY_CURRENT_USER, kSettingsKey, L"RegisterEnglishProfile", english_profile ? 1 : 0)) {
        report.Error(L"Could not record the English copy choice in HKCU\\Software\\Neokey.");
        return false;
    }
    return true;
}

// Add-NeokeyToUserLanguageList.
bool AddToLanguageList(LanguageSettings& settings, bool english_profile, SetupReport& report) {
    report.Line(L"Adding TIP to user language list...");
    std::wstring error;
    const auto current = settings.Read(error);
    if (!current) {
        report.Error(error);
        return false;
    }
    report.Line(L"Language list before: " + DescribeList(*current));

    const AddNeokeyPlan plan = PlanAddNeokey(*current, english_profile, settings.DefaultInputMethods(L"en-US"));
    for (const std::wstring& note : plan.notes) {
        report.Line(note);
    }

    // Only this run can still see what Vietnamese looked like: write it down
    // before changing it.
    const bool already_recorded = ValuePresent(HKEY_CURRENT_USER, kSettingsKey, kRecordedTips);
    if (ShouldRecordPreNeokeyState(plan.added_vietnamese, plan.replaced, already_recorded)) {
        if (!SetString(HKEY_CURRENT_USER, kSettingsKey, kRecordedTips, JoinTips(plan.replaced)) ||
            !SetNumber(HKEY_CURRENT_USER, kSettingsKey, kRecordedAddedLanguage, plan.added_vietnamese ? 1 : 0)) {
            report.Warning(L"Could not record what to restore on uninstall.");
        }
    }

    if (!plan.changed) {
        return true;
    }
    if (!settings.Write(plan.languages, report)) {
        return false;
    }
    const auto after = settings.Read(error);
    report.Line(L"Language list after: " + (after ? DescribeList(*after) : error));
    return true;
}

// Initialize-NeokeyUserData: the per-user shorthand folder, readable by the
// DLL inside sandboxed apps, with the package's shorthand file moved in once.
void InitializeUserData(const std::wstring& package_directory, SetupReport& report) {
    const std::wstring local_app_data = EnvironmentValue(L"LOCALAPPDATA");
    if (TrimWhitespace(local_app_data).empty()) {
        report.Warning(L"LOCALAPPDATA is unavailable; shorthand data will use the package fallback path.");
        return;
    }
    const std::wstring data_directory = JoinPath(local_app_data, L"Neokey");
    CreateDirectoryW(data_directory.c_str(), nullptr);
    const std::wstring shorthand = JoinPath(data_directory, L"neokey_shorthand.txt");
    const std::wstring legacy = JoinPath(package_directory, L"neokey_shorthand.txt");
    if (!FileExists(shorthand) && FileExists(legacy)) {
        if (CopyFileW(legacy.c_str(), shorthand.c_str(), TRUE)) {
            report.Line(L"Migrated shorthand data to the current user profile.");
        }
    }
    for (const wchar_t* sid : {kAllAppPackages, kAllRestrictedAppPackages}) {
        std::wstring error;
        if (!ApplyAccess(data_directory, SE_FILE_OBJECT, sid, FILE_GENERIC_READ | FILE_GENERIC_EXECUTE,
                         SUB_CONTAINERS_AND_OBJECTS_INHERIT, SET_ACCESS, error)) {
            report.Warning(L"Could not initialize the per-user shorthand folder: " + error);
        }
    }
}

// Initialize-NeokeyUserSettings.
bool InitializeUserSettings(const std::wstring& package_directory, SetupReport& report) {
    InitializeUserData(package_directory, report);
    if (!ValuePresent(HKEY_CURRENT_USER, kSettingsKey, L"InputMethod")) {
        report.Line(L"Initializing default InputMethod to VNI (2)...");
        SetNumber(HKEY_CURRENT_USER, kSettingsKey, L"InputMethod", 2);
    }
    report.Line(L"Granting AppContainer read access to HKCU:\\Software\\Neokey...");
    std::wstring error;
    if (!ApplyAccess(L"CURRENT_USER\\Software\\Neokey", SE_REGISTRY_KEY, kAllAppPackages, KEY_READ,
                     SUB_CONTAINERS_AND_OBJECTS_INHERIT, SET_ACCESS, error)) {
        report.Error(L"Could not open HKCU:\\Software\\Neokey for ACL update: " + error);
        return false;
    }
    report.Line(L"AppContainer read access granted successfully.");
    return true;
}

// Set-NeokeyInputOrder.
void PutNeokeyFirstInInputOrder(SetupReport& report) {
    struct Target {
        const wchar_t* path;
        bool preload_style;
    };
    const Target targets[] = {{kPreloadKey, true}, {kCtfOrderKey, false}};
    for (const Target& target : targets) {
        const std::vector<std::wstring> current = OrderValues(ReadOrder(HKEY_CURRENT_USER, target.path));
        if (current.empty()) {
            // Windows has not built this list for the user yet; inventing one
            // would only fight whatever it writes later.
            continue;
        }
        const std::vector<std::wstring> ordered = OrderVietnameseFirst(current);
        if (ordered == current) {
            continue;
        }
        if (!WriteOrder(HKEY_CURRENT_USER, target.path, ordered, target.preload_style)) {
            report.Warning(std::wstring(L"Could not update the input order in ") + target.path + L".");
        }
    }
    const std::vector<std::wstring> preload = OrderValues(ReadOrder(HKEY_CURRENT_USER, kPreloadKey));
    const std::vector<std::wstring> ctf = OrderValues(ReadOrder(HKEY_CURRENT_USER, kCtfOrderKey));
    if (preload == ctf && !preload.empty() && preload.front() == kVietnameseLayout) {
        report.Line(L"Neokey is first in both the Win32 and CTF input lists.");
    } else {
        report.Warning(L"Could not put Neokey first in both input lists. Win32: " + JoinTips(preload) +
                       L". CTF: " + JoinTips(ctf) + L".");
    }
}

// Activate-NeokeyInCurrentSession. SPIF_UPDATEINIFILE is deliberately not
// passed: it rewrites Preload without CTF's copy of the order, and the two
// lists then disagree at the next sign-in.
void ActivateInCurrentSession(SetupReport& report) {
    HKL layout = reinterpret_cast<HKL>(static_cast<ULONG_PTR>(0x0409042a));
    SystemParametersInfoW(SPI_SETDEFAULTINPUTLANG, 0, &layout, SPIF_SENDCHANGE);
    PostMessageW(HWND_BROADCAST, WM_INPUTLANGCHANGEREQUEST, 0, reinterpret_cast<LPARAM>(layout));
    ActivateKeyboardLayout(layout, KLF_ACTIVATE | KLF_SETFORPROCESS);
    report.Line(L"Activated Neokey as active keyboard layout in the current desktop session.");
}

// Set-NeokeyAsDefaultInputMethod.
bool SetAsDefaultInputMethod(LanguageSettings& settings, SetupReport& report) {
    report.Line(L"Setting Neokey as the default input method for the current Windows user...");
    settings.SetOverride(kVietnameseTip, report);
    if (!EqualsIgnoreCase(CurrentOverride(), kVietnameseTip)) {
        report.Error(L"Windows did not retain Neokey as the default input method override.");
        return false;
    }

    PutNeokeyFirstInInputOrder(report);

    // Point the legacy Vietnamese layout at the US physical one, recording
    // what was there first.
    const std::wstring previous = GetString(HKEY_CURRENT_USER, kSubstitutesKey, kVietnameseLayout).value_or(L"");
    if (ShouldRecordLayoutSubstitute(GetString(HKEY_CURRENT_USER, kSettingsKey, kRecordedSubstitute), previous,
                                     kUsLayout)) {
        SetString(HKEY_CURRENT_USER, kSettingsKey, kRecordedSubstitute, previous);
    }
    if (!SetString(HKEY_CURRENT_USER, kSubstitutesKey, kVietnameseLayout, kUsLayout)) {
        report.Warning(L"Could not point the Vietnamese layout at the US layout.");
    }

    ActivateInCurrentSession(report);
    report.Line(L"Neokey is now the default input method override for this user.");
    return true;
}

// Set-NeokeyAutoStart: byte for byte what the app's own "Start with Windows"
// writes, so that setting reads back as on.
void SetAutoStart(SetupReport& report) {
    const std::wstring command = L"\"" + ExecutablePath() + L"\" -silent";
    if (SetString(HKEY_CURRENT_USER, kRunKey, L"Neokey", command)) {
        report.Line(L"Neokey will start with Windows, minimised to the tray: " + command);
    } else {
        report.Warning(L"Could not add Neokey to Windows startup.");
    }
}

// Restore-VietnameseLayoutSubstitute.
void RestoreLayoutSubstitute(SetupReport& report) {
    const auto recorded = GetString(HKEY_CURRENT_USER, kSettingsKey, kRecordedSubstitute);
    if (!recorded) {
        // Nothing recorded: other Vietnamese IMEs write the same value, and
        // removing theirs would hand their users the number-row tone marks.
        return;
    }
    if (recorded->empty()) {
        DeleteValue(HKEY_CURRENT_USER, kSubstitutesKey, kVietnameseLayout);
        report.Line(L"Removed the Vietnamese layout substitute Neokey added.");
    } else {
        SetString(HKEY_CURRENT_USER, kSubstitutesKey, kVietnameseLayout, *recorded);
        report.Line(L"Restored the Vietnamese layout substitute to " + *recorded + L".");
    }
}

void ClearRecords() {
    for (const wchar_t* name : {kRecordedTips, kRecordedAddedLanguage, kRecordedSubstitute}) {
        DeleteValue(HKEY_CURRENT_USER, kSettingsKey, name);
    }
}

// Remove-NeokeyFromUserLanguageList.
bool RemoveFromLanguageList(LanguageSettings& settings, SetupReport& report) {
    report.Line(L"Removing TIP from user language list...");
    if (EqualsIgnoreCase(CurrentOverride(), kVietnameseTip)) {
        settings.ClearOverride(report);
        report.Line(L"Removed Neokey as the default input method override.");
    }
    std::wstring error;
    const auto current = settings.Read(error);
    if (!current) {
        report.Error(error);
        return false;
    }
    report.Line(L"Language list before: " + DescribeList(*current));
    const std::optional<DWORD> added = GetNumber(HKEY_CURRENT_USER, kSettingsKey, kRecordedAddedLanguage);
    const RemoveNeokeyPlan plan =
        PlanRemoveNeokey(*current, ParseRecordedTips(GetString(HKEY_CURRENT_USER, kSettingsKey, kRecordedTips)),
                         added.has_value() && *added == 1, DisplayLanguageIsVietnamese());
    for (const std::wstring& note : plan.notes) {
        report.Line(note);
    }
    if (!plan.changed) {
        report.Line(L"TIP was not in user language list.");
        return true;
    }
    if (!settings.Write(plan.languages, report)) {
        return false;
    }
    const auto after = settings.Read(error);
    report.Line(L"Language list after: " + (after ? DescribeList(*after) : error));
    report.Line(L"Successfully removed TIP from user language list.");
    return true;
}

// Remove-NeokeyResidue.
void RemoveResidue(bool keep_user_data, const std::wstring& package_directory, SetupReport& report) {
    StopTrayApp(report);
    for (const ResidueTarget& target :
         BuildResidueTargets(EnvironmentValue(L"TEMP"), EnvironmentValue(L"LOCALAPPDATA"),
                             EnvironmentValue(L"SystemRoot"), package_directory)) {
        bool present = false;
        switch (target.kind) {
            case ResidueKind::RegistryKey:
                present = KeyPresent(HKEY_CURRENT_USER, target.path);
                break;
            case ResidueKind::RegistryValue:
                present = ValuePresent(HKEY_CURRENT_USER, target.path, target.value_name.c_str());
                break;
            default:
                present = GetFileAttributesW(target.path.c_str()) != INVALID_FILE_ATTRIBUTES;
                break;
        }
        if (!present) {
            continue;
        }
        if (keep_user_data && target.user_data) {
            report.Line(L"Kept " + target.label + L": " + target.DisplayName());
            continue;
        }
        bool removed = false;
        switch (target.kind) {
            case ResidueKind::RegistryKey:
                removed = DeleteKeyTree(HKEY_CURRENT_USER, target.path);
                break;
            case ResidueKind::RegistryValue:
                removed = DeleteValue(HKEY_CURRENT_USER, target.path, target.value_name.c_str());
                break;
            case ResidueKind::Directory: {
                // SHFileOperation takes a double-terminated list.
                std::wstring from = target.path;
                from.push_back(L'\0');
                SHFILEOPSTRUCTW operation{};
                operation.wFunc = FO_DELETE;
                operation.pFrom = from.c_str();
                operation.fFlags = FOF_NO_UI;
                removed = SHFileOperationW(&operation) == 0 &&
                          GetFileAttributesW(target.path.c_str()) == INVALID_FILE_ATTRIBUTES;
                break;
            }
            case ResidueKind::File:
                removed = DeleteFileW(target.path.c_str()) != FALSE;
                break;
        }
        if (removed) {
            report.Line(L"Removed " + target.label + L": " + target.DisplayName());
        } else {
            // A log still open in an app that has not been restarted is the
            // ordinary case, and not a reason to fail the uninstall.
            report.Warning(L"Could not remove " + target.label + L" at " + target.DisplayName() + L".");
        }
    }
}

// Get-NeokeyMachineRegistrationKeys.
struct MachineKey {
    HKEY root;
    std::wstring path;
    std::wstring display;
};

std::vector<MachineKey> MachineRegistrationKeys() {
    const std::wstring clsid(kClsid);
    return {
        {HKEY_LOCAL_MACHINE, L"SOFTWARE\\Classes\\CLSID\\" + clsid, L"HKLM:\\SOFTWARE\\Classes\\CLSID\\" + clsid},
        {HKEY_LOCAL_MACHINE, L"SOFTWARE\\Classes\\Wow6432Node\\CLSID\\" + clsid,
         L"HKLM:\\SOFTWARE\\Classes\\Wow6432Node\\CLSID\\" + clsid},
        {HKEY_CURRENT_USER, L"SOFTWARE\\Classes\\CLSID\\" + clsid, L"HKCU:\\SOFTWARE\\Classes\\CLSID\\" + clsid},
        {HKEY_CURRENT_USER, L"SOFTWARE\\Classes\\Wow6432Node\\CLSID\\" + clsid,
         L"HKCU:\\SOFTWARE\\Classes\\Wow6432Node\\CLSID\\" + clsid},
        {HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\CTF\\TIP\\" + clsid, L"HKLM:\\SOFTWARE\\Microsoft\\CTF\\TIP\\" + clsid},
        {HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Microsoft\\CTF\\TIP\\" + clsid,
         L"HKLM:\\SOFTWARE\\WOW6432Node\\Microsoft\\CTF\\TIP\\" + clsid},
    };
}

// Files that came out of a zip downloaded with a browser carry its "from the
// internet" mark, which makes Windows ask before starting the tray app - at
// every sign-in, from the startup entry. Only files whose hashes the manifest
// has just vouched for are cleared.
void ClearDownloadedMark(const std::wstring& package_directory) {
    for (const wchar_t* name : {L"neokey.dll", L"neokey32.dll", L"neokey_config.exe"}) {
        const std::wstring stream = JoinPath(package_directory, name) + L":Zone.Identifier";
        DeleteFileW(stream.c_str());
    }
}

// OneDrive's "Always keep on this device" for the files Windows loads.
void PinForOneDrive(const std::wstring& package_directory) {
    for (const wchar_t* name : {L"neokey.dll", L"neokey32.dll", L"neokey_config.exe"}) {
        const std::wstring path = JoinPath(package_directory, name);
        const DWORD attributes = GetFileAttributesW(path.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            continue;
        }
        SetFileAttributesW(path.c_str(),
                           (attributes | FILE_ATTRIBUTE_PINNED) & ~static_cast<DWORD>(FILE_ATTRIBUTE_UNPINNED));
    }
}

LocationVerdict CheckLocation(const std::wstring& package_directory) {
    const std::wstring directory = LongPath(package_directory);
    UINT drive_type = DRIVE_UNKNOWN;
    if (directory.size() >= 2 && directory[1] == L':') {
        const std::wstring root = directory.substr(0, 2) + L"\\";
        drive_type = GetDriveTypeW(root.c_str());
    }
    wchar_t temp_path[MAX_PATH + 1] = {};
    GetTempPathW(MAX_PATH + 1, temp_path);
    std::vector<std::wstring> temps = {LongPath(EnvironmentValue(L"TEMP")), LongPath(EnvironmentValue(L"TMP")),
                                       LongPath(temp_path)};
    const std::wstring local_app_data = EnvironmentValue(L"LOCALAPPDATA");
    if (!local_app_data.empty()) {
        temps.push_back(JoinPath(local_app_data, L"Temp"));
    }
    const std::wstring system_root = EnvironmentValue(L"SystemRoot");
    if (!system_root.empty()) {
        temps.push_back(JoinPath(system_root, L"Temp"));
    }
    const std::vector<std::wstring> onedrive = {EnvironmentValue(L"OneDrive"), EnvironmentValue(L"OneDriveConsumer"),
                                                EnvironmentValue(L"OneDriveCommercial")};
    // Checked in both spellings: the archive-folder names are recognisable in
    // either, and the comparison with TEMP needs the long one.
    LocationVerdict verdict = ClassifyInstallLocation(directory, temps, onedrive, drive_type);
    if (verdict.problem == LocationProblem::None && directory != package_directory) {
        verdict = ClassifyInstallLocation(package_directory, temps, onedrive, drive_type);
    }
    return verdict;
}

// Runs this exe again as Administrator with `arguments`, and waits. An empty
// optional means it never started; the error says why.
std::optional<DWORD> RunElevatedSelf(const std::wstring& arguments, DWORD& error) {
    const std::wstring executable = ExecutablePath();
    SHELLEXECUTEINFOW execute{};
    execute.cbSize = sizeof(execute);
    execute.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    execute.lpVerb = L"runas";
    execute.lpFile = executable.c_str();
    execute.lpParameters = arguments.c_str();
    execute.nShow = SW_HIDE;
    if (!ShellExecuteExW(&execute) || execute.hProcess == nullptr) {
        error = GetLastError();
        return std::nullopt;
    }
    WaitForSingleObject(execute.hProcess, INFINITE);
    DWORD exit_code = 1;
    GetExitCodeProcess(execute.hProcess, &exit_code);
    CloseHandle(execute.hProcess);
    return exit_code;
}

// The Administrator half ran in a process of its own and wrote what it said to
// a file; on a failure that is the only way to tell "the DLL could not be
// loaded" from "Windows refused".
void EchoLog(const std::wstring& path, SetupReport& report) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    std::string bytes;
    char buffer[4096];
    DWORD read = 0;
    while (ReadFile(file, buffer, sizeof(buffer), &read, nullptr) && read > 0 && bytes.size() < (1 << 20)) {
        bytes.append(buffer, read);
    }
    CloseHandle(file);
    if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF) {
        bytes.erase(0, 3);
    }
    const int length = MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    std::wstring text(static_cast<size_t>(length > 0 ? length : 0), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()), text.data(), length);
    report.Line(L"What the Administrator step reported (" + path + L"):");
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find(L'\n', start);
        if (end == std::wstring::npos) {
            end = text.size();
        }
        std::wstring line = text.substr(start, end - start);
        if (!line.empty() && line.back() == L'\r') {
            line.pop_back();
        }
        if (!line.empty()) {
            report.Line(L"  " + line);
        }
        start = end + 1;
    }
}

}  // namespace

bool IsProcessElevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return false;
    }
    TOKEN_ELEVATION elevation{};
    DWORD size = 0;
    const bool elevated = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size) &&
                          elevation.TokenIsElevated != 0;
    CloseHandle(token);
    return elevated;
}

bool UserPrefersVietnamese() {
    // The tray's menus follow the same value: English only when the person
    // switched Neokey to English.
    const std::optional<DWORD> mode = GetNumber(HKEY_CURRENT_USER, kSettingsKey, L"TypingMode");
    return !mode.has_value() || *mode != 1;
}

// Invoke-DllRegistration.
bool RegisterElevated(const SetupOptions& options, const std::wstring& package_directory, SetupReport& report) {
    if (!IsProcessElevated()) {
        report.Error(L"--register-elevated requires Administrator privileges.");
        return false;
    }
    const PackageCheck check = CheckPackage(package_directory, PortableRequiredFiles());
    if (check.problem != PackageProblem::None) {
        report.Error(DescribePackageProblem(check, false));
        return false;
    }
    report.Line(L"Release artifact hashes verified. Version: " + check.version);

    // Set before the DLL reads it, including on upgrades from an opt-out.
    if (!WriteProfilePreference(options.english_profile, report)) {
        return false;
    }
    report.Line(options.english_profile ? L"Registering Neokey under both Vietnamese and English (US)..."
                                        : L"Registering Neokey under Vietnamese only (in-place)...");

    const std::wstring dll64 = JoinPath(package_directory, L"neokey.dll");
    const std::wstring dll32 = JoinPath(package_directory, L"neokey32.dll");
    report.Line(L"Target directory: " + package_directory);
    report.Line(L"DLL 64 path: " + dll64);
    report.Line(L"DLL 32 path: " + dll32);

    // Apps that run in a sandbox load the DLL too, and can read nothing they
    // are not granted.
    for (const wchar_t* sid : {kAllAppPackages, kAllRestrictedAppPackages}) {
        std::wstring error;
        if (!ApplyAccess(package_directory, SE_FILE_OBJECT, sid, FILE_GENERIC_READ | FILE_GENERIC_EXECUTE,
                         SUB_CONTAINERS_AND_OBJECTS_INHERIT, GRANT_ACCESS, error)) {
            report.Warning(L"Could not grant app sandboxes read access to the folder: " + error);
        }
        for (const std::wstring& dll : {dll64, dll32}) {
            if (FileExists(dll) &&
                !ApplyAccess(dll, SE_FILE_OBJECT, sid, FILE_GENERIC_READ | FILE_GENERIC_EXECUTE, NO_INHERITANCE,
                             GRANT_ACCESS, error)) {
                report.Warning(L"Could not grant app sandboxes read access to " + dll + L": " + error);
            }
        }
    }

    std::wstring failure;
    if (FileExists(dll32) && !RunRegsvr32(dll32, true, false, report, failure)) {
        report.Error(failure);
        return false;
    }
    if (!RunRegsvr32(dll64, false, false, report, failure)) {
        report.Error(failure);
        return false;
    }
    return true;
}

// Invoke-DllUnregistration: both DLLs are asked even when one refuses, the
// keys are swept either way, and what is left on the machine decides.
bool UnregisterElevated(const std::wstring& package_directory, SetupReport& report) {
    if (!IsProcessElevated()) {
        report.Error(L"--unregister-elevated requires Administrator privileges.");
        return false;
    }
    std::vector<std::wstring> failures;
    struct Dll {
        std::wstring path;
        bool thirty_two_bit;
    };
    for (const Dll& dll : {Dll{JoinPath(package_directory, L"neokey32.dll"), true},
                           Dll{JoinPath(package_directory, L"neokey.dll"), false}}) {
        if (!FileExists(dll.path)) {
            report.Line(std::wstring(dll.thirty_two_bit ? L"32-bit" : L"64-bit") +
                        L" DLL is not in this folder; its registration is swept instead.");
            continue;
        }
        std::wstring failure;
        if (!RunRegsvr32(dll.path, dll.thirty_two_bit, true, report, failure)) {
            failures.push_back(failure);
        }
    }

    // Swept after regsvr32 /u, never before: unregistration is what asks
    // Windows to retract the profile.
    std::vector<std::wstring> remaining;
    for (const MachineKey& key : MachineRegistrationKeys()) {
        if (!KeyPresent(key.root, key.path)) {
            continue;
        }
        if (DeleteKeyTree(key.root, key.path)) {
            report.Line(L"Removed leftover registration key: " + key.display);
        } else {
            report.Warning(L"Could not remove " + key.display);
            remaining.push_back(key.display);
        }
    }
    if (!remaining.empty()) {
        std::wstring message = L"Neokey is still registered.";
        for (const std::wstring& failure : failures) {
            message += L" " + failure + L".";
        }
        report.Error(message);
        return false;
    }
    return true;
}

// Configure-NeokeyCurrentUser, always with -SetDefault: both installers pass it.
bool ConfigureUser(const SetupOptions& options, const std::wstring& package_directory, SetupReport& report) {
    LanguageSettings settings;
    std::wstring error;
    if (!settings.Load(error)) {
        report.Error(error);
        return false;
    }
    if (!WriteProfilePreference(options.english_profile, report) ||
        !AddToLanguageList(settings, options.english_profile, report) ||
        !InitializeUserSettings(package_directory, report) || !SetAsDefaultInputMethod(settings, report)) {
        return false;
    }
    SetAutoStart(report);
    return true;
}

// Unconfigure-NeokeyCurrentUser.
bool UnconfigureUser(const SetupOptions& options, const std::wstring& package_directory, SetupReport& report) {
    LanguageSettings settings;
    std::wstring error;
    if (!settings.Load(error)) {
        report.Error(error);
        return false;
    }
    // Stopping here keeps the record of what Vietnamese looked like, so a
    // second attempt can still put it back.
    if (!RemoveFromLanguageList(settings, report)) {
        return false;
    }
    RestoreLayoutSubstitute(report);
    // Both of those read the record out of the settings key, which is about to
    // go; if removing the key fails, the record must not survive to be
    // replayed against a later install.
    ClearRecords();
    RemoveResidue(options.keep_user_data, package_directory, report);
    return true;
}

bool Install(const SetupOptions& options, const std::wstring& package_directory, SetupReport& report) {
    report.Line(L"Installing Neokey from " + package_directory);
    const bool elevated = IsProcessElevated();
    if (elevated) {
        report.Warning(L"Running as Administrator: the language list and default input changes apply to the Administrator account. Start Neokey normally so it asks for Administrator permission only for the registration.");
    }

    const LocationVerdict location = CheckLocation(package_directory);
    if (location.problem != LocationProblem::None) {
        const std::wstring message = DescribeLocationProblem(location.problem, package_directory, false);
        if (location.blocking) {
            report.Error(message);
            return false;
        }
        report.Warning(message);
        if (location.problem == LocationProblem::OneDrive) {
            PinForOneDrive(package_directory);
        }
    }

    const PackageCheck check = CheckPackage(package_directory, PortableRequiredFiles());
    if (check.problem != PackageProblem::None) {
        report.Error(DescribePackageProblem(check, false));
        return false;
    }
    report.Line(L"Release artifact hashes verified. Version: " + check.version);

    if (elevated) {
        if (!RegisterElevated(options, package_directory, report)) {
            return false;
        }
    } else {
        report.Line(L"Requesting Administrator privileges to register the DLLs...");
        // Removed first, so a step that dies before writing its own record
        // does not have an old one shown in its place.
        const std::wstring log = JoinPath(package_directory, L"register_elevated.log");
        DeleteFileW(log.c_str());
        std::wstring arguments = L"--register-elevated --quiet --log " + QuoteArgument(log);
        if (!options.english_profile) {
            arguments += L" --no-english";
        }
        DWORD error = 0;
        const std::optional<DWORD> code = RunElevatedSelf(arguments, error);
        if (!code) {
            report.Error(error == ERROR_CANCELLED
                             ? std::wstring(L"The Administrator permission was declined, so Neokey was not installed.")
                             : L"Could not start the Administrator step (error " + std::to_wstring(error) + L").");
            return false;
        }
        if (*code != 0) {
            EchoLog(log, report);
            report.Error(L"Failed to register DLLs. Exit code: " + std::to_wstring(*code));
            return false;
        }
        report.Line(L"DLLs registered successfully in-place.");
    }

    if (!ConfigureUser(options, package_directory, report)) {
        return false;
    }

    ClearDownloadedMark(package_directory);
    if (elevated) {
        report.Line(L"The Neokey tray app will start at the next sign-in.");
    } else {
        // The new copy has to be the one left running.
        StopTrayApp(report);
        StartTrayApp(report);
    }
    return true;
}

bool Uninstall(const SetupOptions& options, const std::wstring& package_directory, SetupReport& report) {
    report.Line(L"Uninstalling Neokey...");
    // The machine-wide half first, so a declined prompt or a refusal leaves
    // the user's working setup intact.
    if (IsProcessElevated()) {
        if (!UnregisterElevated(package_directory, report)) {
            return false;
        }
    } else {
        report.Line(L"Requesting Administrator privileges to unregister the DLLs...");
        const std::wstring log = JoinPath(package_directory, L"unregister_elevated.log");
        DeleteFileW(log.c_str());
        DWORD error = 0;
        const std::optional<DWORD> code =
            RunElevatedSelf(L"--unregister-elevated --quiet --log " + QuoteArgument(log), error);
        if (!code) {
            report.Error(error == ERROR_CANCELLED
                             ? std::wstring(L"The Administrator permission was declined, so Neokey was not removed.")
                             : L"Could not start the Administrator step (error " + std::to_wstring(error) + L").");
            return false;
        }
        if (*code != 0) {
            EchoLog(log, report);
            report.Error(L"Failed to unregister DLLs. Exit code: " + std::to_wstring(*code));
            return false;
        }
        DeleteFileW(log.c_str());
        report.Line(L"DLLs unregistered successfully.");
    }
    return UnconfigureUser(options, package_directory, report);
}

}  // namespace vn_ime::setup
