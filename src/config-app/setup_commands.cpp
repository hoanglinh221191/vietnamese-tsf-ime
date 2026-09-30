#include "setup_commands.hpp"

#include <bcrypt.h>

#include "setup_actions.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <utility>

#pragma comment(lib, "bcrypt.lib")

namespace vn_ime::setup {
namespace {

std::string WideToUtf8(std::wstring_view text) {
    if (text.empty()) {
        return {};
    }
    const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                           nullptr, 0, nullptr, nullptr);
    if (length <= 0) {
        return {};
    }
    std::string utf8(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), utf8.data(), length,
                        nullptr, nullptr);
    return utf8;
}

std::wstring Utf8ToWide(std::string_view text) {
    if (text.empty()) {
        return {};
    }
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (length <= 0) {
        return {};
    }
    std::wstring wide(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
    return wide;
}

const wchar_t* BoolText(bool value) noexcept {
    return value ? L"True" : L"False";
}

std::wstring JoinList(const std::vector<std::wstring>& items) {
    std::wstring joined;
    for (const std::wstring& item : items) {
        if (!joined.empty()) {
            joined += L", ";
        }
        joined += item;
    }
    return joined;
}

bool ListContainsIgnoreCase(const std::vector<std::wstring>& items, std::wstring_view wanted) {
    return std::any_of(items.begin(), items.end(),
                       [&](const std::wstring& item) { return EqualsIgnoreCase(item, wanted); });
}

std::wstring EnvironmentValue(const wchar_t* name) {
    const DWORD length = GetEnvironmentVariableW(name, nullptr, 0);
    if (length == 0) {
        return {};
    }
    std::wstring value(length, L'\0');
    const DWORD written = GetEnvironmentVariableW(name, value.data(), length);
    value.resize(written);
    return value;
}

// ---------------------------------------------------------------------------
// Registry, always through the 64-bit view: the 32-bit registration lives
// under the explicit Wow6432Node paths, exactly as register.ps1 read it.
// ---------------------------------------------------------------------------

class RegKey {
public:
    RegKey(HKEY root, const std::wstring& path) {
        if (RegOpenKeyExW(root, path.c_str(), 0, KEY_READ | KEY_WOW64_64KEY, &key_) != ERROR_SUCCESS) {
            key_ = nullptr;
        }
    }
    ~RegKey() {
        if (key_) {
            RegCloseKey(key_);
        }
    }
    RegKey(const RegKey&) = delete;
    RegKey& operator=(const RegKey&) = delete;

    explicit operator bool() const noexcept { return key_ != nullptr; }
    HKEY get() const noexcept { return key_; }

private:
    HKEY key_ = nullptr;
};

bool KeyExists(HKEY root, const std::wstring& path) {
    return static_cast<bool>(RegKey(root, path));
}

bool ValueExists(const RegKey& key, const wchar_t* name) {
    return key && RegQueryValueExW(key.get(), name, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
}

std::optional<std::vector<wchar_t>> ReadRawValue(const RegKey& key, const wchar_t* name, DWORD& type) {
    if (!key) {
        return std::nullopt;
    }
    DWORD size = 0;
    if (RegQueryValueExW(key.get(), name, nullptr, &type, nullptr, &size) != ERROR_SUCCESS) {
        return std::nullopt;
    }
    std::vector<wchar_t> buffer(size / sizeof(wchar_t) + 2, L'\0');
    DWORD bytes = static_cast<DWORD>(buffer.size() * sizeof(wchar_t));
    if (RegQueryValueExW(key.get(), name, nullptr, &type, reinterpret_cast<BYTE*>(buffer.data()), &bytes) !=
        ERROR_SUCCESS) {
        return std::nullopt;
    }
    buffer.resize(bytes / sizeof(wchar_t));
    return buffer;
}

std::optional<std::wstring> ReadString(const RegKey& key, const wchar_t* name) {
    DWORD type = 0;
    const auto raw = ReadRawValue(key, name, type);
    if (!raw || (type != REG_SZ && type != REG_EXPAND_SZ)) {
        return std::nullopt;
    }
    std::wstring value(raw->begin(), raw->end());
    while (!value.empty() && value.back() == L'\0') {
        value.pop_back();
    }
    return value;
}

std::vector<std::wstring> ReadMultiString(const RegKey& key, const wchar_t* name) {
    DWORD type = 0;
    const auto raw = ReadRawValue(key, name, type);
    std::vector<std::wstring> values;
    if (!raw || (type != REG_MULTI_SZ && type != REG_SZ)) {
        return values;
    }
    std::wstring current;
    for (const wchar_t ch : *raw) {
        if (ch == L'\0') {
            if (!current.empty()) {
                values.push_back(current);
            }
            current.clear();
        } else {
            current.push_back(ch);
        }
    }
    if (!current.empty()) {
        values.push_back(current);
    }
    return values;
}

// A number stored either way: register.ps1 wrote some records as DWORD and
// some as strings, and a hand edit can turn one into the other.
std::optional<DWORD> ReadNumber(const RegKey& key, const wchar_t* name) {
    if (!key) {
        return std::nullopt;
    }
    DWORD type = 0;
    DWORD value = 0;
    DWORD size = sizeof(value);
    if (RegQueryValueExW(key.get(), name, nullptr, &type, reinterpret_cast<BYTE*>(&value), &size) ==
            ERROR_SUCCESS &&
        type == REG_DWORD && size == sizeof(value)) {
        return value;
    }
    const auto text = ReadString(key, name);
    if (!text) {
        return std::nullopt;
    }
    const std::wstring trimmed = TrimWhitespace(*text);
    if (trimmed.empty()) {
        return std::nullopt;
    }
    wchar_t* end = nullptr;
    const unsigned long parsed = std::wcstoul(trimmed.c_str(), &end, 10);
    if (end == nullptr || *end != L'\0') {
        return std::nullopt;
    }
    return static_cast<DWORD>(parsed);
}

struct NamedValue {
    std::wstring name;
    DWORD type = REG_NONE;
    std::vector<BYTE> data;
};

std::vector<NamedValue> EnumerateValues(const RegKey& key) {
    std::vector<NamedValue> values;
    if (!key) {
        return values;
    }
    DWORD max_name = 0;
    DWORD max_data = 0;
    if (RegQueryInfoKeyW(key.get(), nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &max_name,
                         &max_data, nullptr, nullptr) != ERROR_SUCCESS) {
        return values;
    }
    std::vector<wchar_t> name(max_name + 2);
    std::vector<BYTE> data(max_data + 4);
    for (DWORD index = 0;; ++index) {
        DWORD name_length = static_cast<DWORD>(name.size());
        DWORD data_length = static_cast<DWORD>(data.size());
        DWORD type = 0;
        const LSTATUS status = RegEnumValueW(key.get(), index, name.data(), &name_length, nullptr, &type,
                                             data.data(), &data_length);
        if (status == ERROR_NO_MORE_ITEMS) {
            break;
        }
        if (status != ERROR_SUCCESS) {
            continue;
        }
        NamedValue value;
        value.name.assign(name.data(), name_length);
        value.type = type;
        value.data.assign(data.begin(), data.begin() + data_length);
        values.push_back(std::move(value));
    }
    return values;
}

std::wstring ValueText(const NamedValue& value) {
    if (value.type != REG_SZ && value.type != REG_EXPAND_SZ) {
        return {};
    }
    std::wstring text(reinterpret_cast<const wchar_t*>(value.data.data()), value.data.size() / sizeof(wchar_t));
    while (!text.empty() && text.back() == L'\0') {
        text.pop_back();
    }
    return text;
}

// ---------------------------------------------------------------------------
// The input lists Windows keeps for a user
// ---------------------------------------------------------------------------

constexpr wchar_t kUserProfileKey[] = L"Control Panel\\International\\User Profile";

struct UserLanguage {
    std::wstring tag;
    std::vector<std::wstring> input_method_tips;
};

// What Get-WinUserLanguageList returns, read from where it keeps it: the
// order of languages in "Languages", and each language's input methods as
// values of its own subkey, ordered by their number.
std::vector<UserLanguage> ReadUserLanguageList(HKEY root, const std::wstring& profile_key) {
    std::vector<UserLanguage> languages;
    const RegKey profile(root, profile_key);
    for (const std::wstring& tag : ReadMultiString(profile, L"Languages")) {
        UserLanguage language;
        language.tag = tag;
        const RegKey language_key(root, profile_key + L"\\" + tag);
        std::vector<std::pair<DWORD, std::wstring>> ordered;
        for (const NamedValue& value : EnumerateValues(language_key)) {
            if (value.type != REG_DWORD || value.data.size() != sizeof(DWORD) ||
                value.name.find(L':') == std::wstring::npos) {
                continue;
            }
            DWORD order = 0;
            std::memcpy(&order, value.data.data(), sizeof(order));
            ordered.emplace_back(order, value.name);
        }
        std::stable_sort(ordered.begin(), ordered.end(),
                         [](const auto& left, const auto& right) { return left.first < right.first; });
        for (auto& entry : ordered) {
            language.input_method_tips.push_back(std::move(entry.second));
        }
        languages.push_back(std::move(language));
    }
    return languages;
}

bool LanguageListHasTip(const std::vector<UserLanguage>& languages, std::wstring_view language_prefix,
                        std::wstring_view tip) {
    for (const UserLanguage& language : languages) {
        if (!StartsWithIgnoreCase(language.tag, language_prefix)) {
            continue;
        }
        if (ListContainsIgnoreCase(language.input_method_tips, tip)) {
            return true;
        }
    }
    return false;
}

// Preload names its values 1, 2, ...; CTF's SortOrder names them 00000000,
// 00000001, ...: both are hex indexes, and the order is theirs.
std::vector<std::wstring> ReadInputListOrder(HKEY root, const std::wstring& path) {
    std::vector<std::pair<unsigned long, std::wstring>> entries;
    const RegKey key(root, path);
    for (const NamedValue& value : EnumerateValues(key)) {
        if (value.name.empty()) {
            continue;
        }
        wchar_t* end = nullptr;
        const unsigned long index = std::wcstoul(value.name.c_str(), &end, 16);
        if (end == nullptr || *end != L'\0' || value.name[0] == L'-' || value.name[0] == L'+' ||
            value.name.size() > 8) {
            continue;
        }
        entries.emplace_back(index, ToLowerAscii(TrimWhitespace(ValueText(value))));
    }
    std::stable_sort(entries.begin(), entries.end(),
                     [](const auto& left, const auto& right) { return left.first < right.first; });
    std::vector<std::wstring> order;
    for (auto& entry : entries) {
        order.push_back(std::move(entry.second));
    }
    return order;
}

// ---------------------------------------------------------------------------
// Files
// ---------------------------------------------------------------------------

std::optional<std::string> ReadSmallFile(const std::wstring& path, size_t limit) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return std::nullopt;
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 || static_cast<uint64_t>(size.QuadPart) > limit) {
        CloseHandle(file);
        return std::nullopt;
    }
    std::string contents(static_cast<size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    const BOOL ok = contents.empty() ||
                    ReadFile(file, contents.data(), static_cast<DWORD>(contents.size()), &read, nullptr);
    CloseHandle(file);
    if (!ok || read != contents.size()) {
        return std::nullopt;
    }
    return contents;
}

struct FileFacts {
    bool exists = false;
    bool is_directory = false;
    uint64_t size = 0;
    FILETIME modified{};
};

FileFacts StatPath(const std::wstring& path) {
    FileFacts facts;
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
        return facts;
    }
    facts.exists = true;
    facts.is_directory = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    facts.size = (static_cast<uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
    facts.modified = data.ftLastWriteTime;
    return facts;
}

std::wstring FormatLocalTime(const FILETIME& time) {
    SYSTEMTIME utc{};
    SYSTEMTIME local{};
    if (!FileTimeToSystemTime(&time, &utc) || !SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local)) {
        return L"<unknown>";
    }
    wchar_t text[32] = {};
    swprintf_s(text, L"%04u-%02u-%02u %02u:%02u:%02u", local.wYear, local.wMonth, local.wDay, local.wHour,
               local.wMinute, local.wSecond);
    return text;
}

