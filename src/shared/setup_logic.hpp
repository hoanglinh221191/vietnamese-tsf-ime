#pragma once

// The decisions behind installing and removing Neokey that do not touch the
// machine: what a command line asks for, whether a release manifest is well
// formed, whether a folder can hold a registered input method, and which input
// method Windows will start on. Kept apart from the code that acts on them so
// each one can be tested on its own, the way register.ps1's were.

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cwctype>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vn_ime::setup {

inline constexpr wchar_t kClsid[] = L"{A85F2C8C-7DE6-4F7F-9B67-4EBEA54D4A4B}";
inline constexpr wchar_t kProfileGuid[] = L"{4B6925B4-1E4E-40BC-BDD3-C26BA333CD12}";
// A TIP is named by language as well as by class and profile, so these name two
// entries backed by one DLL rather than two installations.
inline constexpr wchar_t kVietnameseTip[] =
    L"042A:{A85F2C8C-7DE6-4F7F-9B67-4EBEA54D4A4B}{4B6925B4-1E4E-40BC-BDD3-C26BA333CD12}";
inline constexpr wchar_t kEnglishTip[] =
    L"0409:{A85F2C8C-7DE6-4F7F-9B67-4EBEA54D4A4B}{4B6925B4-1E4E-40BC-BDD3-C26BA333CD12}";
inline constexpr wchar_t kManifestFileName[] = L"neokey_manifest.json";

// Every file a portable package must carry, and the manifest must vouch for.
// package.ps1 writes the manifest from its own copy of this list.
inline const std::vector<std::wstring>& PortableRequiredFiles() {
    static const std::vector<std::wstring> files = {
        L"neokey.dll",
        L"neokey32.dll",
        L"neokey_config.exe",
        L"register.ps1",
        L"install.bat",
        L"uninstall.bat",
        L"PORTABLE_RELEASE.md",
        L"README.md",
        L"README.vi.md",
        L"LICENSE",
        L"THIRD_PARTY_NOTICES.md",
        L"VERSION",
        L"neokey_shorthand.txt",
    };
    return files;
}

inline wchar_t FoldAscii(wchar_t ch) noexcept {
    return (ch >= L'A' && ch <= L'Z') ? static_cast<wchar_t>(ch - L'A' + L'a') : ch;
}

inline bool EqualsIgnoreCase(std::wstring_view left, std::wstring_view right) noexcept {
    if (left.size() != right.size()) {
        return false;
    }
    return CompareStringOrdinal(left.data(), static_cast<int>(left.size()),
                                right.data(), static_cast<int>(right.size()),
                                TRUE) == CSTR_EQUAL;
}

inline bool StartsWithIgnoreCase(std::wstring_view text, std::wstring_view prefix) noexcept {
    return text.size() >= prefix.size() && EqualsIgnoreCase(text.substr(0, prefix.size()), prefix);
}

inline std::wstring ToLowerAscii(std::wstring_view text) {
    std::wstring lowered(text);
    for (wchar_t& ch : lowered) {
        ch = FoldAscii(ch);
    }
    return lowered;
}

inline std::wstring TrimWhitespace(std::wstring_view text) {
    size_t begin = 0;
    size_t end = text.size();
    while (begin < end && std::iswspace(text[begin])) {
        ++begin;
    }
    while (end > begin && std::iswspace(text[end - 1])) {
        --end;
    }
    return std::wstring(text.substr(begin, end - begin));
}

// ---------------------------------------------------------------------------
// Command line
// ---------------------------------------------------------------------------

enum class SetupAction {
    None,                // not a setup command: the tray app runs as usual
    Status,              // --status
    VerifyPackage,       // --verify
    Install,             // --install
    Uninstall,           // --uninstall
    RegisterElevated,    // --register-elevated (the Administrator half of --install)
    UnregisterElevated,  // --unregister-elevated (the Administrator half of --uninstall)
    ConfigureUser,       // --configure-user (the installer's per-user step)
    UnconfigureUser,     // --unconfigure-user (the uninstaller's per-user step)
};

struct SetupOptions {
    SetupAction action = SetupAction::None;
    bool english_profile = true;
    bool keep_user_data = false;
    // No dialogs: the installer and scripts read the exit code and the report.
    bool quiet = false;
    // Where the report also goes. The Administrator half is told this by the
    // half that started it, since that is the one that shows it.
    std::wstring log_path;
    std::wstring error;
};

// Arguments after the program name. "-silent" belongs to the tray app and is
// left alone; setup switches all start with "--", so the two cannot be
// confused, and one command line cannot ask for both.
inline SetupOptions ParseSetupArguments(const std::vector<std::wstring>& arguments) {
    SetupOptions options;
    bool saw_silent = false;
    bool saw_option = false;

    auto set_action = [&](SetupAction action, std::wstring_view name) {
        if (options.action != SetupAction::None) {
            options.error = L"Only one setup command can be given; " + std::wstring(name) +
                            L" follows another.";
            return false;
        }
        options.action = action;
        return true;
    };

    for (size_t index = 0; index < arguments.size(); ++index) {
        const std::wstring& argument = arguments[index];
        if (EqualsIgnoreCase(argument, L"-silent")) {
            saw_silent = true;
            continue;
        }
        if (!StartsWithIgnoreCase(argument, L"--")) {
            // Anything else was never meaningful to the tray app, which only
            // ever looked for -silent; keep ignoring it there.
            if (options.action == SetupAction::None && !saw_option) {
                continue;
            }
            options.error = L"Unexpected argument: " + argument;
            return options;
        }

        const std::wstring name = ToLowerAscii(argument);
        bool ok = true;
        if (name == L"--status") {
            ok = set_action(SetupAction::Status, argument);
        } else if (name == L"--verify") {
            ok = set_action(SetupAction::VerifyPackage, argument);
        } else if (name == L"--install") {
            ok = set_action(SetupAction::Install, argument);
        } else if (name == L"--uninstall") {
            ok = set_action(SetupAction::Uninstall, argument);
        } else if (name == L"--register-elevated") {
            ok = set_action(SetupAction::RegisterElevated, argument);
        } else if (name == L"--unregister-elevated") {
            ok = set_action(SetupAction::UnregisterElevated, argument);
        } else if (name == L"--configure-user") {
            ok = set_action(SetupAction::ConfigureUser, argument);
        } else if (name == L"--unconfigure-user") {
            ok = set_action(SetupAction::UnconfigureUser, argument);
        } else if (name == L"--no-english") {
            options.english_profile = false;
            saw_option = true;
        } else if (name == L"--keep-user-data") {
            options.keep_user_data = true;
            saw_option = true;
        } else if (name == L"--quiet") {
            options.quiet = true;
            saw_option = true;
        } else if (name == L"--log") {
            if (index + 1 >= arguments.size() || arguments[index + 1].empty() ||
                StartsWithIgnoreCase(arguments[index + 1], L"--")) {
                options.error = L"--log needs a file path.";
                return options;
            }
            options.log_path = arguments[++index];
            saw_option = true;
        } else {
            options.error = L"Unknown setup option: " + argument;
            return options;
        }
        if (!ok) {
            return options;
        }
    }

    if (options.action == SetupAction::None) {
        if (saw_option) {
            options.error = L"Setup options need a command such as --install or --uninstall.";
        }
        return options;
    }
    if (saw_silent) {
        options.error = L"-silent starts the tray app and cannot be combined with a setup command.";
        return options;
    }

    // An option a command does not use would be accepted and do nothing.
    const bool registers = options.action == SetupAction::Install ||
                           options.action == SetupAction::RegisterElevated ||
                           options.action == SetupAction::ConfigureUser;
    const bool removes = options.action == SetupAction::Uninstall ||
                         options.action == SetupAction::UnconfigureUser;
    if (!options.english_profile && !registers) {
        options.error = L"--no-english only applies to --install, --register-elevated and --configure-user.";
    } else if (options.keep_user_data && !removes) {
        options.error = L"--keep-user-data only applies to --uninstall and --unconfigure-user.";
    }
    return options;
}

// ---------------------------------------------------------------------------
// Release manifest
// ---------------------------------------------------------------------------

// Enough JSON for neokey_manifest.json and nothing more: objects, arrays,
// strings with every escape, integers, true, false and null. Numbers are kept
// as their text so a byte count is read exactly rather than through a double.
struct JsonValue {
    enum class Kind { Null, Boolean, Number, String, Array, Object };
    Kind kind = Kind::Null;
    bool boolean = false;
    std::string number;
    std::wstring text;
    std::vector<JsonValue> items;
    std::vector<std::pair<std::wstring, JsonValue>> members;

    const JsonValue* Member(std::wstring_view name) const {
        for (const auto& member : members) {
            if (member.first == name) {
                return &member.second;
            }
        }
        return nullptr;
    }
};

class JsonReader {
public:
    explicit JsonReader(std::string_view input) : input_(input) {
        // Windows PowerShell's Set-Content -Encoding UTF8 writes a BOM.
        if (input_.size() >= 3 && static_cast<unsigned char>(input_[0]) == 0xEF &&
            static_cast<unsigned char>(input_[1]) == 0xBB &&
            static_cast<unsigned char>(input_[2]) == 0xBF) {
            position_ = 3;
        }
    }

    std::optional<JsonValue> ReadDocument() {
        JsonValue value;
        if (!ReadValue(value, 0)) {
            return std::nullopt;
        }
        SkipWhitespace();
        if (position_ != input_.size()) {
            return std::nullopt;
        }
        return value;
    }

private:
    static constexpr int kMaxDepth = 32;

    void SkipWhitespace() {
        while (position_ < input_.size()) {
            const char ch = input_[position_];
            if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') {
                break;
            }
            ++position_;
        }
    }

    bool Consume(char expected) {
        SkipWhitespace();
        if (position_ < input_.size() && input_[position_] == expected) {
            ++position_;
            return true;
        }
        return false;
    }

    bool ReadLiteral(std::string_view literal) {
        if (input_.substr(position_, literal.size()) != literal) {
            return false;
        }
        position_ += literal.size();
        return true;
    }

    bool ReadValue(JsonValue& value, int depth) {
        if (depth > kMaxDepth) {
            return false;
        }
        SkipWhitespace();
        if (position_ >= input_.size()) {
            return false;
        }
        const char ch = input_[position_];
        if (ch == '{') {
            return ReadObject(value, depth);
        }
        if (ch == '[') {
            return ReadArray(value, depth);
        }
        if (ch == '"') {
            value.kind = JsonValue::Kind::String;
            return ReadString(value.text);
        }
        if (ch == 't') {
            value.kind = JsonValue::Kind::Boolean;
            value.boolean = true;
            return ReadLiteral("true");
        }
        if (ch == 'f') {
            value.kind = JsonValue::Kind::Boolean;
            value.boolean = false;
            return ReadLiteral("false");
        }
        if (ch == 'n') {
            value.kind = JsonValue::Kind::Null;
            return ReadLiteral("null");
        }
        if (ch == '-' || (ch >= '0' && ch <= '9')) {
            value.kind = JsonValue::Kind::Number;
            return ReadNumber(value.number);
        }
        return false;
    }

    bool ReadObject(JsonValue& value, int depth) {
        value.kind = JsonValue::Kind::Object;
        ++position_;  // '{'
        if (Consume('}')) {
            return true;
        }
        for (;;) {
            SkipWhitespace();
            if (position_ >= input_.size() || input_[position_] != '"') {
                return false;
            }
            std::wstring name;
            if (!ReadString(name) || !Consume(':')) {
                return false;
            }
            JsonValue member;
            if (!ReadValue(member, depth + 1)) {
                return false;
            }
            value.members.emplace_back(std::move(name), std::move(member));
            if (Consume(',')) {
                continue;
            }
            return Consume('}');
        }
    }

    bool ReadArray(JsonValue& value, int depth) {
        value.kind = JsonValue::Kind::Array;
        ++position_;  // '['
        if (Consume(']')) {
            return true;
        }
        for (;;) {
            JsonValue item;
            if (!ReadValue(item, depth + 1)) {
                return false;
            }
            value.items.push_back(std::move(item));
            if (Consume(',')) {
                continue;
            }
            return Consume(']');
        }
    }

    bool ReadNumber(std::string& number) {
        const size_t start = position_;
        if (input_[position_] == '-') {
            ++position_;
        }
        const size_t digits_start = position_;
        while (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9') {
            ++position_;
        }
        if (position_ == digits_start) {
            return false;
        }
        if (input_[digits_start] == '0' && position_ - digits_start > 1) {
            return false;
        }
        if (position_ < input_.size() && input_[position_] == '.') {
            ++position_;
            const size_t fraction_start = position_;
            while (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9') {
                ++position_;
            }
            if (position_ == fraction_start) {
                return false;
            }
        }
        if (position_ < input_.size() && (input_[position_] == 'e' || input_[position_] == 'E')) {
            ++position_;
            if (position_ < input_.size() && (input_[position_] == '+' || input_[position_] == '-')) {
                ++position_;
            }
            const size_t exponent_start = position_;
            while (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9') {
                ++position_;
            }
            if (position_ == exponent_start) {
                return false;
            }
        }
        number.assign(input_.substr(start, position_ - start));
        return true;
    }

    static int HexDigit(char ch) {
        if (ch >= '0' && ch <= '9') return ch - '0';
        if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
        if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
        return -1;
    }

    bool ReadHex4(unsigned& code) {
        if (position_ + 4 > input_.size()) {
            return false;
        }
        code = 0;
        for (int i = 0; i < 4; ++i) {
            const int digit = HexDigit(input_[position_ + i]);
            if (digit < 0) {
                return false;
            }
            code = (code << 4) | static_cast<unsigned>(digit);
        }
        position_ += 4;
        return true;
    }

    bool ReadString(std::wstring& out) {
        ++position_;  // opening quote
        std::string utf8;
        for (;;) {
            if (position_ >= input_.size()) {
                return false;
            }
            const char ch = input_[position_++];
            if (ch == '"') {
                break;
            }
            if (static_cast<unsigned char>(ch) < 0x20) {
                return false;
            }
            if (ch != '\\') {
                utf8.push_back(ch);
                continue;
            }
            if (position_ >= input_.size()) {
                return false;
            }
            const char escape = input_[position_++];
            switch (escape) {
                case '"': utf8.push_back('"'); break;
                case '\\': utf8.push_back('\\'); break;
                case '/': utf8.push_back('/'); break;
                case 'b': utf8.push_back('\b'); break;
                case 'f': utf8.push_back('\f'); break;
                case 'n': utf8.push_back('\n'); break;
                case 'r': utf8.push_back('\r'); break;
                case 't': utf8.push_back('\t'); break;
                case 'u': {
                    unsigned code = 0;
                    if (!ReadHex4(code)) {
                        return false;
                    }
                    if (code >= 0xD800 && code <= 0xDBFF) {
                        unsigned low = 0;
                        if (!ReadLiteral("\\u") || !ReadHex4(low) || low < 0xDC00 || low > 0xDFFF) {
                            return false;
                        }
                        code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                    } else if (code >= 0xDC00 && code <= 0xDFFF) {
                        return false;
                    }
                    AppendUtf8(utf8, code);
                    break;
                }
                default:
                    return false;
            }
        }
        return Utf8ToWide(utf8, out);
    }

    static void AppendUtf8(std::string& out, unsigned code) {
        if (code < 0x80) {
            out.push_back(static_cast<char>(code));
        } else if (code < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (code >> 6)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        } else if (code < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (code >> 12)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (code >> 18)));
            out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        }
    }

    static bool Utf8ToWide(const std::string& utf8, std::wstring& out) {
        out.clear();
        if (utf8.empty()) {
            return true;
        }
        const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(),
                                               static_cast<int>(utf8.size()), nullptr, 0);
        if (length <= 0) {
            return false;
        }
        out.resize(static_cast<size_t>(length));
        return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(),
                                   static_cast<int>(utf8.size()), out.data(), length) == length;
    }

    std::string_view input_;
    size_t position_ = 0;
};

