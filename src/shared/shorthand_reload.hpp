#pragma once

#include <windows.h>
#include <cstdint>
#include <optional>
#include <string>

namespace vn_ime {

struct ShorthandFileVersion {
    bool exists = false;
    std::uint64_t last_write = 0;
    std::uint64_t size = 0;

    bool operator==(const ShorthandFileVersion&) const = default;
};

inline std::optional<ShorthandFileVersion> ReadShorthandFileVersion(
    const std::wstring& path) noexcept {
    if (path.empty()) {
        return std::nullopt;
    }

    WIN32_FILE_ATTRIBUTE_DATA attributes{};
    if (!GetFileAttributesExW(
            path.c_str(), GetFileExInfoStandard, &attributes)) {
        const DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND ||
            error == ERROR_PATH_NOT_FOUND) {
            return ShorthandFileVersion{};
        }
        return std::nullopt;
    }
    if ((attributes.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        return std::nullopt;
    }

    ULARGE_INTEGER last_write{};
    last_write.LowPart = attributes.ftLastWriteTime.dwLowDateTime;
    last_write.HighPart = attributes.ftLastWriteTime.dwHighDateTime;
    ULARGE_INTEGER size{};
    size.LowPart = attributes.nFileSizeLow;
    size.HighPart = attributes.nFileSizeHigh;
    return ShorthandFileVersion{
        true, last_write.QuadPart, size.QuadPart};
}

inline bool ShouldReloadShorthandFile(
    const std::optional<ShorthandFileVersion>& loaded,
    const std::optional<ShorthandFileVersion>& observed) noexcept {
    return observed.has_value() &&
        (!loaded.has_value() || *loaded != *observed);
}

} // namespace vn_ime
