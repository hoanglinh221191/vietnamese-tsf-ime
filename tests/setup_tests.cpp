// Tests for neokey_config.exe's setup commands: the decisions in
// setup_logic.hpp, and the parts of setup_commands.cpp that only read.
// Nothing here registers, unregisters or changes the user's input settings.

#include <windows.h>
#include <shellapi.h>

#include <iostream>
#include <string>
#include <vector>

#include "setup_commands.hpp"
#include "setup_logic.hpp"

using namespace vn_ime::setup;

namespace {

int g_passed = 0;
int g_failed = 0;

std::string Narrow(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }
    const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0,
                                           nullptr, nullptr);
    std::string utf8(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), utf8.data(), length, nullptr,
                        nullptr);
    return utf8;
}

void Check(bool condition, const std::string& name) {
    if (condition) {
        ++g_passed;
        std::cout << "  [PASS] " << name << "\n";
    } else {
        ++g_failed;
        std::cout << "  [FAIL] " << name << "\n";
    }
}

SetupOptions Parse(std::initializer_list<const wchar_t*> arguments) {
    std::vector<std::wstring> list;
    for (const wchar_t* argument : arguments) {
        list.emplace_back(argument);
    }
    return ParseSetupArguments(list);
}

void TestArguments() {
    std::cout << "\nsetup arguments\n";
    Check(Parse({}).action == SetupAction::None && Parse({}).error.empty(), "no arguments start the tray");
    Check(Parse({L"-silent"}).action == SetupAction::None && Parse({L"-silent"}).error.empty(),
          "-silent still starts the tray");
    Check(Parse({L"--status"}).action == SetupAction::Status, "--status");
    Check(Parse({L"--VERIFY"}).action == SetupAction::VerifyPackage, "switches ignore case");
    Check(Parse({L"--install", L"--no-english"}).action == SetupAction::Install &&
              !Parse({L"--install", L"--no-english"}).english_profile,
          "--install --no-english");
    Check(Parse({L"--uninstall", L"--keep-user-data", L"--quiet"}).keep_user_data &&
              Parse({L"--uninstall", L"--keep-user-data", L"--quiet"}).quiet,
          "--uninstall --keep-user-data --quiet");

    const SetupOptions logged = Parse({L"--register-elevated", L"--log", L"C:\\Neokey [1]\\register.log"});
    Check(logged.action == SetupAction::RegisterElevated && logged.log_path == L"C:\\Neokey [1]\\register.log",
          "--log takes the next argument as a path");

    Check(!Parse({L"--install", L"--uninstall"}).error.empty(), "two commands are refused");
    Check(!Parse({L"--install", L"-silent"}).error.empty(), "a setup command with -silent is refused");
    Check(!Parse({L"--instal"}).error.empty(), "a misspelled switch is refused rather than starting the tray");
    Check(!Parse({L"--keep-user-data"}).error.empty(), "an option without a command is refused");
    Check(!Parse({L"--status", L"--keep-user-data"}).error.empty(),
          "an option the command does not use is refused");
    Check(!Parse({L"--install", L"--keep-user-data"}).error.empty(), "--keep-user-data is for removing");
    Check(!Parse({L"--uninstall", L"--no-english"}).error.empty(), "--no-english is for registering");
    Check(!Parse({L"--install", L"--log"}).error.empty(), "--log without a path is refused");
    Check(!Parse({L"--install", L"--log", L"--quiet"}).error.empty(), "--log does not swallow a switch");
    Check(!Parse({L"--install", L"extra"}).error.empty(), "a stray argument after a command is refused");
    Check(Parse({L"whatever"}).action == SetupAction::None && Parse({L"whatever"}).error.empty(),
          "a stray argument alone is ignored, as the tray always did");
}

std::string Manifest(const std::string& files, const std::string& head = "\"schema\": 1, \"algorithm\": \"SHA256\"") {
    return "{" + head + ", \"version\": \"0.1.19\", \"files\": [" + files + "]}";
}

std::string Entry(const std::string& path, const std::string& bytes = "10",
                  const std::string& hash = std::string(64, 'a')) {
    return "{\"path\": \"" + path + "\", \"sha256\": \"" + hash + "\", \"bytes\": " + bytes + "}";
}