inline std::optional<JsonValue> ParseJson(std::string_view utf8) {
    return JsonReader(utf8).ReadDocument();
}

// A JSON number that is a plain non-negative integer, as a byte count or a
// schema number has to be.
inline std::optional<uint64_t> ParseJsonUnsigned(const JsonValue& value) {
    if (value.kind != JsonValue::Kind::Number || value.number.empty() || value.number[0] == '-') {
        return std::nullopt;
    }
    uint64_t result = 0;
    for (const char ch : value.number) {
        if (ch < '0' || ch > '9') {
            return std::nullopt;
        }
        const uint64_t digit = static_cast<uint64_t>(ch - '0');
        if (result > (UINT64_MAX - digit) / 10) {
            return std::nullopt;
        }
        result = result * 10 + digit;
    }
    return result;
}

struct ManifestEntry {
    std::wstring path;
    std::wstring sha256;  // lower-case hex
    uint64_t bytes = 0;
};

struct ArtifactManifest {
    std::wstring version;
    std::vector<ManifestEntry> files;
};

struct ManifestResult {
    std::optional<ArtifactManifest> manifest;
    std::wstring error;
};

// A path inside the package: relative, no "..", no empty segment.
inline bool IsSafeManifestPath(std::wstring_view path) {
    if (path.empty() || path.find(L':') != std::wstring_view::npos) {
        return false;
    }
    if (path.front() == L'\\' || path.front() == L'/') {
        return false;
    }
    size_t start = 0;
    for (;;) {
        const size_t end = path.find_first_of(L"\\/", start);
        const std::wstring_view part =
            path.substr(start, end == std::wstring_view::npos ? std::wstring_view::npos : end - start);
        if (part.empty() || part == L"..") {
            return false;
        }
        if (end == std::wstring_view::npos) {
            return true;
        }
        start = end + 1;
    }
}