std::wstring ParentDirectory(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
}

std::wstring LeafName(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

// The manifest in a folder, read for what it says rather than checked: a
// package missing a file still has a version to report.
std::optional<ArtifactManifest> ReadManifestLoosely(const std::wstring& directory) {
    const auto contents = ReadSmallFile(JoinPath(directory, kManifestFileName), 1 << 20);
    if (!contents) {
        return std::nullopt;
    }
    ManifestResult parsed = ParseArtifactManifest(*contents, {});
    return parsed.manifest;
}

std::wstring PackageVersion(const std::wstring& directory) {
    if (const auto manifest = ReadManifestLoosely(directory); manifest && !manifest->version.empty()) {
        return manifest->version;
    }
    if (const auto version = ReadSmallFile(JoinPath(directory, L"VERSION"), 4096)) {
        std::string text = *version;
        if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF) {
            text.erase(0, 3);
        }
        const std::wstring trimmed = TrimWhitespace(Utf8ToWide(text));
        if (!trimmed.empty()) {
            return trimmed;
        }
    }
    return L"<unknown>";
}

void WriteRegisteredFileStatus(SetupReport& report, const std::wstring& label,
                               const std::optional<std::wstring>& path) {
    if (!path || TrimWhitespace(*path).empty()) {
        report.Line(label + L" path: <not registered>");
        return;
    }
    report.Line(label + L" path: " + *path);
    const FileFacts facts = StatPath(*path);
    if (!facts.exists || facts.is_directory) {
        report.Line(label + L" file: missing");
        return;
    }
    const std::optional<std::wstring> hash = Sha256FileHex(*path);
    report.Line(label + L" file: size=" + std::to_wstring(facts.size) + L", modified=" +
                FormatLocalTime(facts.modified) + L", sha256=" + hash.value_or(L"<unreadable>"));

    const std::wstring directory = ParentDirectory(*path);
    const std::wstring leaf = LeafName(*path);
    const auto manifest = ReadManifestLoosely(directory);
    const ManifestEntry* entry = nullptr;
    if (manifest) {
        for (const ManifestEntry& candidate : manifest->files) {
            if (EqualsIgnoreCase(candidate.path, leaf)) {
                entry = &candidate;
                break;
            }
        }
    }
    if (entry == nullptr) {
        report.Line(label + L" manifest: <not found>");
        return;
    }
    const bool hash_matches = hash && *hash == entry->sha256;
    const bool size_matches = entry->bytes == facts.size;
    report.Line(label + L" manifest: version=" + PackageVersion(directory) + L", hash_match=" +
                BoolText(hash_matches) + L", size_match=" + BoolText(size_matches));
}

