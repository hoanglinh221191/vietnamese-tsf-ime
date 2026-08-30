#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace vn_ime {

inline constexpr std::wstring_view SHORTHAND_DATE_TAG =
    L"{{DD/MM/YYYY}}";
inline constexpr std::wstring_view SHORTHAND_CLIPBOARD_TAG =
    L"{{CLIPBOARD}}";

struct DynamicShorthandValues {
    std::optional<std::wstring_view> date;
    std::optional<std::wstring_view> clipboard;
};

inline bool HasShorthandDateTag(std::wstring_view value) noexcept {
    return value.find(SHORTHAND_DATE_TAG) != std::wstring_view::npos;
}

inline bool HasShorthandClipboardTag(std::wstring_view value) noexcept {
    return value.find(SHORTHAND_CLIPBOARD_TAG) != std::wstring_view::npos;
}

inline std::optional<std::wstring> FormatShorthandDate(
    unsigned day, unsigned month, unsigned year) {
    if (day == 0 || day > 31 || month == 0 || month > 12 || year > 9999) {
        return std::nullopt;
    }

    std::wstring result(10, L'0');
    result[0] = static_cast<wchar_t>(L'0' + (day / 10));
    result[1] = static_cast<wchar_t>(L'0' + (day % 10));
    result[2] = L'/';
    result[3] = static_cast<wchar_t>(L'0' + (month / 10));
    result[4] = static_cast<wchar_t>(L'0' + (month % 10));
    result[5] = L'/';
    result[6] = static_cast<wchar_t>(L'0' + ((year / 1000) % 10));
    result[7] = static_cast<wchar_t>(L'0' + ((year / 100) % 10));
    result[8] = static_cast<wchar_t>(L'0' + ((year / 10) % 10));
    result[9] = static_cast<wchar_t>(L'0' + (year % 10));
    return result;
}

inline std::optional<std::wstring> ResolveDynamicShorthandTemplate(
    std::wstring_view shorthand_template,
    const DynamicShorthandValues& values,
    size_t max_output_chars) {
    const auto matched_tag_length = [&](size_t cursor, bool& use_date) {
        if (shorthand_template.substr(
                cursor, SHORTHAND_DATE_TAG.length()) ==
            SHORTHAND_DATE_TAG) {
            use_date = true;
            return SHORTHAND_DATE_TAG.length();
        }
        if (shorthand_template.substr(
                cursor, SHORTHAND_CLIPBOARD_TAG.length()) ==
            SHORTHAND_CLIPBOARD_TAG) {
            use_date = false;
            return SHORTHAND_CLIPBOARD_TAG.length();
        }
        return size_t{0};
    };

    // Validate every provider and the final size before copying clipboard data.
    // This keeps fail-closed paths from leaving a partial sensitive result.
    size_t final_length = 0;
    const auto add_length_bounded = [&](size_t length) {
        if (length > max_output_chars - final_length) {
            return false;
        }
        final_length += length;
        return true;
    };

    size_t cursor = 0;
    size_t literal_start = 0;
    while (cursor < shorthand_template.length()) {
        bool use_date = false;
        const size_t tag_length = matched_tag_length(cursor, use_date);
        if (tag_length == 0) {
            ++cursor;
            continue;
        }

        if (!add_length_bounded(cursor - literal_start)) {
            return std::nullopt;
        }

        const auto& replacement = use_date ? values.date : values.clipboard;
        if (!replacement || !add_length_bounded(replacement->length())) {
            return std::nullopt;
        }

        cursor += tag_length;
        literal_start = cursor;
    }

    if (!add_length_bounded(shorthand_template.length() - literal_start)) {
        return std::nullopt;
    }

    std::wstring result;
    result.reserve(final_length);
    cursor = 0;
    literal_start = 0;
    while (cursor < shorthand_template.length()) {
        bool use_date = false;
        const size_t tag_length = matched_tag_length(cursor, use_date);
        if (tag_length == 0) {
            ++cursor;
            continue;
        }

        result.append(shorthand_template.substr(
            literal_start, cursor - literal_start));
        const auto& replacement = use_date ? values.date : values.clipboard;
        result.append(*replacement);
        cursor += tag_length;
        literal_start = cursor;
    }
    result.append(shorthand_template.substr(literal_start));

    return result;
}

} // namespace vn_ime
