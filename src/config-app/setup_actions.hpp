#pragma once

// The setup commands that change the machine: registering and unregistering
// the DLLs (the Administrator half), configuring and unconfiguring the user
// (the desktop half), and the --install and --uninstall that run both. Each
// follows the register.ps1 function it replaces, and says which.

#include <string>

#include "setup_commands.hpp"
#include "setup_logic.hpp"

namespace vn_ime::setup {

// Each returns true when it did what it was asked; what happened, and why it
// failed, is in the report.
bool RegisterElevated(const SetupOptions& options, const std::wstring& package_directory, SetupReport& report);
bool UnregisterElevated(const std::wstring& package_directory, SetupReport& report);
bool ConfigureUser(const SetupOptions& options, const std::wstring& package_directory, SetupReport& report);
bool UnconfigureUser(const SetupOptions& options, const std::wstring& package_directory, SetupReport& report);
bool Install(const SetupOptions& options, const std::wstring& package_directory, SetupReport& report);
bool Uninstall(const SetupOptions& options, const std::wstring& package_directory, SetupReport& report);

bool IsProcessElevated();

// Settings and messages shown to the person follow the app's own language
// choice, which is Vietnamese unless they picked English.
bool UserPrefersVietnamese();

}  // namespace vn_ime::setup