bool ResidueTargetPresent(const ResidueTarget& target) {
    switch (target.kind) {
        case ResidueKind::RegistryKey:
            return KeyExists(HKEY_CURRENT_USER, target.path);
        case ResidueKind::RegistryValue: {
            const RegKey key(HKEY_CURRENT_USER, target.path);
            return ValueExists(key, target.value_name.c_str());
        }
        case ResidueKind::Directory:
        case ResidueKind::File:
            return StatPath(target.path).exists;
    }
    return false;
}

bool ShowFailureWindow(const SetupReport& report, const SetupOptions& options, const std::wstring& text) {
    if (report.HasConsole() || options.quiet) {
        return false;
    }
    MessageBoxW(nullptr, text.c_str(), L"Neokey", MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
    return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// SetupReport
// ---------------------------------------------------------------------------

SetupReport::SetupReport(const std::wstring& log_path, bool append) {
    // Redirected output (a file or a pipe) is inherited as a standard handle;
    // a console the exe was typed into has to be attached to, since a GUI
    // program gets none of its own.
    HANDLE standard = GetStdHandle(STD_OUTPUT_HANDLE);
    if (standard != nullptr && standard != INVALID_HANDLE_VALUE) {
        DWORD mode = 0;
        output_ = standard;
        output_is_console_ = GetConsoleMode(standard, &mode) != FALSE;
    } else if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        HANDLE console = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                     nullptr, OPEN_EXISTING, 0, nullptr);
        if (console != INVALID_HANDLE_VALUE) {
            output_ = console;
            output_is_console_ = true;
            owns_output_ = true;
            // The prompt was printed before this process wrote anything.
            DWORD written = 0;
            WriteConsoleW(output_, L"\r\n", 2, &written, nullptr);
        }
    }
    if (!log_path.empty()) {
        log_ = CreateFileW(log_path.c_str(),
                           (append ? FILE_APPEND_DATA : GENERIC_WRITE) | FILE_READ_ATTRIBUTES, FILE_SHARE_READ, nullptr,
                           append ? OPEN_ALWAYS : CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        LARGE_INTEGER size{};
        if (log_ != INVALID_HANDLE_VALUE && GetFileSizeEx(log_, &size) && size.QuadPart == 0) {
            // With the mark, Notepad and Windows PowerShell both read it as UTF-8.
            static const char kBom[] = {'\xEF', '\xBB', '\xBF'};
            DWORD written = 0;
            WriteFile(log_, kBom, sizeof(kBom), &written, nullptr);
        }
    }
}

SetupReport::~SetupReport() {
    if (log_ != INVALID_HANDLE_VALUE) {
        CloseHandle(log_);
    }
    if (owns_output_ && output_ != nullptr) {
        CloseHandle(output_);
    }
}

void SetupReport::Write(std::wstring_view text) {
    lines_.emplace_back(text);
    std::wstring line(text);
    line += L"\r\n";
    if (output_ != nullptr) {
        DWORD written = 0;
        if (output_is_console_) {
            WriteConsoleW(output_, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
        } else {
            const std::string utf8 = WideToUtf8(line);
            WriteFile(output_, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
        }
    }
    if (log_ != INVALID_HANDLE_VALUE) {
        const std::string utf8 = WideToUtf8(line);
        DWORD written = 0;
        WriteFile(log_, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    }
}

void SetupReport::Line(std::wstring_view text) {
    Write(text);
}

void SetupReport::Warning(std::wstring_view text) {
    Write(L"WARNING: " + std::wstring(text));
}

void SetupReport::Error(std::wstring_view text) {
    Write(L"ERROR: " + std::wstring(text));
}

// ---------------------------------------------------------------------------
// Hashing and the package
// ---------------------------------------------------------------------------

std::optional<std::wstring> Sha256FileHex(const std::wstring& path) {
    // Shared every way: a DLL that apps have loaded is still readable.
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return std::nullopt;
    }

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::optional<std::wstring> result;
    if (BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0)) &&
        BCRYPT_SUCCESS(BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0))) {
        std::vector<unsigned char> buffer(1 << 16);
        bool ok = true;
        for (;;) {
            DWORD read = 0;
            if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr)) {
                ok = false;
                break;
            }
            if (read == 0) {
                break;
            }
            if (!BCRYPT_SUCCESS(BCryptHashData(hash, buffer.data(), read, 0))) {
                ok = false;
                break;
            }
        }
        unsigned char digest[32] = {};
        if (ok && BCRYPT_SUCCESS(BCryptFinishHash(hash, digest, sizeof(digest), 0))) {
            static constexpr wchar_t kHex[] = L"0123456789abcdef";
            std::wstring hex;
            hex.reserve(64);
            for (const unsigned char byte : digest) {
                hex.push_back(kHex[byte >> 4]);
                hex.push_back(kHex[byte & 0x0F]);
            }
            result = std::move(hex);
        }
    }
    if (hash) {
        BCryptDestroyHash(hash);
    }
    if (algorithm) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
    }
    CloseHandle(file);
    return result;
}