void TestManifest() {
    std::cout << "\nrelease manifest\n";
    const std::vector<std::wstring> none;

    ManifestResult ok = ParseArtifactManifest(
        "\xEF\xBB\xBF" + Manifest(Entry("neokey.dll", "1442304", std::string(64, 'A')) + "," +
                                  Entry("docs/README.md")),
        {L"neokey.dll"});
    Check(ok.manifest.has_value(), "a manifest written by Windows PowerShell (with its BOM) parses");
    if (ok.manifest) {
        Check(ok.manifest->version == L"0.1.19", "the version is read");
        Check(ok.manifest->files.size() == 2 && ok.manifest->files[0].bytes == 1442304,
              "byte counts are read exactly");
        Check(ok.manifest->files[0].sha256 == std::wstring(64, L'a'), "hashes are compared in lower case");
    }

    Check(!ParseArtifactManifest(Manifest(Entry("neokey.dll")), {L"neokey.dll", L"install.bat"}).manifest,
          "a required file missing from the manifest is refused");
    Check(ParseArtifactManifest(Manifest(Entry("neokey.dll")), {L"NEOKEY.DLL"}).manifest.has_value(),
          "required files are matched regardless of case");
    Check(!ParseArtifactManifest(Manifest(Entry("a.dll") + "," + Entry("A.DLL")), none).manifest,
          "a path listed twice is refused");
    Check(!ParseArtifactManifest(Manifest(Entry("..\\\\x.dll")), none).manifest, "a path climbing out is refused");
    Check(!ParseArtifactManifest(Manifest(Entry("C:\\\\x.dll")), none).manifest, "a rooted path is refused");
    Check(!ParseArtifactManifest(Manifest(Entry("a//b.dll")), none).manifest, "an empty path segment is refused");
    Check(!ParseArtifactManifest(Manifest(Entry("a.dll", "10", std::string(63, 'a'))), none).manifest,
          "a short hash is refused");
    Check(!ParseArtifactManifest(Manifest(Entry("a.dll", "10", std::string(63, 'a') + "g")), none).manifest,
          "a hash that is not hex is refused");
    Check(!ParseArtifactManifest(Manifest(Entry("a.dll", "-1")), none).manifest, "a negative size is refused");
    Check(!ParseArtifactManifest(Manifest(Entry("a.dll", "1.5")), none).manifest, "a fractional size is refused");
    Check(!ParseArtifactManifest(Manifest(Entry("a.dll"), "\"schema\": 2, \"algorithm\": \"SHA256\""), none).manifest,
          "another schema is refused");
    Check(!ParseArtifactManifest(Manifest(Entry("a.dll"), "\"schema\": 1, \"algorithm\": \"MD5\""), none).manifest,
          "another algorithm is refused");
    Check(!ParseArtifactManifest("{\"schema\": 1, \"algorithm\": \"SHA256\", \"files\": [", none).manifest,
          "truncated JSON is refused");
    Check(!ParseArtifactManifest(Manifest(Entry("a.dll")) + "x", none).manifest, "trailing text is refused");

    const ManifestResult escaped =
        ParseArtifactManifest(Manifest(Entry("t\\u00e0i li\\u1ec7u\\/\\\"x\\\".txt")), none);
    Check(escaped.manifest && escaped.manifest->files[0].path == L"t\u00e0i li\u1ec7u/\"x\".txt",
          "string escapes, including \\u, are decoded");
    Check(!ParseArtifactManifest(Manifest(Entry("\\ud800.txt")), none).manifest, "a lone surrogate is refused");

    std::string deep(40, '[');
    deep += std::string(40, ']');
    Check(!ParseJson(deep).has_value(), "nesting deeper than any manifest is refused");
}

void TestPackageProblemText() {
    std::cout << "\npackage problem text\n";
    PackageCheck size;
    size.problem = PackageProblem::SizeMismatch;
    size.detail = L"neokey32.dll";
    size.expected_bytes = 10;
    size.actual_bytes = 11;
    const std::wstring english = DescribePackageProblem(size, false);
    Check(english.find(L"Size mismatch for neokey32.dll. Expected 10, got 11.") == 0,
          "a size mismatch says which file and both sizes");
    Check(english.find(L"two versions") != std::wstring::npos,
          "a mismatch explains the usual cause, extracting over a running copy");
    Check(DescribePackageProblem(size, true).find(L"hai phiên bản") != std::wstring::npos,
          "the Vietnamese text explains it too");
    PackageCheck none;
    Check(DescribePackageProblem(none, false).empty(), "a good package has nothing to say");

    const std::wstring location = L"C:\\Program Files\\Neokey\\";
    Check(DescribeInstalledBySetup(true, location, true).find(location) != std::wstring::npos &&
              DescribeInstalledBySetup(true, location, true).find(L"NeokeySetup.exe bản mới") != std::wstring::npos,
          "an install over the installer's copy points to the new NeokeySetup.exe, and names where it is");
    Check(DescribeInstalledBySetup(false, L"", false).find(L"Settings > Apps") != std::wstring::npos &&
              DescribeInstalledBySetup(false, L"", false).find(L"()") == std::wstring::npos,
          "an uninstall of the installer's copy points to Settings, without an empty location");
    Check(DescribeDeclinedElevation(true, true) != DescribeDeclinedElevation(false, true) &&
              DescribeDeclinedElevation(true, false).find(L"not installed") != std::wstring::npos,
          "a declined prompt says which job did not happen");
}

