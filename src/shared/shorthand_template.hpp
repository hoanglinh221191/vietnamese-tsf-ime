#pragma once

#include <cstddef>
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace vn_ime {

inline constexpr std::wstring_view SHORTHAND_DATE_TAG =
    L"{{DD/MM/YYYY}}";
inline constexpr std::wstring_view SHORTHAND_DATE_ALIAS_TAG =
    L"{{DATE}}";
inline constexpr std::wstring_view SHORTHAND_TIME_TAG =
    L"{{TIME}}";
inline constexpr std::wstring_view SHORTHAND_WEEKDAY_TAG =
    L"{{WEEKDAY}}";
inline constexpr std::wstring_view SHORTHAND_UUID_TAG =
    L"{{UUID}}";
inline constexpr std::wstring_view SHORTHAND_NEWLINE_TAG =
    L"{{NEWLINE}}";
inline constexpr std::wstring_view SHORTHAND_TAB_TAG =
    L"{{TAB}}";
inline constexpr std::wstring_view SHORTHAND_CURSOR_TAG =
    L"{{CURSOR}}";
inline constexpr std::wstring_view SHORTHAND_CLIPBOARD_TAG =
    L"{{CLIPBOARD}}";
inline constexpr std::wstring_view SHORTHAND_CLIPBOARD_TRIM_TAG =
    L"{{CLIPBOARD|TRIM}}";
inline constexpr std::wstring_view SHORTHAND_CLIPBOARD_UPPER_TAG =
    L"{{CLIPBOARD|UPPER}}";
inline constexpr std::wstring_view SHORTHAND_CLIPBOARD_LOWER_TAG =
    L"{{CLIPBOARD|LOWER}}";
inline constexpr std::wstring_view SHORTHAND_SELECTION_TAG =
    L"{{SELECTION}}";

struct DynamicShorthandValues {
    std::optional<std::wstring_view> date;
    std::optional<std::wstring_view> time;
    std::optional<std::wstring_view> weekday;
    std::optional<std::wstring_view> uuid;
    std::optional<std::wstring_view> clipboard;
    std::optional<std::wstring_view> clipboard_trim;
    std::optional<std::wstring_view> clipboard_upper;
    std::optional<std::wstring_view> clipboard_lower;
    std::optional<std::wstring_view> selection;
};

struct DynamicShorthandResult {
    std::wstring text;
    std::optional<size_t> selection_start;
    std::optional<size_t> selection_end;

    [[nodiscard]] bool HasSelection() const noexcept {
        return selection_start.has_value() && selection_end.has_value();
    }
};

enum class ShorthandSelectionCapturePlan {
    Clear,
    Capture,
    Preserve,
};

inline ShorthandSelectionCapturePlan PlanShorthandSelectionCapture(
    bool key_starts_selection_shorthand,
    bool has_pending_selection) noexcept {
    if (!key_starts_selection_shorthand) {
        return ShorthandSelectionCapturePlan::Clear;
    }
    return has_pending_selection
        ? ShorthandSelectionCapturePlan::Preserve
        : ShorthandSelectionCapturePlan::Capture;
}

inline bool HasShorthandDateTag(std::wstring_view value) noexcept {
    return value.find(SHORTHAND_DATE_TAG) != std::wstring_view::npos ||
        value.find(SHORTHAND_DATE_ALIAS_TAG) != std::wstring_view::npos;
}

inline bool HasShorthandTimeTag(std::wstring_view value) noexcept {
    return value.find(SHORTHAND_TIME_TAG) != std::wstring_view::npos;
}

inline bool HasShorthandWeekdayTag(std::wstring_view value) noexcept {
    return value.find(SHORTHAND_WEEKDAY_TAG) != std::wstring_view::npos;
}

inline bool HasShorthandUuidTag(std::wstring_view value) noexcept {
    return value.find(SHORTHAND_UUID_TAG) != std::wstring_view::npos;
}

inline bool HasShorthandCursorTag(std::wstring_view value) noexcept {
    return value.find(SHORTHAND_CURSOR_TAG) != std::wstring_view::npos;
}

inline bool HasShorthandClipboardTag(std::wstring_view value) noexcept {
    return value.find(SHORTHAND_CLIPBOARD_TAG) != std::wstring_view::npos ||
        value.find(SHORTHAND_CLIPBOARD_TRIM_TAG) != std::wstring_view::npos ||
        value.find(SHORTHAND_CLIPBOARD_UPPER_TAG) != std::wstring_view::npos ||
        value.find(SHORTHAND_CLIPBOARD_LOWER_TAG) != std::wstring_view::npos;
}

inline bool HasShorthandClipboardTrimTag(
    std::wstring_view value) noexcept {
    return value.find(SHORTHAND_CLIPBOARD_TRIM_TAG) !=
        std::wstring_view::npos;
}

inline bool HasShorthandClipboardUpperTag(
    std::wstring_view value) noexcept {
    return value.find(SHORTHAND_CLIPBOARD_UPPER_TAG) !=
        std::wstring_view::npos;
}

inline bool HasShorthandClipboardLowerTag(
    std::wstring_view value) noexcept {
    return value.find(SHORTHAND_CLIPBOARD_LOWER_TAG) !=
        std::wstring_view::npos;
}

inline bool HasShorthandSelectionTag(std::wstring_view value) noexcept {
    return value.find(SHORTHAND_SELECTION_TAG) != std::wstring_view::npos;
}

inline bool IsShorthandTrimWhitespace(wchar_t ch) noexcept {
    return ch == L' ' || (ch >= L'\t' && ch <= L'\r') ||
        ch == L'\u0085' || ch == L'\u00A0' || ch == L'\u1680' ||
        (ch >= L'\u2000' && ch <= L'\u200A') ||
        ch == L'\u2028' || ch == L'\u2029' || ch == L'\u202F' ||
        ch == L'\u205F' || ch == L'\u3000';
}

inline std::wstring TrimShorthandText(std::wstring_view value) {
    size_t start = 0;
    while (start < value.length() &&
           IsShorthandTrimWhitespace(value[start])) {
        ++start;
    }
    size_t end = value.length();
    while (end > start && IsShorthandTrimWhitespace(value[end - 1])) {
        --end;
    }
    return std::wstring(value.substr(start, end - start));
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

inline std::optional<std::wstring> FormatShorthandTime(
    unsigned hour, unsigned minute) {
    if (hour > 23 || minute > 59) {
        return std::nullopt;
    }

    std::wstring result(5, L'0');
    result[0] = static_cast<wchar_t>(L'0' + (hour / 10));
    result[1] = static_cast<wchar_t>(L'0' + (hour % 10));
    result[2] = L':';
    result[3] = static_cast<wchar_t>(L'0' + (minute / 10));
    result[4] = static_cast<wchar_t>(L'0' + (minute % 10));
    return result;
}

inline std::optional<std::wstring_view> FormatShorthandWeekday(
    unsigned day_of_week) noexcept {
    static constexpr std::array<std::wstring_view, 7> kWeekdays = {
        L"Chủ nhật", L"Thứ Hai", L"Thứ Ba", L"Thứ Tư",
        L"Thứ Năm", L"Thứ Sáu", L"Thứ Bảy"};
    if (day_of_week >= kWeekdays.size()) {
        return std::nullopt;
    }
    return kWeekdays[day_of_week];
}

namespace shorthand_detail {

struct TagMatch {
    size_t length = 0;
    std::optional<std::wstring_view> replacement;
    bool is_cursor = false;
};

inline TagMatch MatchTag(
    std::wstring_view shorthand_template, size_t cursor,
    const DynamicShorthandValues& values) noexcept {
    const auto matches = [&](std::wstring_view tag) {
        return shorthand_template.substr(cursor, tag.length()) == tag;
    };

    if (matches(SHORTHAND_DATE_TAG)) {
        return {SHORTHAND_DATE_TAG.length(), values.date};
    }
    if (matches(SHORTHAND_DATE_ALIAS_TAG)) {
        return {SHORTHAND_DATE_ALIAS_TAG.length(), values.date};
    }
    if (matches(SHORTHAND_TIME_TAG)) {
        return {SHORTHAND_TIME_TAG.length(), values.time};
    }
    if (matches(SHORTHAND_WEEKDAY_TAG)) {
        return {SHORTHAND_WEEKDAY_TAG.length(), values.weekday};
    }
    if (matches(SHORTHAND_UUID_TAG)) {
        return {SHORTHAND_UUID_TAG.length(), values.uuid};
    }
    if (matches(SHORTHAND_NEWLINE_TAG)) {
        return {SHORTHAND_NEWLINE_TAG.length(), std::wstring_view(L"\r\n")};
    }
    if (matches(SHORTHAND_TAB_TAG)) {
        return {SHORTHAND_TAB_TAG.length(), std::wstring_view(L"\t")};
    }
    if (matches(SHORTHAND_CURSOR_TAG)) {
        return {
            SHORTHAND_CURSOR_TAG.length(), std::wstring_view(L""), true};
    }
    if (matches(SHORTHAND_CLIPBOARD_TRIM_TAG)) {
        return {
            SHORTHAND_CLIPBOARD_TRIM_TAG.length(), values.clipboard_trim};
    }
    if (matches(SHORTHAND_CLIPBOARD_UPPER_TAG)) {
        return {
            SHORTHAND_CLIPBOARD_UPPER_TAG.length(), values.clipboard_upper};
    }
    if (matches(SHORTHAND_CLIPBOARD_LOWER_TAG)) {
        return {
            SHORTHAND_CLIPBOARD_LOWER_TAG.length(), values.clipboard_lower};
    }
    if (matches(SHORTHAND_CLIPBOARD_TAG)) {
        return {SHORTHAND_CLIPBOARD_TAG.length(), values.clipboard};
    }
    if (matches(SHORTHAND_SELECTION_TAG)) {
        return {SHORTHAND_SELECTION_TAG.length(), values.selection};
    }
    return {};
}

} // namespace shorthand_detail

inline std::optional<DynamicShorthandResult>
ResolveDynamicShorthandTemplateWithSelection(
    std::wstring_view shorthand_template,
    const DynamicShorthandValues& values,
    size_t max_output_chars) {
    // Validate every provider and the final size before copying clipboard data.
    // This keeps fail-closed paths from leaving a partial sensitive result.
    size_t final_length = 0;
    std::optional<size_t> selection_start;
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
        const shorthand_detail::TagMatch match =
            shorthand_detail::MatchTag(shorthand_template, cursor, values);
        if (match.length == 0) {
            ++cursor;
            continue;
        }

        if (!add_length_bounded(cursor - literal_start)) {
            return std::nullopt;
        }

        if (match.is_cursor) {
            if (selection_start) {
                return std::nullopt;
            }
            selection_start = final_length;
        }

        if (!match.replacement ||
            !add_length_bounded(match.replacement->length())) {
            return std::nullopt;
        }

        cursor += match.length;
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
        const shorthand_detail::TagMatch match =
            shorthand_detail::MatchTag(shorthand_template, cursor, values);
        if (match.length == 0) {
            ++cursor;
            continue;
        }

        result.append(shorthand_template.substr(
            literal_start, cursor - literal_start));
        result.append(*match.replacement);
        cursor += match.length;
        literal_start = cursor;
    }
    result.append(shorthand_template.substr(literal_start));

    return DynamicShorthandResult{
        std::move(result), selection_start, selection_start};
}

inline std::optional<std::wstring> ResolveDynamicShorthandTemplate(
    std::wstring_view shorthand_template,
    const DynamicShorthandValues& values,
    size_t max_output_chars) {
    auto resolved = ResolveDynamicShorthandTemplateWithSelection(
        shorthand_template, values, max_output_chars);
    if (!resolved) {
        return std::nullopt;
    }
    return std::move(resolved->text);
}

} // namespace vn_ime