std::wstring ExecutableDirectory() {
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (length == 0) {
            return {};
        }
        if (length < path.size()) {
            path.resize(length);
            break;
        }
        path.resize(path.size() * 2);
    }
    return ParentDirectory(path);
}

PackageCheck CheckPackage(const std::wstring& directory, const std::vector<std::wstring>& required_files) {
    PackageCheck check;
    const std::wstring manifest_path = JoinPath(directory, kManifestFileName);
    const auto contents = ReadSmallFile(manifest_path, 1 << 20);
    if (!contents) {
        check.problem = StatPath(manifest_path).exists ? PackageProblem::ManifestInvalid
                                                       : PackageProblem::ManifestMissing;
        check.detail = check.problem == PackageProblem::ManifestMissing ? manifest_path
                                                                        : L"it cannot be read";
        return check;
    }
    ManifestResult parsed = ParseArtifactManifest(*contents, required_files);
    if (!parsed.manifest) {
        check.problem = PackageProblem::ManifestInvalid;
        check.detail = parsed.error;
        return check;
    }
    check.version = parsed.manifest->version;

    for (const ManifestEntry& entry : parsed.manifest->files) {
        std::wstring relative = entry.path;
        std::replace(relative.begin(), relative.end(), L'/', L'\\');
        const std::wstring path = JoinPath(directory, relative);
        const FileFacts facts = StatPath(path);
        if (!facts.exists || facts.is_directory) {
            check.problem = PackageProblem::FileMissing;
            check.detail = path;
            return check;
        }
        if (facts.size != entry.bytes) {
            check.problem = PackageProblem::SizeMismatch;
            check.detail = entry.path;
            check.expected_bytes = entry.bytes;
            check.actual_bytes = facts.size;
            return check;
        }
        const std::optional<std::wstring> hash = Sha256FileHex(path);
        if (!hash) {
            check.problem = PackageProblem::FileUnreadable;
            check.detail = path;
            return check;
        }
        if (*hash != entry.sha256) {
            check.problem = PackageProblem::HashMismatch;
            check.detail = entry.path;
            return check;
        }
    }
    return check;
}