void TestLocations() {
    std::cout << "\ninstall location\n";
    const std::wstring temp = L"C:\\Users\\An\\AppData\\Local\\Temp";
    const std::wstring onedrive = L"C:\\Users\\An\\OneDrive";
    struct Case {
        const wchar_t* directory;
        UINT drive_type;
        LocationProblem expected;
        bool blocking;
    };
    const Case cases[] = {
        {L"\\\\nas\\share\\Neokey", DRIVE_UNKNOWN, LocationProblem::Network, true},
        {L"Z:\\Neokey", DRIVE_REMOTE, LocationProblem::Network, true},
        {L"C:\\Users\\An\\AppData\\Local\\Temp\\Temp1_Neokey-0.1.18-portable.zip\\Neokey", DRIVE_FIXED,
         LocationProblem::Archive, true},
        {L"C:\\Users\\ANDREW~1\\AppData\\Local\\Temp\\TEMP1_~1.ZIP", DRIVE_FIXED, LocationProblem::Archive, true},
        {L"C:\\Users\\An\\AppData\\Local\\Temp\\Rar$EXa1234.5678\\Neokey", DRIVE_FIXED, LocationProblem::Archive,
         true},
        {L"C:\\Users\\An\\AppData\\Local\\Temp\\7zO1A2B3C4D\\Neokey", DRIVE_FIXED, LocationProblem::Archive, true},
        {L"C:\\Users\\An\\AppData\\Local\\Temp\\Neokey", DRIVE_FIXED, LocationProblem::Temp, true},
        {L"E:\\Neokey", DRIVE_REMOVABLE, LocationProblem::Removable, false},
        {L"C:\\Users\\An\\OneDrive\\Desktop\\Neokey", DRIVE_FIXED, LocationProblem::OneDrive, false},
        {L"C:\\Users\\An\\AppData\\Local\\Temporary Neokey", DRIVE_FIXED, LocationProblem::None, false},
        {L"C:\\Users\\An\\OneDriveBackup\\Neokey", DRIVE_FIXED, LocationProblem::None, false},
        {L"D:\\Tools\\7zip\\Neokey", DRIVE_FIXED, LocationProblem::None, false},
        {L"C:\\Neokey [0.1.18]", DRIVE_FIXED, LocationProblem::None, false},
        {L"C:\\Users\\An\\Downloads\\Neokey-0.1.18-portable (1)", DRIVE_FIXED, LocationProblem::None, false},
    };
    for (const Case& test : cases) {
        const LocationVerdict verdict =
            ClassifyInstallLocation(test.directory, {temp, L"C:\\Windows\\Temp", L""}, {onedrive, L""},
                                    test.drive_type);
        Check(verdict.problem == test.expected && verdict.blocking == test.blocking,
              "location " + Narrow(test.directory));
        if (test.expected != LocationProblem::None) {
            Check(DescribeLocationProblem(test.expected, test.directory, false).find(test.directory) !=
                          std::wstring::npos &&
                      DescribeLocationProblem(test.expected, test.directory, true).find(test.directory) !=
                          std::wstring::npos,
                  "the message names the folder, in both languages");
        }
    }
}

