#pragma once

#include <string_view>

namespace vn_ime::logger {

enum class Level {
    Debug,
    Info,
    Warning,
    Error
};

void Initialize();
void Shutdown();
void Log(Level level, std::wstring_view message);

// Formatted logging helper
void LogFormat(Level level, const wchar_t* format, ...);

// Environment detection
bool IsSecureDesktop();
bool IsAppContainer();

// Logging configuration
void SetEnabled(bool enabled);
bool IsEnabled();

} // namespace vn_ime::logger
