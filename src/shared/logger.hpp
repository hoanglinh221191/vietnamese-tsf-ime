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

} // namespace vn_ime::logger