void TestResolution() {
    std::cout << "\nwhich input method a session starts on\n";
    Check(InputListResolvesToNeokey(kVietnameseTip, {L"en-US"}, kVietnameseTip, L"vi"),
          "the override decides when there is one");
    Check(!InputListResolvesToNeokey(L"0409:00000409", {L"vi"}, kVietnameseTip, L"vi"),
          "an override elsewhere wins over the language order");
    Check(InputListResolvesToNeokey(L"", {L"vi-VN", L"en-US"}, kVietnameseTip, L"vi"),
          "without one, Vietnamese first starts on Neokey");
    Check(!InputListResolvesToNeokey(L"", {L"en-US", L"vi"}, kVietnameseTip, L"vi"),
          "and another language first does not");
    Check(!InputListResolvesToNeokey(L"", {L"vit"}, kVietnameseTip, L"vi"), "vi is matched as a whole tag");
    Check(!InputListResolvesToNeokey(L"", {}, kVietnameseTip, L"vi"), "an empty list starts on nothing of ours");
    Check(std::wstring(kVietnameseTip) == std::wstring(L"042A:") + kClsid + kProfileGuid &&
              std::wstring(kEnglishTip) == std::wstring(L"0409:") + kClsid + kProfileGuid,
          "the TIP strings are built from the class and profile");
}

void TestResidue() {
    std::cout << "\nwhat Neokey leaves on a machine\n";
    const std::vector<ResidueTarget> targets =
        BuildResidueTargets(L"C:\\Users\\An\\AppData\\Local\\Temp\\", L"C:\\Users\\An\\AppData\\Local",
                            L"C:\\Windows", L"D:\\Neokey");
    std::vector<std::wstring> names;
    for (const ResidueTarget& target : targets) {
        names.push_back(target.DisplayName());
    }
    const std::vector<std::wstring> expected = {
        L"HKCU:\\Software\\Neokey",
        L"HKCU:\\Software\\Microsoft\\Windows\\CurrentVersion\\Run\\Neokey",
        L"C:\\Users\\An\\AppData\\Local\\Neokey",
        L"C:\\Users\\An\\AppData\\Local\\Temp\\neokey.log",
        L"C:\\Temp\\neokey.log",
        L"C:\\Windows\\Temp\\neokey.log",
        L"D:\\Neokey\\register_elevated.log",
    };
    Check(names == expected, "the footprint list matches register.ps1's");
    int user_data = 0;
    for (const ResidueTarget& target : targets) {
        user_data += target.user_data ? 1 : 0;
    }
    Check(user_data == 1 && targets[2].user_data, "only the shorthand folder is the user's own data");
    Check(BuildResidueTargets(L"", L"", L"", L"").size() == 3,
          "missing environment values leave their entries out");
}

std::wstring Describe(const std::vector<LanguageEntry>& languages) {
    std::wstring text;
    for (const LanguageEntry& language : languages) {
        text += language.tag + L"[" + JoinTips(language.tips) + L"] ";
    }
    return text;
}

void TestAddPlan() {
    std::cout << "\nadding Neokey to the language list\n";
    const std::wstring ms_telex = L"042A:{C2CB2CF0-AF47-413E-9780-8BC3A3C16068}{5FB02EC5-0A77-4684-B4FA-DEF8A2195628}";

    // A machine without Vietnamese: the entry is created empty, so nothing of
    // Windows' own is recorded as the user's.
    AddNeokeyPlan fresh = PlanAddNeokey({{L"en-US", {kStockUsKeyboard}}}, true, {kStockUsKeyboard});
    Check(fresh.added_vietnamese && fresh.replaced.empty() && fresh.changed, "a new Vietnamese entry records nothing replaced");
    Check(Describe(fresh.languages) == L"en-US[0409:00000409;" + std::wstring(kEnglishTip) + L"] vi-VN[" +
                                           kVietnameseTip + L"] ",
          "Neokey goes under the new entry, the English copy beside the US keyboard");

    // A machine with Microsoft's keyboard under Vietnamese: it is replaced and
    // recorded.
    AddNeokeyPlan existing = PlanAddNeokey({{L"vi", {ms_telex, kStockVietnameseKeyboard}}, {L"en-US", {kStockUsKeyboard}}},
                                           true, {kStockUsKeyboard});
    Check(!existing.added_vietnamese && existing.replaced.size() == 2 && existing.replaced[0] == ms_telex,
          "keyboards already under Vietnamese are recorded as replaced, in order");
    Check(existing.languages[0].tips.size() == 1 && existing.languages[0].tips[0] == kVietnameseTip,
          "only Neokey is left under Vietnamese");

    // A repair run: nothing to do.
    AddNeokeyPlan repair = PlanAddNeokey(existing.languages, true, {kStockUsKeyboard});
    Check(!repair.changed && repair.replaced.empty(), "a repair run changes nothing");

    // Without the English copy it is taken off, and the US keyboard stays.
    AddNeokeyPlan no_english = PlanAddNeokey(existing.languages, false, {kStockUsKeyboard});
    Check(no_english.changed && no_english.languages[1].tips.size() == 1 &&
              no_english.languages[1].tips[0] == kStockUsKeyboard,
          "--no-english takes the English copy off and keeps the US keyboard");

    // No English at all: en-US is added with what Windows gives it.
    AddNeokeyPlan no_en = PlanAddNeokey({{L"vi-VN", {kVietnameseTip}}}, true, {kStockUsKeyboard});
    Check(no_en.languages.size() == 2 && no_en.languages[1].tag == L"en-US" &&
              no_en.languages[1].tips[0] == kStockUsKeyboard,
          "a missing English entry is added with the US keyboard first");

    // Case does not make a keyboard someone else's.
    AddNeokeyPlan cased = PlanAddNeokey({{L"vi", {ToLowerAscii(kVietnameseTip)}}}, false, {});
    Check(cased.replaced.empty() && !cased.changed, "Neokey's own TIP in another case is still Neokey's");

    Check(ShouldRecordPreNeokeyState(true, {}, false), "record: first install that adds the language");
    Check(ShouldRecordPreNeokeyState(false, {kStockVietnameseKeyboard}, false), "record: first install that prunes");
    Check(!ShouldRecordPreNeokeyState(false, {}, false), "record: a repair run records nothing");
    Check(!ShouldRecordPreNeokeyState(true, {kStockVietnameseKeyboard}, true), "record: an existing record stays");

    Check(ShouldRecordLayoutSubstitute(std::nullopt, L"", L"00000409"), "substitute: none on a machine that never had one");
    Check(ShouldRecordLayoutSubstitute(std::nullopt, L"0000042a", L"00000409"), "substitute: the user's own value");
    Check(!ShouldRecordLayoutSubstitute(std::nullopt, L"00000409", L"00000409"),
          "substitute: a value that reads like Neokey's own is not claimed");
    Check(!ShouldRecordLayoutSubstitute(std::wstring(L""), L"", L"00000409"), "substitute: an existing record stays");
}