inline bool IsSha256Hex(std::wstring_view text) {
    if (text.size() != 64) {
        return false;
    }
    for (const wchar_t ch : text) {
        const bool hex = (ch >= L'0' && ch <= L'9') || (ch >= L'a' && ch <= L'f') ||
                         (ch >= L'A' && ch <= L'F');
        if (!hex) {
            return false;
        }
    }
    return true;
}

// The same checks register.ps1's Assert-ArtifactManifest made before hashing a
// single file: the format, every path safe and listed once, every hash well
// formed, and every file the package needs vouched for.
inline ManifestResult ParseArtifactManifest(std::string_view utf8,
                                            const std::vector<std::wstring>& required_files) {
    ManifestResult result;
    const std::optional<JsonValue> document = ParseJson(utf8);
    if (!document || document->kind != JsonValue::Kind::Object) {
        result.error = L"The hash manifest is not valid JSON.";
        return result;
    }

    const JsonValue* schema = document->Member(L"schema");
    const JsonValue* algorithm = document->Member(L"algorithm");
    const std::optional<uint64_t> schema_number = schema ? ParseJsonUnsigned(*schema) : std::nullopt;
    if (!schema_number || *schema_number != 1 || !algorithm ||
        algorithm->kind != JsonValue::Kind::String || !EqualsIgnoreCase(algorithm->text, L"SHA256")) {
        result.error = L"Unsupported hash manifest format.";
        return result;
    }

    ArtifactManifest manifest;
    if (const JsonValue* version = document->Member(L"version");
        version && version->kind == JsonValue::Kind::String) {
        manifest.version = version->text;
    }

    const JsonValue* files = document->Member(L"files");
    if (!files || files->kind != JsonValue::Kind::Array) {
        result.error = L"The hash manifest has no file list.";
        return result;
    }
    for (const JsonValue& item : files->items) {
        const JsonValue* path = item.Member(L"path");
        const JsonValue* sha256 = item.Member(L"sha256");
        const JsonValue* bytes = item.Member(L"bytes");
        if (item.kind != JsonValue::Kind::Object || !path || path->kind != JsonValue::Kind::String) {
            result.error = L"A hash manifest entry has no path.";
            return result;
        }
        if (!IsSafeManifestPath(path->text)) {
            result.error = L"Unsafe path in hash manifest: " + path->text;
            return result;
        }
        for (const ManifestEntry& existing : manifest.files) {
            if (EqualsIgnoreCase(existing.path, path->text)) {
                result.error = L"Duplicate path in hash manifest: " + path->text;
                return result;
            }
        }
        if (!sha256 || sha256->kind != JsonValue::Kind::String || !IsSha256Hex(sha256->text)) {
            result.error = L"Invalid SHA256 in hash manifest for: " + path->text;
            return result;
        }
        const std::optional<uint64_t> size = bytes ? ParseJsonUnsigned(*bytes) : std::nullopt;
        if (!size) {
            result.error = L"Invalid byte size in hash manifest for: " + path->text;
            return result;
        }
        manifest.files.push_back({path->text, ToLowerAscii(sha256->text), *size});
    }

    for (const std::wstring& required : required_files) {
        bool listed = false;
        for (const ManifestEntry& entry : manifest.files) {
            if (EqualsIgnoreCase(entry.path, required)) {
                listed = true;
                break;
            }
        }
        if (!listed) {
            result.error = L"Hash manifest does not include required file: " + required;
            return result;
        }
    }

    result.manifest = std::move(manifest);
    return result;
}