// ---------------------------------------------------------------------------
// --status
// ---------------------------------------------------------------------------

void WriteStatus(SetupReport& report, const std::wstring& package_directory) {
    report.Line(L"Checking registration status...");
    report.Line(L"This package version: " + PackageVersion(package_directory));
    // What decides whether opening this exe offers to install, and whether
    // the tray offers to uninstall.
    const InstallState install_state = ReadInstallState(package_directory);
    report.Line(std::wstring(L"Registered from this folder: ") + BoolText(install_state.registered_here));
    report.Line(std::wstring(L"Installed by NeokeySetup.exe: ") + BoolText(install_state.installed_by_setup));

    const std::wstring clsid_key = std::wstring(L"Software\\Classes\\CLSID\\") + kClsid;
    const std::wstring clsid_key32 = std::wstring(L"Software\\Classes\\Wow6432Node\\CLSID\\") + kClsid;

    const bool com64 = KeyExists(HKEY_LOCAL_MACHINE, clsid_key) || KeyExists(HKEY_CURRENT_USER, clsid_key);
    report.Line(std::wstring(L"64-bit COM DLL Registered: ") + BoolText(com64));
    WriteRegisteredFileStatus(report, L"64-bit HKLM",
                              ReadString(RegKey(HKEY_LOCAL_MACHINE, clsid_key + L"\\InprocServer32"), L""));
    WriteRegisteredFileStatus(report, L"64-bit HKCU",
                              ReadString(RegKey(HKEY_CURRENT_USER, clsid_key + L"\\InprocServer32"), L""));

    const bool com32 = KeyExists(HKEY_LOCAL_MACHINE, clsid_key32) || KeyExists(HKEY_CURRENT_USER, clsid_key32);
    report.Line(std::wstring(L"32-bit COM DLL Registered: ") + BoolText(com32));
    WriteRegisteredFileStatus(report, L"32-bit HKLM",
                              ReadString(RegKey(HKEY_LOCAL_MACHINE, clsid_key32 + L"\\InprocServer32"), L""));
    WriteRegisteredFileStatus(report, L"32-bit HKCU",
                              ReadString(RegKey(HKEY_CURRENT_USER, clsid_key32 + L"\\InprocServer32"), L""));

    const std::vector<UserLanguage> languages = ReadUserLanguageList(HKEY_CURRENT_USER, kUserProfileKey);
    report.Line(std::wstring(L"TIP in User Language List: ") +
                BoolText(LanguageListHasTip(languages, L"vi", kVietnameseTip)));

    // Reported from what is on the machine, not from the switches this run
    // was given.
    const RegKey settings(HKEY_CURRENT_USER, L"Software\\Neokey");
    const std::optional<DWORD> english_requested = ReadNumber(settings, L"RegisterEnglishProfile");
    report.Line(std::wstring(L"English copy registered: ") +
                BoolText(english_requested.has_value() && *english_requested != 0));
    report.Line(std::wstring(L"English copy in User Language List: ") +
                BoolText(LanguageListHasTip(languages, L"en", kEnglishTip)));

    const RegKey user_profile(HKEY_CURRENT_USER, kUserProfileKey);
    const std::wstring default_tip = TrimWhitespace(ReadString(user_profile, L"InputMethodOverride").value_or(L""));
    report.Line(L"Default Input Method TIP: " +
                (default_tip.empty() ? std::wstring(L"<dynamic Windows selection>") : default_tip));
    report.Line(std::wstring(L"Neokey is Default Input Method: ") +
                BoolText(EqualsIgnoreCase(default_tip, kVietnameseTip)));

    // The two input lists disagreeing is what lets a sign-in come back on the
    // wrong IME.
    const std::vector<std::wstring> preload = ReadInputListOrder(HKEY_CURRENT_USER, L"Keyboard Layout\\Preload");
    const std::vector<std::wstring> ctf =
        ReadInputListOrder(HKEY_CURRENT_USER, L"Software\\Microsoft\\CTF\\SortOrder\\Language");
    report.Line(L"Win32 input order (Preload): " + JoinList(preload));
    report.Line(L"CTF input order (SortOrder): " + JoinList(ctf));
    const bool orders_agree = preload == ctf;
    report.Line(std::wstring(L"Input orders agree: ") + BoolText(orders_agree));
    if (!orders_agree) {
        report.Warning(L"The Win32 and CTF input lists disagree. Windows re-resolves them at sign-in and may activate another language's IME. Reinstall Neokey to fix.");
    }

    // Windows asks the language list first, twice: once for the signed-in
    // user and once for the screen you unlock at.
    std::vector<std::wstring> language_order;
    for (const UserLanguage& language : languages) {
        language_order.push_back(language.tag);
    }
    report.Line(L"Language order (user): " + JoinList(language_order));
    const bool user_resolves = InputListResolvesToNeokey(default_tip, language_order, kVietnameseTip, L"vi");
    report.Line(std::wstring(L"User session starts on Neokey: ") + BoolText(user_resolves));
    if (!user_resolves) {
        report.Warning(L"Your own session does not start on Neokey. Reinstall Neokey to fix.");
    }

    const RegKey sign_in(HKEY_USERS, std::wstring(L".DEFAULT\\") + kUserProfileKey);
    if (!sign_in) {
        report.Line(L"Sign-in screen input settings: <not readable>");
    } else {
        const std::wstring sign_in_override =
            TrimWhitespace(ReadString(sign_in, L"InputMethodOverride").value_or(L""));
        const std::vector<std::wstring> sign_in_languages = ReadMultiString(sign_in, L"Languages");
        report.Line(L"Sign-in screen override: " +
                    (sign_in_override.empty() ? std::wstring(L"<none>") : sign_in_override));
        report.Line(L"Sign-in screen language order: " + JoinList(sign_in_languages));
        const bool sign_in_resolves =
            InputListResolvesToNeokey(sign_in_override, sign_in_languages, kVietnameseTip, L"vi");
        report.Line(std::wstring(L"Sign-in screen starts on Neokey: ") + BoolText(sign_in_resolves));
        if (user_resolves && !sign_in_resolves) {
            report.Warning(L"The sign-in screen starts on another input method, and unlocking carries that choice into the session - so waking the machine from sleep comes back on the wrong keyboard even though your own settings are right. Fix it in Settings > Time & language > Language & region > Administrative language settings > Copy settings, ticking 'Welcome screen and system accounts'.");
        }
    }

    // Without the substitute Windows binds 0x042a to its own Vietnamese layout,
    // where the number row types tone marks.
    const RegKey substitutes(HKEY_CURRENT_USER, L"Keyboard Layout\\Substitutes");
    const std::wstring substitute = TrimWhitespace(ReadString(substitutes, L"0000042a").value_or(L""));
    if (substitute.empty()) {
        report.Line(L"Vietnamese layout substitute: <not set>");
        report.Warning(L"Without the substitute Windows may bind the Vietnamese physical layout, where the number row types tone marks and VNI cannot be typed. Reinstall Neokey to fix.");
    } else {
        report.Line(L"Vietnamese layout substitute: " + substitute);
        if (!EqualsIgnoreCase(substitute, L"00000409")) {
            report.Warning(L"The Vietnamese language is bound to layout " + substitute +
                           L" rather than the US layout 00000409. Reinstall Neokey to fix.");
        }
    }

    // Whether this install can put Vietnamese back the way it found it.
    const std::optional<std::wstring> recorded = ReadString(settings, L"PreviousVietnameseInputMethods");
    if (!recorded) {
        report.Line(L"Vietnamese state before Neokey: not recorded (installed by an earlier build)");
    } else if (recorded->empty()) {
        report.Line(L"Vietnamese state before Neokey: no keyboards were filed under it");
    } else {
        report.Line(L"Vietnamese state before Neokey: " + *recorded);
    }
    const std::optional<DWORD> added = ReadNumber(settings, L"AddedVietnameseLanguage");
    report.Line(std::wstring(L"Vietnamese language entry added by Neokey: ") +
                BoolText(added.has_value() && *added == 1));

    // On a working install this reads as what is in use; after an uninstall,
    // as what the uninstall failed to remove.
    report.Line(L"Machine footprint:");
    for (const ResidueTarget& target :
         BuildResidueTargets(EnvironmentValue(L"TEMP"), EnvironmentValue(L"LOCALAPPDATA"),
                             EnvironmentValue(L"SystemRoot"), package_directory)) {
        report.Line(std::wstring(L"  [") + (ResidueTargetPresent(target) ? L"x" : L" ") + L"] " +
                    target.DisplayName() + L" (" + target.label + L")");
    }

    const RegKey run(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run");
    const std::wstring autostart = ReadString(run, L"Neokey").value_or(L"");
    if (TrimWhitespace(autostart).empty()) {
        report.Line(L"Starts with Windows: False");
    } else {
        report.Line(L"Starts with Windows: True (" + autostart + L")");
    }
}

// ---------------------------------------------------------------------------
// Dispatch
// ---------------------------------------------------------------------------

namespace {

// The last word of an install or uninstall for someone who double-clicked:
// what happened, and on a failure the reason and where the full report is.
void ShowOutcome(const SetupReport& report, const SetupOptions& options, bool succeeded,
                 const std::wstring& version) {
    if (report.HasConsole() || options.quiet) {
        return;
    }
    const bool vietnamese = UserPrefersVietnamese();
    const bool installing = options.action == SetupAction::Install;
    std::wstring text;
    if (succeeded && installing) {
        text = vietnamese
            ? L"Đã cài đặt Neokey " + version + L".\n\nNeokey đã được đặt làm bộ gõ mặc định (VIE). Dùng Win + Space để chuyển giữa ENG và VIE.\n\nHãy giữ thư mục này ở nguyên vị trí. Các ứng dụng đang mở sẽ dùng Neokey sau khi được đóng và mở lại."
            : L"Neokey " + version + L" is installed.\n\nNeokey is now the default input method (VIE). Use Win + Space to switch between ENG and VIE.\n\nKeep this folder where it is. Apps that are already open use Neokey once they are closed and reopened.";
    } else if (succeeded) {
        text = vietnamese
            ? std::wstring(L"Đã gỡ Neokey.\n\nHãy đóng và mở lại các ứng dụng đang chạy: mỗi ứng dụng vẫn giữ bộ gõ đến khi được khởi động lại. Giờ có thể xóa thư mục này.")
            : std::wstring(L"Neokey was removed.\n\nClose and reopen your applications: each one keeps the input method until it is restarted. You can now delete this folder.");
    } else {
        text = vietnamese ? (installing ? L"Chưa cài đặt được Neokey.\n\n" : L"Chưa gỡ được Neokey.\n\n")
                          : (installing ? L"Neokey could not be installed.\n\n" : L"Neokey could not be removed.\n\n");
        for (const std::wstring& line : report.Lines()) {
            if (line.rfind(L"ERROR: ", 0) == 0) {
                text += line.substr(7) + L"\n\n";
            }
        }
        if (!options.log_path.empty()) {
            text += (vietnamese ? L"Chi tiết: " : L"Details: ") + options.log_path;
        }
    }
    MessageBoxW(nullptr, text.c_str(), L"Neokey",
                MB_OK | MB_SETFOREGROUND | (succeeded ? MB_ICONINFORMATION : MB_ICONERROR));
}

}  // namespace

int RunSetupCommand(const SetupOptions& requested) {
    SetupOptions options = requested;
    // An install or uninstall started by a double-click has no console, so
    // what it said has to be kept somewhere a support thread can ask for -
    // both runs in the one file, each under a heading of its own.
    const bool whole_job = options.action == SetupAction::Install || options.action == SetupAction::Uninstall;
    bool default_log = false;
    if (whole_job && options.log_path.empty()) {
        const std::wstring temp = EnvironmentValue(L"TEMP");
        if (!temp.empty()) {
            options.log_path = JoinPath(temp, L"neokey_setup.log");
            default_log = true;
        }
    }
    SetupReport report(options.log_path, default_log);
    if (whole_job) {
        SYSTEMTIME now{};
        GetLocalTime(&now);
        wchar_t stamp[32] = {};
        swprintf_s(stamp, L"%04u-%02u-%02u %02u:%02u:%02u", now.wYear, now.wMonth, now.wDay, now.wHour,
                   now.wMinute, now.wSecond);
        report.Line(std::wstring(L"==== Neokey ") + PackageVersion(ExecutableDirectory()) +
                    (options.action == SetupAction::Install ? L" install, " : L" uninstall, ") + stamp + L" ====");
    }
    if (!options.error.empty()) {
        report.Error(options.error);
        ShowFailureWindow(report, options, options.error);
        return 2;
    }

    const std::wstring package_directory = ExecutableDirectory();
    switch (options.action) {
        case SetupAction::RegisterElevated:
            return RegisterElevated(options, package_directory, report) ? 0 : 1;
        case SetupAction::UnregisterElevated:
            return UnregisterElevated(package_directory, report) ? 0 : 1;
        case SetupAction::ConfigureUser:
            return ConfigureUser(options, package_directory, report) ? 0 : 1;
        case SetupAction::UnconfigureUser:
            return UnconfigureUser(options, package_directory, report) ? 0 : 1;
        case SetupAction::Install:
        case SetupAction::Uninstall: {
            const bool succeeded = options.action == SetupAction::Install
                                       ? Install(options, package_directory, report)
                                       : Uninstall(options, package_directory, report);
            ShowOutcome(report, options, succeeded, PackageVersion(package_directory));
            return succeeded ? 0 : 1;
        }
        default:
            break;
    }
    switch (options.action) {
        case SetupAction::Status:
            WriteStatus(report, package_directory);
            return 0;

        case SetupAction::VerifyPackage: {
            const PackageCheck check = CheckPackage(package_directory, PortableRequiredFiles());
            if (check.problem != PackageProblem::None) {
                const std::wstring message = DescribePackageProblem(check, false);
                report.Error(message);
                ShowFailureWindow(report, options, message);
                return 1;
            }
            report.Line(L"Release artifact hashes verified. Version: " +
                        (check.version.empty() ? std::wstring(L"<unknown>") : check.version));
            return 0;
        }

        case SetupAction::None:
            return 0;

        default:
            return 2;
    }
}

}  // namespace vn_ime::setup