void TestCleanup() {
    std::cout << "\nwhat Vietnamese becomes after Neokey\n";
    using Recorded = std::optional<std::vector<std::wstring>>;
    const std::wstring ms_telex = L"042A:{C2CB2CF0-AF47-413E-9780-8BC3A3C16068}{5FB02EC5-0A77-4684-B4FA-DEF8A2195628}";
    struct Case {
        const char* name;
        std::vector<std::wstring> remaining;
        Recorded recorded;
        bool added;
        bool only;
        bool display;
        CleanupAction expected;
    };
    const Case cases[] = {
        {"legacy install, Neokey was the only thing under Vietnamese", {}, std::nullopt, false, false, false, CleanupAction::RemoveLanguage},
        {"this install added the language", {}, std::nullopt, true, false, false, CleanupAction::RemoveLanguage},
        {"a keyboard was pruned at install", {}, Recorded({kStockVietnameseKeyboard}), false, false, false, CleanupAction::RestoreRecorded},
        {"a keyboard was added under Vietnamese after Neokey", {L"042A:00000042"}, std::nullopt, true, false, false, CleanupAction::Leave},
        {"restoring wins over leaving", {L"042A:00000042"}, Recorded({kStockVietnameseKeyboard}), false, false, false, CleanupAction::RestoreRecorded},
        {"Vietnamese is the only language", {}, std::nullopt, true, true, false, CleanupAction::InstallStockKeyboard},
        {"Windows is displayed in Vietnamese", {}, std::nullopt, true, false, true, CleanupAction::InstallStockKeyboard},
        {"the language was there and empty", {}, Recorded(std::vector<std::wstring>{}), false, false, false, CleanupAction::InstallStockKeyboard},
        {"we added the language and the record is empty", {}, Recorded(std::vector<std::wstring>{}), true, false, false, CleanupAction::RemoveLanguage},
        {"we added the language and an old record lists Windows' keyboard", {}, Recorded({ms_telex}), true, false, false, CleanupAction::RemoveLanguage},
        {"we added it, an old record, and it is the display language", {}, Recorded({kStockVietnameseKeyboard}), true, false, true, CleanupAction::InstallStockKeyboard},
    };
    for (const Case& test : cases) {
        const CleanupDecision decision =
            DecideVietnameseCleanup(test.remaining, test.recorded, test.added, test.only, test.display);
        Check(decision.action == test.expected, std::string("cleanup: ") + test.name);
    }

    Check(!ParseRecordedTips(std::nullopt).has_value(), "no record reads as unknown");
    Check(ParseRecordedTips(std::wstring(L"")).has_value() && ParseRecordedTips(std::wstring(L""))->empty(),
          "an empty record reads as empty");
    Check(ParseRecordedTips(std::wstring(L" ; 042A:0000042a;;")).value() == std::vector<std::wstring>{kStockVietnameseKeyboard},
          "blank parts of a record are dropped");

    // The whole trip on a machine that had no Vietnamese before Neokey.
    RemoveNeokeyPlan removed = PlanRemoveNeokey(
        {{L"en-US", {kStockUsKeyboard, kEnglishTip}}, {L"vi-VN", {kVietnameseTip}}},
        ParseRecordedTips(std::wstring(L"")), true, false);
    Check(removed.action == CleanupAction::RemoveLanguage &&
              Describe(removed.languages) == L"en-US[0409:00000409] ",
          "uninstall takes away the language it added and the English copy");

    RemoveNeokeyPlan restored = PlanRemoveNeokey(
        {{L"vi", {kVietnameseTip}}, {L"en-US", {kEnglishTip}}},
        ParseRecordedTips(std::wstring(L"042A:0000042a;") + ms_telex), false, false);
    Check(restored.action == CleanupAction::RestoreRecorded &&
              Describe(restored.languages) ==
                  L"vi[042A:0000042a;" + ms_telex + L"] en-US[0409:00000409] ",
          "uninstall restores what it replaced, and English is never left empty");

    RemoveNeokeyPlan absent = PlanRemoveNeokey({{L"en-US", {kStockUsKeyboard}}}, std::nullopt, false, false);
    Check(!absent.changed, "a list without Neokey is left as it is");

    Check(OrderVietnameseFirst({L"00000409", L"0000042a", L"00000804", L"00000409"}) ==
              std::vector<std::wstring>({L"0000042a", L"00000409", L"00000804"}),
          "input order: Vietnamese first, the rest in order, no duplicates");
}