// What checking a package against its manifest found, in a form each caller
// can put into words: the tray app in the user's language, the report in
// English for a support thread.
enum class PackageProblem {
    None,
    ManifestMissing,
    ManifestInvalid,
    FileMissing,
    FileUnreadable,
    SizeMismatch,
    HashMismatch,
};

struct PackageCheck {
    PackageProblem problem = PackageProblem::None;
    std::wstring detail;  // the file, or what was wrong with the manifest
    std::wstring version;
    uint64_t expected_bytes = 0;
    uint64_t actual_bytes = 0;
};

inline std::wstring DescribePackageProblem(const PackageCheck& check, bool vietnamese) {
    // The usual cause of a mismatch is not a damaged download: it is a new zip
    // extracted over a folder Neokey is running from. Windows will not replace
    // a DLL or program that is in use, the extractor skips it, and the folder
    // ends up holding two versions.
    const std::wstring mixed = vietnamese
        ? L"Thư mục đang chứa file của hai phiên bản Neokey. Việc này xảy ra khi giải nén bản mới đè lên thư mục Neokey đang chạy: Windows giữ lại các file đang được dùng. Hãy giải nén vào một thư mục mới, trống, rồi cài từ đó; hoặc gỡ cài đặt, khởi động lại Windows, rồi giải nén lại."
        : L"The folder holds files from two versions of Neokey. This happens when a new zip is extracted over a folder Neokey is still running from: Windows keeps the files that are in use. Extract the zip into a new, empty folder and install from there, or uninstall, restart Windows, and extract again.";
    const std::wstring missing = vietnamese
        ? L"Hãy giải nén lại toàn bộ file zip vào một thư mục mới, trống. Nếu file lại biến mất, một chương trình diệt virus đang xóa nó."
        : L"Extract the whole zip again into a new, empty folder. If the file disappears again, an antivirus program is removing it.";

    switch (check.problem) {
        case PackageProblem::None:
            return {};
        case PackageProblem::ManifestMissing:
            return (vietnamese ? L"Thiếu file kiểm tra gói: " : L"Hash manifest missing: ") +
                   check.detail + L". " + missing;
        case PackageProblem::ManifestInvalid:
            return (vietnamese ? L"File kiểm tra gói bị hỏng: " : L"The hash manifest cannot be used: ") +
                   check.detail;
        case PackageProblem::FileMissing:
            return (vietnamese ? L"Thiếu file của gói: " : L"Required release file missing: ") +
                   check.detail + L". " + missing;
        case PackageProblem::FileUnreadable:
            return (vietnamese ? L"Không đọc được file: " : L"Cannot read release file: ") +
                   check.detail + L". " + missing;
        case PackageProblem::SizeMismatch:
            return (vietnamese ? L"Kích thước không khớp: " : L"Size mismatch for ") + check.detail +
                   (vietnamese ? L" (cần " : L". Expected ") + std::to_wstring(check.expected_bytes) +
                   (vietnamese ? L" byte, có " : L", got ") + std::to_wstring(check.actual_bytes) +
                   (vietnamese ? L" byte). " : L". ") + mixed;
        case PackageProblem::HashMismatch:
            return (vietnamese ? L"Nội dung không khớp: " : L"SHA256 mismatch for ") + check.detail +
                   L". " + mixed;
    }
    return {};
}

