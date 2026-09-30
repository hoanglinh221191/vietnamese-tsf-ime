#pragma once

// neokey_config.exe as its own installer: the command-line half. The tray app
// hands over here before it does anything else when it is started with a
// setup switch (--status, --verify, ...), and exits with what this returns.

#include <windows.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "setup_logic.hpp"

namespace vn_ime::setup {

// Where a setup command's words go: the console it was started from (or the
// file its output was redirected to), and the log file it was told to keep.
class SetupReport {
public:
    explicit SetupReport(const std::wstring& log_path);
    ~SetupReport();
    SetupReport(const SetupReport&) = delete;
    SetupReport& operator=(const SetupReport&) = delete;

    void Line(std::wstring_view text);
    void Warning(std::wstring_view text);
    void Error(std::wstring_view text);

    // Whether anyone can read the lines as they are written. A double-clicked
    // exe has no console, and there a failure has to be shown in a window.
    bool HasConsole() const noexcept { return output_ != nullptr; }
    const std::vector<std::wstring>& Lines() const noexcept { return lines_; }

private:
    void Write(std::wstring_view text);

    HANDLE output_ = nullptr;
    bool output_is_console_ = false;
    bool owns_output_ = false;
    HANDLE log_ = INVALID_HANDLE_VALUE;
    std::vector<std::wstring> lines_;
};

// Lower-case hex SHA-256 of a file, or nothing when it cannot be read.
std::optional<std::wstring> Sha256FileHex(const std::wstring& path);

// The folder the running exe is in, which is the package folder.
std::wstring ExecutableDirectory();

// Checks every file the manifest in `directory` lists: present, the right
// size, the right hash. `required_files` are the ones the manifest must list.
PackageCheck CheckPackage(const std::wstring& directory,
                          const std::vector<std::wstring>& required_files);

// Everything --status reports, in the order register.ps1 -Status did, so the
// two can be compared line by line while both exist.
void WriteStatus(SetupReport& report, const std::wstring& package_directory);

// Runs a parsed setup command and returns the process exit code: 0 when it
// did what it was asked, 1 when it failed, 2 when the command line was wrong.
int RunSetupCommand(const SetupOptions& options);

}  // namespace vn_ime::setup