void TestQuoting() {
    std::cout << "\ncommand-line quoting\n";
    const std::vector<std::wstring> arguments = {
        L"--log", L"C:\\Neokey [0.1.19] & co\\register_elevated.log", L"plain", L"", L"a \"quoted\" word",
        L"trailing\\", L"C:\\path with space\\", L"back\\\\\"slash",
    };
    std::wstring command_line = L"neokey_config.exe";
    for (const std::wstring& argument : arguments) {
        command_line += L" " + QuoteArgument(argument);
    }
    int count = 0;
    LPWSTR* parsed = CommandLineToArgvW(command_line.c_str(), &count);
    bool same = parsed != nullptr && count == static_cast<int>(arguments.size()) + 1;
    for (int index = 1; same && index < count; ++index) {
        same = arguments[static_cast<size_t>(index - 1)] == parsed[index];
    }
    if (parsed) {
        LocalFree(parsed);
    }
    Check(same, "every argument survives quoting and CommandLineToArgvW");
}

void TestSha256() {
    std::cout << "\nSHA-256\n";
    wchar_t temp_directory[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, temp_directory);
    const std::wstring path = JoinPath(temp_directory, L"neokey_setup_tests_sha256.bin");
    auto hash_of = [&](const std::string& bytes) {
        HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                                  nullptr);
        DWORD written = 0;
        if (!bytes.empty()) {
            WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
        }
        CloseHandle(file);
        return Sha256FileHex(path).value_or(L"");
    };
    Check(hash_of("") == L"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "empty input");
    Check(hash_of("abc") == L"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "\"abc\"");
    // The NIST vector of a million 'a's spans many read buffers.
    Check(hash_of(std::string(1000000, 'a')) ==
              L"cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
          "one million 'a's");
    DeleteFileW(path.c_str());
    Check(!Sha256FileHex(path).has_value(), "a missing file has no hash");
}

}  // namespace

int main() {
    SetConsoleOutputCP(CP_UTF8);
    std::cout << "========================================\n";
    std::cout << "   NEOKEY SETUP COMMAND TESTS\n";
    std::cout << "========================================\n";

    TestArguments();
    TestManifest();
    TestPackageProblemText();
    TestLocations();
    TestResolution();
    TestResidue();
    TestAddPlan();
    TestCleanup();
    TestQuoting();
    TestSha256();

    std::cout << "\nsetup_tests: " << g_passed << " passed, " << g_failed << " failed\n";
    return g_failed > 0 ? 1 : 0;
}