// NeokeySetup.exe installed Neokey, and a portable copy must not register over
// it or unregister it: the installer's uninstall entry would stay behind and
// its uninstaller would then take the portable copy's registration with it.
inline std::wstring DescribeInstalledBySetup(bool installing, std::wstring_view location, bool vietnamese) {
    const std::wstring where = location.empty() ? std::wstring() : L" (" + std::wstring(location) + L")";
    if (installing) {
        return vietnamese
            ? L"Neokey trên máy này đã được cài bằng bộ cài NeokeySetup.exe" + where +
                  L". Để cập nhật, hãy chạy NeokeySetup.exe bản mới. Để chuyển sang bản portable, hãy gỡ bản đó trước trong Settings > Apps > Installed apps."
            : L"Neokey on this computer was installed with NeokeySetup.exe" + where +
                  L". To update it, run the new NeokeySetup.exe. To switch to the portable copy, first remove that one in Settings > Apps > Installed apps.";
    }
    return vietnamese
        ? L"Neokey trên máy này được cài bằng bộ cài NeokeySetup.exe" + where +
              L". Hãy gỡ nó trong Settings > Apps > Installed apps."
        : L"Neokey on this computer was installed with NeokeySetup.exe" + where +
              L". Remove it in Settings > Apps > Installed apps.";
}

inline std::wstring DescribeDeclinedElevation(bool installing, bool vietnamese) {
    if (installing) {
        return vietnamese
            ? L"Quyền Quản trị viên đã bị từ chối nên Neokey chưa được cài. Hãy mở lại neokey_config.exe và chọn Yes khi Windows hỏi."
            : L"The Administrator permission was declined, so Neokey was not installed. Open neokey_config.exe again and choose Yes when Windows asks.";
    }
    return vietnamese
        ? L"Quyền Quản trị viên đã bị từ chối nên Neokey chưa được gỡ. Hãy chọn lại Gỡ cài đặt và chọn Yes khi Windows hỏi."
        : L"The Administrator permission was declined, so Neokey was not removed. Choose Uninstall again and pick Yes when Windows asks.";
}

// ---------------------------------------------------------------------------
// Where the package is
// ---------------------------------------------------------------------------

// Every application that takes typing loads neokey.dll from the folder it was
// registered in, for as long as Neokey stays installed. A folder in one of
// these places installs without complaint and breaks afterwards - when the
// temporary folder is emptied, the drive is unplugged, or the network is not
// there at sign-in.
enum class LocationProblem { None, Network, Archive, Temp, Removable, OneDrive };

struct LocationVerdict {
    LocationProblem problem = LocationProblem::None;
    bool blocking = false;
};

inline bool PathIsUnder(std::wstring_view path, std::wstring_view root) {
    std::wstring trimmed_root = TrimWhitespace(root);
    while (!trimmed_root.empty() && (trimmed_root.back() == L'\\' || trimmed_root.back() == L'/')) {
        trimmed_root.pop_back();
    }
    if (trimmed_root.empty()) {
        return false;
    }
    trimmed_root.push_back(L'\\');
    return StartsWithIgnoreCase(path, trimmed_root);
}

// Explorer's "open the zip and double-click", WinRAR and 7-Zip each unpack
// what they run into a folder of their own under TEMP and delete it again.
// Checked by name as well as by place, since TEMP can be spelled in 8.3 form.
inline bool IsArchiveTempFolderName(std::wstring_view segment) {
    if (StartsWithIgnoreCase(segment, L"Temp")) {
        size_t index = 4;
        const size_t digits_start = index;
        while (index < segment.size() && segment[index] >= L'0' && segment[index] <= L'9') {
            ++index;
        }
        if (index > digits_start && index < segment.size() && segment[index] == L'_') {
            return true;
        }
    }
    if (StartsWithIgnoreCase(segment, L"Rar$EX") || StartsWithIgnoreCase(segment, L"Rar$DI")) {
        return true;
    }
    if (segment.size() >= 9 && FoldAscii(segment[0]) == L'7' && FoldAscii(segment[1]) == L'z' &&
        (FoldAscii(segment[2]) == L'o' || FoldAscii(segment[2]) == L'e')) {
        for (size_t index = 3; index < segment.size(); ++index) {
            const wchar_t ch = FoldAscii(segment[index]);
            if (!((ch >= L'0' && ch <= L'9') || (ch >= L'a' && ch <= L'f'))) {
                return false;
            }
        }
        return true;
    }
    return false;
}

// drive_type is what GetDriveTypeW reported for the folder's root, or
// DRIVE_UNKNOWN when it could not be asked.
inline LocationVerdict ClassifyInstallLocation(std::wstring_view directory,
                                               const std::vector<std::wstring>& temp_directories,
                                               const std::vector<std::wstring>& onedrive_directories,
                                               UINT drive_type) {
    if (directory.empty()) {
        return {};
    }
    std::wstring full(directory);
    while (!full.empty() && (full.back() == L'\\' || full.back() == L'/')) {
        full.pop_back();
    }
    full.push_back(L'\\');

    if (StartsWithIgnoreCase(full, L"\\\\") || drive_type == DRIVE_REMOTE) {
        return {LocationProblem::Network, true};
    }

    size_t start = 0;
    while (start < full.size()) {
        const size_t end = full.find(L'\\', start);
        const std::wstring_view segment = std::wstring_view(full).substr(start, end - start);
        if (IsArchiveTempFolderName(segment)) {
            return {LocationProblem::Archive, true};
        }
        start = end + 1;
    }

    for (const std::wstring& temp : temp_directories) {
        if (PathIsUnder(full, temp)) {
            return {LocationProblem::Temp, true};
        }
    }
    if (drive_type == DRIVE_REMOVABLE) {
        return {LocationProblem::Removable, false};
    }
    for (const std::wstring& onedrive : onedrive_directories) {
        if (PathIsUnder(full, onedrive)) {
            return {LocationProblem::OneDrive, false};
        }
    }
    return {};
}

inline std::wstring DescribeLocationProblem(LocationProblem problem, std::wstring_view directory,
                                            bool vietnamese) {
    const std::wstring folder(directory);
    switch (problem) {
        case LocationProblem::Network:
            return vietnamese
                ? L"Thư mục này nằm trên ổ mạng (" + folder + L"). Mọi ứng dụng bạn gõ sẽ phải nạp Neokey qua mạng, và bước Quản trị viên không thấy được các ổ mạng đã ánh xạ cho tài khoản của bạn. Hãy chép cả thư mục vào máy này, ví dụ C:\\Neokey, rồi cài từ đó."
                : L"This folder is on a network drive (" + folder + L"). Every app you type in would load Neokey over the network, and the Administrator step cannot see drives mapped for your account. Copy the whole folder to this computer, for example to C:\\Neokey, and install from there.";
        case LocationProblem::Archive:
            return vietnamese
                ? L"Neokey đang chạy ngay trong file zip (" + folder + L"). Windows giải nén nó vào một thư mục tạm và sẽ xóa thư mục đó sau, Neokey sẽ mất theo. Hãy nhấn chuột phải vào file zip, chọn Extract All (Giải nén tất cả), rồi cài từ thư mục vừa giải nén."
                : L"Neokey is running from inside the zip file (" + folder + L"). Windows unpacked it into a temporary folder that it deletes later, and Neokey would go with it. Right-click the zip, choose Extract All, and install from the extracted folder.";
        case LocationProblem::Temp:
            return vietnamese
                ? L"Thư mục này nằm trong thư mục tạm (" + folder + L"), nơi Windows và các công cụ dọn dẹp sẽ xóa. Hãy chuyển cả thư mục đến một chỗ cố định, ví dụ C:\\Neokey, rồi cài từ đó."
                : L"This folder is inside the temporary folder (" + folder + L"), which Windows and cleanup tools empty. Move the whole folder somewhere it can stay, for example to C:\\Neokey, and install from there.";
        case LocationProblem::Removable:
            return vietnamese
                ? L"Thư mục này nằm trên ổ di động (" + folder + L"). Neokey sẽ ngừng hoạt động trong mọi ứng dụng khi rút ổ. Đặt thư mục trên máy, ví dụ C:\\Neokey, sẽ tránh được điều này."
                : L"This folder is on a removable drive (" + folder + L"). Neokey stops working in every app while that drive is unplugged. A folder on this computer, for example C:\\Neokey, avoids that.";
        case LocationProblem::OneDrive:
            return vietnamese
                ? L"Thư mục này nằm trong OneDrive (" + folder + L"). Các file chương trình của Neokey được đặt chế độ \"Always keep on this device\" (luôn giữ trên thiết bị này) để OneDrive không biến chúng thành bản chỉ có trên mạng. Đặt thư mục ngoài OneDrive, ví dụ C:\\Neokey, sẽ tránh được việc này."
                : L"This folder is inside OneDrive (" + folder + L"). Neokey's program files are set to 'Always keep on this device' so OneDrive cannot turn them into online-only copies. A folder outside OneDrive, for example C:\\Neokey, avoids the question.";
        case LocationProblem::None:
            break;
    }
    return {};
}

// ---------------------------------------------------------------------------
// What Neokey leaves on a machine
// ---------------------------------------------------------------------------

inline std::wstring JoinPath(std::wstring_view directory, std::wstring_view name) {
    std::wstring joined(directory);
    while (!joined.empty() && (joined.back() == L'\\' || joined.back() == L'/')) {
        joined.pop_back();
    }
    joined.push_back(L'\\');
    joined.append(name);
    return joined;
}

enum class ResidueKind { RegistryKey, RegistryValue, Directory, File };

struct ResidueTarget {
    ResidueKind kind = ResidueKind::File;
    // Registry targets: a key under HKEY_CURRENT_USER. Others: a full path.
    std::wstring path;
    std::wstring value_name;
    std::wstring label;
    // The one thing here the user typed themselves; --keep-user-data spares it.
    bool user_data = false;

    std::wstring DisplayName() const {
        if (kind == ResidueKind::RegistryKey) {
            return L"HKCU:\\" + path;
        }
        if (kind == ResidueKind::RegistryValue) {
            return L"HKCU:\\" + path + L"\\" + value_name;
        }
        return path;
    }
};

// Everything Neokey leaves outside its own folder. One list, read by the
// uninstall and by --status, so a status run after a failed uninstall names
// what it left behind.
inline std::vector<ResidueTarget> BuildResidueTargets(std::wstring_view temp_directory,
                                                      std::wstring_view local_app_data,
                                                      std::wstring_view system_root,
                                                      std::wstring_view package_directory) {
    std::vector<ResidueTarget> targets;
    targets.push_back({ResidueKind::RegistryKey, L"Software\\Neokey", L"", L"settings", false});
    targets.push_back({ResidueKind::RegistryValue, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                       L"Neokey", L"start with Windows", false});
    if (!TrimWhitespace(local_app_data).empty()) {
        targets.push_back({ResidueKind::Directory, JoinPath(local_app_data, L"Neokey"), L"",
                           L"shorthand data", true});
    }
    // The log goes to whatever GetTempPath() returns in each process that loads
    // the DLL, so more than one can exist on the same machine.
    std::vector<std::wstring> log_directories = {std::wstring(temp_directory), L"C:\\Temp"};
    if (!TrimWhitespace(system_root).empty()) {
        log_directories.push_back(JoinPath(system_root, L"Temp"));
    }
    for (const std::wstring& directory : log_directories) {
        if (TrimWhitespace(directory).empty()) {
            continue;
        }
        targets.push_back({ResidueKind::File, JoinPath(directory, L"neokey.log"), L"", L"log", false});
    }
    if (!TrimWhitespace(package_directory).empty()) {
        targets.push_back({ResidueKind::File, JoinPath(package_directory, L"register_elevated.log"), L"",
                           L"registration log", false});
    }
    return targets;
}

// ---------------------------------------------------------------------------
// Which input method a session starts on
// ---------------------------------------------------------------------------

// Windows asks the override first, and without one takes the first language's
// first input method. "vi" and "vi-VN" are the same language to it.
inline bool InputListResolvesToNeokey(std::wstring_view override_tip,
                                      const std::vector<std::wstring>& languages,
                                      std::wstring_view neokey_tip,
                                      std::wstring_view neokey_language) {
    if (!TrimWhitespace(override_tip).empty()) {
        return EqualsIgnoreCase(override_tip, neokey_tip);
    }
    if (languages.empty()) {
        return false;
    }
    const std::wstring& first = languages.front();
    return EqualsIgnoreCase(first, neokey_language) ||
           StartsWithIgnoreCase(first, std::wstring(neokey_language) + L"-");
}

// ---------------------------------------------------------------------------
// The user's language list
// ---------------------------------------------------------------------------

// One entry of what Get-WinUserLanguageList returned: a BCP-47 tag and its
// input methods, in order.
struct LanguageEntry {
    std::wstring tag;
    std::vector<std::wstring> tips;
};

// register.ps1 matched these as -like "vi*" and "en*".
inline bool IsVietnameseTag(std::wstring_view tag) {
    return StartsWithIgnoreCase(tag, L"vi");
}

inline bool IsEnglishTag(std::wstring_view tag) {
    return StartsWithIgnoreCase(tag, L"en");
}

inline bool ContainsTip(const std::vector<std::wstring>& tips, std::wstring_view tip) {
    for (const std::wstring& candidate : tips) {
        if (EqualsIgnoreCase(candidate, tip)) {
            return true;
        }
    }
    return false;
}

inline size_t FindLanguage(const std::vector<LanguageEntry>& languages, bool (*matches)(std::wstring_view)) {
    for (size_t index = 0; index < languages.size(); ++index) {
        if (matches(languages[index].tag)) {
            return index;
        }
    }
    return languages.size();
}

struct AddNeokeyPlan {
    std::vector<LanguageEntry> languages;
    bool changed = false;
    bool added_vietnamese = false;
    // Keyboards that were under Vietnamese before this run and are not now.
    std::vector<std::wstring> replaced;
    std::vector<std::wstring> notes;
};

// What register.ps1's Add-NeokeyToUserLanguageList did to the list: Neokey
// alone under Vietnamese, and the English copy beside the US keyboard (or
// taken off when it is not wanted). `english_defaults` is what Windows gives a
// new en-US entry.
inline AddNeokeyPlan PlanAddNeokey(std::vector<LanguageEntry> languages, bool english_profile,
                                   const std::vector<std::wstring>& english_defaults) {
    AddNeokeyPlan plan;
    size_t vi = FindLanguage(languages, IsVietnameseTag);
    if (vi == languages.size()) {
        // Created empty. Windows would fill a new entry with its own keyboard
        // for the language, which was never on this machine and must not be
        // recorded as the user's.
        languages.push_back({L"vi-VN", {}});
        vi = languages.size() - 1;
        plan.added_vietnamese = true;
        plan.changed = true;
        plan.notes.push_back(L"Vietnamese language not found in user settings. Adding vi-VN...");
    }

    std::vector<std::wstring> kept;
    for (const std::wstring& tip : languages[vi].tips) {
        if (EqualsIgnoreCase(tip, kVietnameseTip)) {
            kept.push_back(tip);
        } else {
            plan.replaced.push_back(tip);
            plan.changed = true;
            plan.notes.push_back(L"Removed redundant built-in Vietnamese keyboard: " + tip);
        }
    }
    languages[vi].tips = std::move(kept);
    if (!ContainsTip(languages[vi].tips, kVietnameseTip)) {
        languages[vi].tips.push_back(kVietnameseTip);
        plan.changed = true;
        plan.notes.push_back(L"Added Neokey to the user language list.");
    } else {
        plan.notes.push_back(L"Neokey is already in the user language list.");
    }

    // The English copy shares its language with the US keyboard, and that
    // keyboard stays: it is the only way back if the service fails to load.
    size_t en = FindLanguage(languages, IsEnglishTag);
    if (english_profile) {
        if (en == languages.size()) {
            languages.push_back({L"en-US", english_defaults});
            en = languages.size() - 1;
            plan.changed = true;
            plan.notes.push_back(L"English not found in user settings. Adding en-US...");
        }
        if (!ContainsTip(languages[en].tips, kEnglishTip)) {
            languages[en].tips.push_back(kEnglishTip);
            plan.changed = true;
            plan.notes.push_back(L"Added the English copy of Neokey to the user language list.");
        }
    } else if (en != languages.size() && ContainsTip(languages[en].tips, kEnglishTip)) {
        auto& tips = languages[en].tips;
        tips.erase(std::remove_if(tips.begin(), tips.end(),
                                  [](const std::wstring& tip) { return EqualsIgnoreCase(tip, kEnglishTip); }),
                   tips.end());
        plan.changed = true;
        plan.notes.push_back(L"Removed the English copy of Neokey from the user language list.");
    }

    plan.languages = std::move(languages);
    return plan;
}

// The first run that touches Vietnamese is the only one that can see what was
// there. A repair or upgrade run changes nothing, and recording "there was
// nothing here" for it would tell the uninstaller the machine arrived that way.
inline bool ShouldRecordPreNeokeyState(bool added_language, const std::vector<std::wstring>& replaced,
                                       bool already_recorded) {
    if (already_recorded) {
        return false;
    }
    return added_language || !replaced.empty();
}

// A substitute that already reads what Neokey is about to write cannot be
// attributed to anyone - other Vietnamese IMEs write the same - so nothing is
// recorded and the uninstall leaves it alone.
inline bool ShouldRecordLayoutSubstitute(const std::optional<std::wstring>& already_recorded,
                                         std::wstring_view existing, std::wstring_view about_to_write) {
    if (already_recorded) {
        return false;
    }
    return !EqualsIgnoreCase(existing, about_to_write);
}

// The ';'-joined record, with blank parts dropped. Absent stays absent: it
// means an older build recorded nothing, which is not the same as a record
// saying there was nothing.
inline std::optional<std::vector<std::wstring>> ParseRecordedTips(const std::optional<std::wstring>& raw) {
    if (!raw) {
        return std::nullopt;
    }
    std::vector<std::wstring> tips;
    size_t start = 0;
    for (;;) {
        const size_t end = raw->find(L';', start);
        const std::wstring part =
            TrimWhitespace(std::wstring_view(*raw).substr(start, end == std::wstring::npos ? std::wstring::npos : end - start));
        if (!part.empty()) {
            tips.push_back(part);
        }
        if (end == std::wstring::npos) {
            break;
        }
        start = end + 1;
    }
    return tips;
}

inline std::wstring JoinTips(const std::vector<std::wstring>& tips) {
    std::wstring joined;
    for (const std::wstring& tip : tips) {
        if (!joined.empty()) {
            joined.push_back(L';');
        }
        joined += tip;
    }
    return joined;
}

inline constexpr wchar_t kStockVietnameseKeyboard[] = L"042A:0000042a";
inline constexpr wchar_t kStockUsKeyboard[] = L"0409:00000409";

enum class CleanupAction { RestoreRecorded, Leave, InstallStockKeyboard, RemoveLanguage };

struct CleanupDecision {
    CleanupAction action = CleanupAction::Leave;
    std::vector<std::wstring> tips;
};

// What Vietnamese becomes once Neokey is taken out of it.
inline CleanupDecision DecideVietnameseCleanup(const std::vector<std::wstring>& remaining,
                                               const std::optional<std::vector<std::wstring>>& recorded,
                                               bool added_language, bool only_language,
                                               bool display_language) {
    // A language Neokey added had nothing in it before Neokey, whatever the
    // record says: builds up to 0.1.18 recorded the keyboard Windows puts into
    // a new entry as if it had been the user's.
    if (added_language) {
        if (!remaining.empty()) {
            return {CleanupAction::Leave, {}};
        }
        if (only_language || display_language) {
            return {CleanupAction::InstallStockKeyboard, {}};
        }
        return {CleanupAction::RemoveLanguage, {}};
    }
    if (recorded && !recorded->empty()) {
        return {CleanupAction::RestoreRecorded, *recorded};
    }
    if (!remaining.empty()) {
        // Another IME, or a keyboard added after Neokey: not ours to remove.
        return {CleanupAction::Leave, {}};
    }
    // Vietnamese would be left with no input method at all, which is not a
    // state to hand back to Windows.
    if (only_language || display_language) {
        return {CleanupAction::InstallStockKeyboard, {}};
    }
    if (!recorded) {
        // Installed by a build that recorded nothing. Removing the language is
        // the recoverable mistake; adding Microsoft's Vietnamese keyboard to a
        // machine that never had it is the one users report.
        return {CleanupAction::RemoveLanguage, {}};
    }
    return {CleanupAction::InstallStockKeyboard, {}};
}

struct RemoveNeokeyPlan {
    std::vector<LanguageEntry> languages;
    bool changed = false;
    bool removed_vietnamese_tip = false;
    CleanupAction action = CleanupAction::Leave;
    std::vector<std::wstring> notes;
};

// What register.ps1's Remove-NeokeyFromUserLanguageList did to the list.
inline RemoveNeokeyPlan PlanRemoveNeokey(std::vector<LanguageEntry> languages,
                                         const std::optional<std::vector<std::wstring>>& recorded,
                                         bool added_language, bool display_language) {
    RemoveNeokeyPlan plan;
    const bool only_language = languages.size() <= 1;

    // Each copy is looked for on its own: a machine may carry one and not the
    // other.
    const size_t vi = FindLanguage(languages, IsVietnameseTag);
    if (vi != languages.size()) {
        auto& tips = languages[vi].tips;
        const size_t before = tips.size();
        tips.erase(std::remove_if(tips.begin(), tips.end(),
                                  [](const std::wstring& tip) { return EqualsIgnoreCase(tip, kVietnameseTip); }),
                   tips.end());
        if (tips.size() != before) {
            plan.removed_vietnamese_tip = true;
            plan.changed = true;
            const CleanupDecision decision =
                DecideVietnameseCleanup(tips, recorded, added_language, only_language, display_language);
            plan.action = decision.action;
            switch (decision.action) {
                case CleanupAction::RestoreRecorded:
                    for (const std::wstring& tip : decision.tips) {
                        if (!ContainsTip(tips, tip)) {
                            tips.push_back(tip);
                        }
                    }
                    plan.notes.push_back(L"Restored the Vietnamese keyboards that were there before Neokey: " +
                                         JoinTips(decision.tips) + L".");
                    break;
                case CleanupAction::RemoveLanguage:
                    languages.erase(std::remove_if(languages.begin(), languages.end(),
                                                   [](const LanguageEntry& entry) { return IsVietnameseTag(entry.tag); }),
                                    languages.end());
                    plan.notes.push_back(L"Removed the Vietnamese language entry, which Neokey was the only thing using.");
                    break;
                case CleanupAction::InstallStockKeyboard:
                    tips.push_back(kStockVietnameseKeyboard);
                    plan.notes.push_back(L"Vietnamese would have been left with no keyboard, so the built-in one takes Neokey's place.");
                    break;
                case CleanupAction::Leave:
                    break;
            }
        }
    }

    const size_t en = FindLanguage(languages, IsEnglishTag);
    if (en != languages.size()) {
        auto& tips = languages[en].tips;
        const size_t before = tips.size();
        tips.erase(std::remove_if(tips.begin(), tips.end(),
                                  [](const std::wstring& tip) { return EqualsIgnoreCase(tip, kEnglishTip); }),
                   tips.end());
        if (tips.size() != before) {
            plan.changed = true;
        }
        if (tips.empty()) {
            tips.push_back(kStockUsKeyboard);
            plan.changed = true;
        }
    }

    plan.languages = std::move(languages);
    return plan;
}

// Vietnamese first, then everything else in the order it was, without
// duplicates - the order both Preload and CTF's SortOrder get.
inline std::vector<std::wstring> OrderVietnameseFirst(const std::vector<std::wstring>& current) {
    static constexpr wchar_t kVietnameseLayout[] = L"0000042a";
    std::vector<std::wstring> ordered = {kVietnameseLayout};
    for (const std::wstring& entry : current) {
        if (!EqualsIgnoreCase(entry, kVietnameseLayout) && !ContainsTip(ordered, entry)) {
            ordered.push_back(entry);
        }
    }
    return ordered;
}

// ---------------------------------------------------------------------------
// Command lines
// ---------------------------------------------------------------------------

// One argument quoted so CommandLineToArgvW reads it back unchanged: quotes
// escaped, and backslashes doubled only where they precede a quote.
inline std::wstring QuoteArgument(std::wstring_view argument) {
    if (!argument.empty() && argument.find_first_of(L" \t\n\v\"") == std::wstring_view::npos) {
        return std::wstring(argument);
    }
    std::wstring quoted = L"\"";
    size_t backslashes = 0;
    for (const wchar_t ch : argument) {
        if (ch == L'\\') {
            ++backslashes;
            continue;
        }
        if (ch == L'"') {
            quoted.append(backslashes * 2 + 1, L'\\');
        } else {
            quoted.append(backslashes, L'\\');
        }
        backslashes = 0;
        quoted.push_back(ch);
    }
    quoted.append(backslashes * 2, L'\\');
    quoted.push_back(L'"');
    return quoted;
}

}  // namespace vn_ime::setup
