#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <richedit.h>
#include <shlobj.h>
#include <uxtheme.h>
#include <algorithm>
#include <array>
#include <new>
#include <string>
#include <vector>
#include "resources.h"
#include "config.hpp"
#include "dialog_layout.hpp"
#include "shorthand_template.hpp"
#include "tray_click_state.hpp"

using namespace vn_ime;

extern std::wstring g_lastActiveProcessName;

namespace {

constexpr DWORD kDwmUseImmersiveDarkMode = 20;
constexpr DWORD kDwmWindowCornerPreference = 33;
constexpr DWORD kDwmSystemBackdropType = 38;
constexpr DWORD kDwmCornerRound = 2;
constexpr DWORD kDwmBackdropNone = 1;
constexpr DWORD kDwmBackdropMainWindow = 2;
constexpr DWORD kDwmBackdropTransientWindow = 3;

struct UiPalette {
    COLORREF background;
    COLORREF surface;
    COLORREF input_surface;
    COLORREF border;
    COLORREF accent;
    COLORREF accent_hover;
    COLORREF text;
    COLORREF secondary_text;
    COLORREF disabled_text;
    COLORREF warning_text;
};

constexpr UiPalette kLightPalette{
    RGB(250, 250, 250), RGB(235, 235, 235), RGB(218, 218, 218),
    RGB(184, 184, 184), RGB(0, 103, 184), RGB(0, 120, 215),
    RGB(26, 26, 26), RGB(78, 78, 78), RGB(128, 128, 128),
    RGB(164, 38, 44)};
constexpr UiPalette kDarkPalette{
    RGB(17, 17, 17), RGB(42, 42, 42), RGB(56, 56, 56),
    RGB(82, 82, 82), RGB(0, 120, 215), RGB(0, 145, 245),
    RGB(248, 248, 248), RGB(205, 205, 205), RGB(140, 140, 140),
    RGB(255, 153, 164)};

HFONT g_uiFont = nullptr;
HFONT g_sectionFont = nullptr;
HFONT g_supportingFont = nullptr;
HFONT g_methodButtonFont = nullptr;
HBRUSH g_lightBackgroundBrush = nullptr;
HBRUSH g_lightSurfaceBrush = nullptr;
HBRUSH g_lightInputBrush = nullptr;
HBRUSH g_darkBackgroundBrush = nullptr;
HBRUSH g_darkSurfaceBrush = nullptr;
HBRUSH g_darkInputBrush = nullptr;
bool g_uiDarkMode = false;
bool g_uiHighContrast = false;
bool g_uiStyleRefreshInProgress = false;

inline constexpr UINT_PTR kShorthandSyntaxTimerId = 41;
inline constexpr UINT kShorthandRecolorMessage = WM_APP + 41;
inline constexpr UINT_PTR kAccentToggleSubclassId = 0x4E4B;
inline constexpr UINT_PTR kStableActionButtonSubclassId = 0x4E4C;

struct MainDialogChildLayout {
    HWND hwnd = nullptr;
    RECT original_rect{};
    bool footer = false;
    bool initially_visible = true;
};

struct MainDialogLayoutState {
    HWND hwnd = nullptr;
    int original_client_height = 0;
    int original_footer_top = 0;
    int footer_top = 0;
    int max_scroll = 0;
    int scroll_offset = 0;
    std::vector<MainDialogChildLayout> children;
};

MainDialogLayoutState g_mainDialogLayout;

struct MainFuzzyInputState {
    DWORD pending_flags = 0;
};

struct FuzzyInputDialogState {
    DWORD flags = 0;
    bool english = false;
};

UINT GetWindowDpiCompat(HWND hwnd) noexcept;

bool IsMainDialogFooterControl(HWND dialog, HWND child) noexcept {
    return child == GetDlgItem(dialog, IDC_STATIC_FOOTER_SEPARATOR) ||
           child == GetDlgItem(dialog, IDC_STATIC_VERSION) ||
           child == GetDlgItem(dialog, IDCANCEL) ||
           child == GetDlgItem(dialog, IDAPPLY) ||
           child == GetDlgItem(dialog, IDOK);
}

void CaptureMainDialogLayout(HWND hwnd) {
    g_mainDialogLayout = {};
    g_mainDialogLayout.hwnd = hwnd;

    RECT client{};
    GetClientRect(hwnd, &client);
    g_mainDialogLayout.original_client_height = client.bottom;

    HWND separator = GetDlgItem(hwnd, IDC_STATIC_FOOTER_SEPARATOR);
    RECT separator_rect{};
    if (separator) {
        GetWindowRect(separator, &separator_rect);
        MapWindowPoints(
            nullptr, hwnd, reinterpret_cast<POINT*>(&separator_rect), 2);
        g_mainDialogLayout.original_footer_top = separator_rect.top;
    } else {
        g_mainDialogLayout.original_footer_top = client.bottom;
    }

    for (HWND child = GetWindow(hwnd, GW_CHILD); child;
         child = GetWindow(child, GW_HWNDNEXT)) {
        RECT rect{};
        GetWindowRect(child, &rect);
        MapWindowPoints(nullptr, hwnd, reinterpret_cast<POINT*>(&rect), 2);
        const bool footer = IsMainDialogFooterControl(hwnd, child);
        g_mainDialogLayout.children.push_back({
            .hwnd = child,
            .original_rect = rect,
            .footer = footer,
            .initially_visible = ShouldKeepDialogTemplateChildVisible(
                footer,
                (GetWindowLongPtrW(child, GWL_STYLE) & WS_VISIBLE) != 0),
        });
    }
}

void ApplyMainDialogViewport(HWND hwnd) {
    if (g_mainDialogLayout.hwnd != hwnd) {
        return;
    }

    RECT client{};
    GetClientRect(hwnd, &client);
    const DialogVerticalFit fit = ComputeDialogVerticalFit(
        g_mainDialogLayout.original_client_height,
        g_mainDialogLayout.original_footer_top, client.bottom);
    g_mainDialogLayout.footer_top = fit.footer_top;
    g_mainDialogLayout.max_scroll = fit.max_scroll;
    g_mainDialogLayout.scroll_offset = (std::clamp)(
        g_mainDialogLayout.scroll_offset, 0, fit.max_scroll);

    SCROLLINFO scroll_info{};
    scroll_info.cbSize = sizeof(scroll_info);
    scroll_info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    scroll_info.nMin = 0;
    scroll_info.nPage = static_cast<UINT>((std::max)(1, fit.footer_top));
    scroll_info.nMax = fit.max_scroll +
        static_cast<int>(scroll_info.nPage) - 1;
    scroll_info.nPos = g_mainDialogLayout.scroll_offset;
    SetScrollInfo(hwnd, SB_VERT, &scroll_info, TRUE);

    if (fit.max_scroll == 0 &&
        client.bottom == g_mainDialogLayout.original_client_height) {
        RedrawWindow(
            hwnd, nullptr, nullptr,
            RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN |
                RDW_UPDATENOW);
        return;
    }

    SendMessageW(hwnd, WM_SETREDRAW, FALSE, 0);
    for (const auto& child : g_mainDialogLayout.children) {
        const int width = child.original_rect.right - child.original_rect.left;
        const int height = child.original_rect.bottom - child.original_rect.top;
        const int top = child.footer
            ? fit.footer_top +
                  (child.original_rect.top -
                   g_mainDialogLayout.original_footer_top)
            : child.original_rect.top - g_mainDialogLayout.scroll_offset;
        const bool in_content_view =
            child.footer ||
            (top >= 0 && top + height <= fit.footer_top);
        const bool should_show = child.initially_visible && in_content_view;
        SetWindowPos(
            child.hwnd, nullptr, child.original_rect.left, top, width, height,
            SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOREDRAW |
                (should_show ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
    }
    SendMessageW(hwnd, WM_SETREDRAW, TRUE, 0);
    for (const auto& child : g_mainDialogLayout.children) {
        if (IsWindowVisible(child.hwnd)) {
            RedrawWindow(
                child.hwnd, nullptr, nullptr,
                RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_UPDATENOW);
        }
    }
    RedrawWindow(
        hwnd, nullptr, nullptr,
        RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN |
            RDW_UPDATENOW);
}

void FitMainDialogToWorkArea(HWND hwnd) {
    CaptureMainDialogLayout(hwnd);

    RECT window_rect{};
    GetWindowRect(hwnd, &window_rect);
    MONITORINFO monitor_info{};
    monitor_info.cbSize = sizeof(monitor_info);
    const HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    if (!monitor || !GetMonitorInfoW(monitor, &monitor_info)) {
        return;
    }

    const int margin = MulDiv(8, static_cast<int>(GetWindowDpiCompat(hwnd)), 96);
    const int available_width =
        (std::max)(1, static_cast<int>(
            monitor_info.rcWork.right - monitor_info.rcWork.left) - 2 * margin);
    const int available_height =
        (std::max)(1, static_cast<int>(
            monitor_info.rcWork.bottom - monitor_info.rcWork.top) - 2 * margin);
    const int current_width = window_rect.right - window_rect.left;
    const int current_height = window_rect.bottom - window_rect.top;
    const int fitted_width = (std::min)(current_width, available_width);
    const int fitted_height = (std::min)(current_height, available_height);
    const int left = monitor_info.rcWork.left +
        ((monitor_info.rcWork.right - monitor_info.rcWork.left) - fitted_width) / 2;
    const int top = monitor_info.rcWork.top +
        ((monitor_info.rcWork.bottom - monitor_info.rcWork.top) - fitted_height) / 2;

    const bool needs_scroll = fitted_height < current_height;
    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    style = needs_scroll ? (style | WS_VSCROLL) : (style & ~WS_VSCROLL);
    SetWindowLongPtrW(hwnd, GWL_STYLE, style);
    SetWindowPos(
        hwnd, nullptr, left, top, fitted_width, fitted_height,
        SWP_NOACTIVATE | SWP_NOZORDER | SWP_FRAMECHANGED);
    ApplyMainDialogViewport(hwnd);
}

void ScrollMainDialog(HWND hwnd, int requested_offset) {
    if (g_mainDialogLayout.hwnd != hwnd ||
        g_mainDialogLayout.max_scroll == 0) {
        return;
    }
    const int next_offset = (std::clamp)(
        requested_offset, 0, g_mainDialogLayout.max_scroll);
    if (next_offset == g_mainDialogLayout.scroll_offset) {
        return;
    }
    g_mainDialogLayout.scroll_offset = next_offset;
    ApplyMainDialogViewport(hwnd);
}

DWORD ReadPersonalizeDword(const wchar_t* value_name, DWORD fallback) noexcept {
    DWORD value = fallback;
    DWORD value_size = sizeof(value);
    if (RegGetValueW(
            HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            value_name, RRF_RT_REG_DWORD, nullptr, &value,
            &value_size) != ERROR_SUCCESS) {
        return fallback;
    }
    return value;
}

bool IsHighContrastEnabled() noexcept {
    HIGHCONTRASTW high_contrast{};
    high_contrast.cbSize = sizeof(high_contrast);
    return SystemParametersInfoW(
               SPI_GETHIGHCONTRAST, sizeof(high_contrast), &high_contrast, 0) &&
        (high_contrast.dwFlags & HCF_HIGHCONTRASTON) != 0;
}

UINT GetWindowDpiCompat(HWND hwnd) noexcept {
    using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
    const HMODULE user32 = GetModuleHandleW(L"user32.dll");
    const auto get_dpi_for_window = user32
        ? reinterpret_cast<GetDpiForWindowFn>(
              GetProcAddress(user32, "GetDpiForWindow"))
        : nullptr;
    if (get_dpi_for_window) {
        const UINT dpi = get_dpi_for_window(hwnd);
        if (dpi != 0) {
            return dpi;
        }
    }

    HDC screen_dc = GetDC(hwnd);
    const UINT dpi = screen_dc
        ? static_cast<UINT>(GetDeviceCaps(screen_dc, LOGPIXELSY))
        : 96u;
    if (screen_dc) {
        ReleaseDC(hwnd, screen_dc);
    }
    return dpi == 0 ? 96u : dpi;
}

HFONT CreateUiFont(HWND hwnd, int point_size, LONG weight) noexcept {
    LOGFONTW log_font{};
    log_font.lfHeight = -MulDiv(
        point_size, static_cast<int>(GetWindowDpiCompat(hwnd)), 72);
    log_font.lfWeight = weight;
    log_font.lfCharSet = DEFAULT_CHARSET;
    log_font.lfOutPrecision = OUT_DEFAULT_PRECIS;
    log_font.lfClipPrecision = CLIP_DEFAULT_PRECIS;
    log_font.lfQuality = CLEARTYPE_QUALITY;
    log_font.lfPitchAndFamily = DEFAULT_PITCH | FF_SWISS;
    wcscpy_s(log_font.lfFaceName, L"Segoe UI Variable Text");
    return CreateFontIndirectW(&log_font);
}

void EnsureModernUiResources(HWND hwnd) noexcept {
    if (!g_uiFont) {
        g_uiFont = CreateUiFont(hwnd, 9, FW_NORMAL);
    }
    if (!g_sectionFont) {
        g_sectionFont = CreateUiFont(hwnd, 10, FW_SEMIBOLD);
    }
    if (!g_supportingFont) {
        g_supportingFont = CreateUiFont(hwnd, 9, FW_NORMAL);
    }
    if (!g_methodButtonFont) {
        g_methodButtonFont = CreateUiFont(hwnd, 10, FW_NORMAL);
    }
    if (!g_lightBackgroundBrush) {
        g_lightBackgroundBrush = CreateSolidBrush(kLightPalette.background);
        g_lightSurfaceBrush = CreateSolidBrush(kLightPalette.surface);
        g_lightInputBrush = CreateSolidBrush(kLightPalette.input_surface);
        g_darkBackgroundBrush = CreateSolidBrush(kDarkPalette.background);
        g_darkSurfaceBrush = CreateSolidBrush(kDarkPalette.surface);
        g_darkInputBrush = CreateSolidBrush(kDarkPalette.input_surface);
        g_uiHighContrast = IsHighContrastEnabled();
        g_uiDarkMode = !g_uiHighContrast &&
            ReadPersonalizeDword(L"AppsUseLightTheme", 1) == 0;
    }
}

void DestroyModernUiResources() noexcept {
    if (g_uiFont) DeleteObject(g_uiFont);
    if (g_sectionFont) DeleteObject(g_sectionFont);
    if (g_supportingFont) DeleteObject(g_supportingFont);
    if (g_methodButtonFont) DeleteObject(g_methodButtonFont);
    if (g_lightBackgroundBrush) DeleteObject(g_lightBackgroundBrush);
    if (g_lightSurfaceBrush) DeleteObject(g_lightSurfaceBrush);
    if (g_lightInputBrush) DeleteObject(g_lightInputBrush);
    if (g_darkBackgroundBrush) DeleteObject(g_darkBackgroundBrush);
    if (g_darkSurfaceBrush) DeleteObject(g_darkSurfaceBrush);
    if (g_darkInputBrush) DeleteObject(g_darkInputBrush);
    g_uiFont = nullptr;
    g_sectionFont = nullptr;
    g_supportingFont = nullptr;
    g_methodButtonFont = nullptr;
    g_lightBackgroundBrush = nullptr;
    g_lightSurfaceBrush = nullptr;
    g_lightInputBrush = nullptr;
    g_darkBackgroundBrush = nullptr;
    g_darkSurfaceBrush = nullptr;
    g_darkInputBrush = nullptr;
}

const UiPalette& CurrentUiPalette() noexcept {
    return g_uiDarkMode ? kDarkPalette : kLightPalette;
}

HBRUSH CurrentBackgroundBrush() noexcept {
    if (g_uiHighContrast) {
        return GetSysColorBrush(COLOR_BTNFACE);
    }
    return g_uiDarkMode ? g_darkBackgroundBrush : g_lightBackgroundBrush;
}

HBRUSH CurrentSurfaceBrush() noexcept {
    if (g_uiHighContrast) {
        return GetSysColorBrush(COLOR_WINDOW);
    }
    return g_uiDarkMode ? g_darkSurfaceBrush : g_lightSurfaceBrush;
}

HBRUSH CurrentInputBrush() noexcept {
    if (g_uiHighContrast) {
        return GetSysColorBrush(COLOR_WINDOW);
    }
    return g_uiDarkMode ? g_darkInputBrush : g_lightInputBrush;
}

constexpr std::array<int, 9> kSurfaceMarkerIds{
    IDC_PANEL_METHOD,
    IDC_PANEL_OPTIONS,
    IDC_PANEL_UTILITIES,
    IDC_PANEL_APP_PROFILES,
    IDC_PANEL_HOTKEY,
    IDC_PANEL_STARTUP,
    IDC_PANEL_SHORTHAND_VARIABLES,
    IDC_PANEL_SHORTHAND_HELP,
    IDC_STATIC_DIRECT_DESC,
};

void HideSurfaceLayoutMarkers(HWND hwnd) noexcept {
    for (const int control_id : kSurfaceMarkerIds) {
        if (HWND marker = GetDlgItem(hwnd, control_id)) {
            ShowWindow(marker, SW_HIDE);
        }
    }
}

bool GetChildRectInParent(
    HWND parent, int control_id, RECT& rect) noexcept {
    HWND child = GetDlgItem(parent, control_id);
    if (!child || !GetWindowRect(child, &rect)) {
        return false;
    }
    MapWindowPoints(nullptr, parent, reinterpret_cast<POINT*>(&rect), 2);
    return true;
}

int ScaleUi(HWND hwnd, int value) noexcept {
    return MulDiv(value, static_cast<int>(GetWindowDpiCompat(hwnd)), 96);
}

void DrawRoundedSurfaceWithColors(
    HWND hwnd, HDC dc, const RECT& rect,
    COLORREF fill_color, COLORREF border_color) noexcept {
    if (g_uiHighContrast) {
        FillRect(dc, &rect, CurrentSurfaceBrush());
        FrameRect(dc, &rect, GetSysColorBrush(COLOR_WINDOWFRAME));
        return;
    }

    HBRUSH brush = CreateSolidBrush(fill_color);
    HPEN pen = CreatePen(PS_SOLID, 1, border_color);
    HGDIOBJ old_pen = SelectObject(dc, pen);
    HGDIOBJ old_brush = SelectObject(dc, brush);
    const int radius = ScaleUi(hwnd, 8);
    RoundRect(
        dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
    DeleteObject(pen);
    DeleteObject(brush);
}

void DrawRoundedSurface(HWND hwnd, HDC dc, const RECT& rect) noexcept {
    const UiPalette& palette = CurrentUiPalette();
    DrawRoundedSurfaceWithColors(
        hwnd, dc, rect, palette.surface, palette.border);
}

void DrawInformationSurface(HWND hwnd, HDC dc, const RECT& rect) noexcept {
    const COLORREF fill = g_uiDarkMode
        ? RGB(218, 218, 218)
        : RGB(235, 235, 235);
    const COLORREF border = g_uiDarkMode
        ? RGB(184, 184, 184)
        : RGB(202, 202, 202);
    DrawRoundedSurfaceWithColors(hwnd, dc, rect, fill, border);
}

void DrawUiText(
    HDC dc, const std::wstring& text, RECT rect, HFONT font,
    COLORREF color, UINT format) noexcept {
    HGDIOBJ old_font = font ? SelectObject(dc, font) : nullptr;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextW(dc, text.c_str(), static_cast<int>(text.length()), &rect, format);
    if (old_font) {
        SelectObject(dc, old_font);
    }
}

std::wstring CurrentShorthandDateText() {
    SYSTEMTIME local_time{};
    GetLocalTime(&local_time);
    return FormatShorthandDate(
               local_time.wDay, local_time.wMonth, local_time.wYear)
        .value_or(L"--/--/----");
}

std::wstring CurrentShorthandTimeText() {
    SYSTEMTIME local_time{};
    GetLocalTime(&local_time);
    return FormatShorthandTime(local_time.wHour, local_time.wMinute)
        .value_or(L"--:--");
}

std::wstring CurrentShorthandWeekdayText(bool vietnamese) {
    SYSTEMTIME local_time{};
    GetLocalTime(&local_time);
    if (vietnamese) {
        const auto value = FormatShorthandWeekday(local_time.wDayOfWeek);
        return value ? std::wstring(*value) : L"-";
    }
    static constexpr std::array<const wchar_t*, 7> kEnglishWeekdays{
        L"Sunday", L"Monday", L"Tuesday", L"Wednesday",
        L"Thursday", L"Friday", L"Saturday"};
    return local_time.wDayOfWeek < kEnglishWeekdays.size()
        ? kEnglishWeekdays[local_time.wDayOfWeek]
        : L"-";
}

void DrawShorthandVariablesPanel(
    HWND hwnd, HDC dc, const RECT& rect, bool vietnamese) noexcept {
    DrawInformationSurface(hwnd, dc, rect);
    const UiPalette& palette = CurrentUiPalette();
    const COLORREF information_text = g_uiHighContrast
        ? GetSysColor(COLOR_WINDOWTEXT)
        : RGB(28, 28, 28);
    const COLORREF information_secondary = g_uiHighContrast
        ? GetSysColor(COLOR_GRAYTEXT)
        : RGB(78, 78, 78);
    const int inset = ScaleUi(hwnd, 12);
    const int title_height = ScaleUi(hwnd, 24);
    RECT title_rect{
        rect.left + inset, rect.top + ScaleUi(hwnd, 7),
        rect.right - inset, rect.top + title_height};
    DrawUiText(
        dc, vietnamese ? L"Biến và kết quả" : L"Variables and results",
        title_rect, g_sectionFont, information_text,
        DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);

    const std::array<std::pair<const wchar_t*, std::wstring>, 10> rows{{
        {L"{{DATE}}", CurrentShorthandDateText()},
        {L"{{DD/MM/YYYY}}", CurrentShorthandDateText()},
        {L"{{TIME}}", CurrentShorthandTimeText()},
        {L"{{WEEKDAY}}", CurrentShorthandWeekdayText(vietnamese)},
        {L"{{UUID}}", L"8c21...4a90"},
        {L"{{CLIPBOARD}}", vietnamese ? L"nội dung đã sao chép" : L"copied text"},
        {L"{{SELECTION}}", vietnamese ? L"văn bản đang chọn" : L"selected text"},
        {L"{{CURSOR}}", vietnamese ? L"vị trí gõ tiếp" : L"next typing position"},
        {L"{{NEWLINE}}", vietnamese ? L"xuống dòng" : L"new line"},
        {L"{{TAB}}", vietnamese ? L"thụt dòng" : L"tab"},
    }};

    const int row_height = ScaleUi(hwnd, 20);
    int top = rect.top + ScaleUi(hwnd, 29);
    const int arrow_left = rect.left + ((rect.right - rect.left) * 52) / 100;
    const int value_left = rect.left + ((rect.right - rect.left) * 59) / 100;
    for (const auto& [tag, value] : rows) {
        RECT tag_rect{
            rect.left + inset, top, arrow_left - ScaleUi(hwnd, 5),
            top + row_height};
        DrawUiText(
            dc, tag, tag_rect, g_supportingFont, palette.accent_hover,
            DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        RECT arrow_rect{
            arrow_left, top, value_left - ScaleUi(hwnd, 3),
            top + row_height};
        DrawUiText(
            dc, L"→", arrow_rect, g_supportingFont, information_secondary,
            DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        RECT value_rect{
            value_left, top, rect.right - inset, top + row_height};
        DrawUiText(
            dc, value, value_rect, g_supportingFont, information_text,
            DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        top += row_height;
    }
}

void DrawShorthandHelpPanel(
    HWND hwnd, HDC dc, const RECT& rect, bool vietnamese) noexcept {
    DrawInformationSurface(hwnd, dc, rect);
    const COLORREF information_text = g_uiHighContrast
        ? GetSysColor(COLOR_WINDOWTEXT)
        : RGB(28, 28, 28);
    const int inset = ScaleUi(hwnd, 12);
    RECT title_rect{
        rect.left + inset, rect.top + ScaleUi(hwnd, 7),
        rect.right - inset, rect.top + ScaleUi(hwnd, 25)};
    DrawUiText(
        dc, vietnamese ? L"Hướng dẫn" : L"Quick guide", title_rect,
        g_sectionFont, information_text,
        DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    const std::array<const wchar_t*, 5> vi_lines{
        L"• Mỗi dòng: phím=nội dung",
        L"• Gõ phím rồi nhấn Space để bung",
        L"• Bôi đen rồi gõ mã dùng SELECTION",
        L"• CURSOR đặt vị trí gõ tiếp",
        L"• Dấu | thêm TRIM, UPPER hoặc LOWER"};
    const std::array<const wchar_t*, 5> en_lines{
        L"• One line: key=text",
        L"• Type the key, then press Space",
        L"• Select text, then type a SELECTION key",
        L"• CURSOR marks where typing continues",
        L"• | adds TRIM, UPPER, or LOWER"};
    const int row_height = ScaleUi(hwnd, 20);
    int top = rect.top + ScaleUi(hwnd, 30);
    for (size_t i = 0; i < vi_lines.size(); ++i) {
        RECT line_rect{
            rect.left + inset, top, rect.right - inset, top + row_height};
        DrawUiText(
            dc, vietnamese ? vi_lines[i] : en_lines[i], line_rect,
            g_supportingFont, information_text,
            DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        top += row_height;
    }
}

void DrawDirectHelpPanel(
    HWND hwnd, HDC dc, const RECT& rect, bool vietnamese) noexcept {
    DrawInformationSurface(hwnd, dc, rect);
    const UiPalette& palette = CurrentUiPalette();
    const COLORREF information_text = g_uiHighContrast
        ? GetSysColor(COLOR_WINDOWTEXT)
        : RGB(28, 28, 28);
    const int inset = ScaleUi(hwnd, 12);
    const std::array<std::wstring, 4> lines = vietnamese
        ? std::array<std::wstring, 4>{
              L"Mỗi dòng nhập một tiến trình kèm chế độ. Ví dụ:",
              L"app.exe hoặc app.exe:inline   = Direct Inline, hoàn tác khi bấm ESC",
              L"app.exe:commit                 = Direct Commit, không chặn phím ESC",
              L"Mặc định: notepad++, explorer và filezilla tự hỗ trợ direct inline/commit."}
        : std::array<std::wstring, 4>{
              L"Enter one process and mode per line. Examples:",
              L"app.exe or app.exe:inline      = Direct Inline, reverted on ESC",
              L"app.exe:commit                 = Direct Commit, ESC is not eaten",
              L"Defaults: notepad++, explorer, and filezilla support direct modes automatically."};
    const int row_height = ScaleUi(hwnd, 19);
    int top = rect.top + ScaleUi(hwnd, 5);
    for (size_t i = 0; i < lines.size(); ++i) {
        RECT line_rect{
            rect.left + inset, top, rect.right - inset, top + row_height};
        DrawUiText(
            dc, lines[i], line_rect,
            i == 0 ? g_sectionFont : g_supportingFont,
            (i == 1 || i == 2) ? palette.accent : information_text,
            DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        top += row_height;
    }
}

void DrawDialogSurfaceMarkers(HWND hwnd, HDC dc) noexcept {
    for (const int control_id : {
             IDC_PANEL_METHOD,
             IDC_PANEL_OPTIONS,
             IDC_PANEL_UTILITIES,
             IDC_PANEL_APP_PROFILES,
             IDC_PANEL_HOTKEY,
             IDC_PANEL_STARTUP}) {
        RECT rect{};
        if (GetChildRectInParent(hwnd, control_id, rect)) {
            DrawRoundedSurface(hwnd, dc, rect);
        }
    }

    const bool vietnamese = GetWindowLongPtrW(hwnd, DWLP_USER) == 0;
    RECT rect{};
    if (GetChildRectInParent(
            hwnd, IDC_PANEL_SHORTHAND_VARIABLES, rect)) {
        DrawShorthandVariablesPanel(hwnd, dc, rect, vietnamese);
    }
    if (GetChildRectInParent(hwnd, IDC_PANEL_SHORTHAND_HELP, rect)) {
        DrawShorthandHelpPanel(hwnd, dc, rect, vietnamese);
    }
    if (GetChildRectInParent(hwnd, IDC_STATIC_DIRECT_DESC, rect)) {
        DrawDirectHelpPanel(hwnd, dc, rect, vietnamese);
    }
}

constexpr bool IsStableActionButtonId(int control_id) noexcept {
    return control_id == IDC_BUTTON_SHORTHAND_TABLE ||
        control_id == IDC_BUTTON_DIRECT_APPS ||
        control_id == IDC_BUTTON_APP_PROFILES ||
        control_id == IDC_BUTTON_FUZZY_INPUT_CONFIG;
}

void PaintAccentButton(
    HWND hwnd, HDC dc, const RECT& rect,
    bool checked, bool pressed, bool enabled, bool focused) noexcept {
    const UiPalette& palette = CurrentUiPalette();
    const int control_id = GetDlgCtrlID(hwnd);
    const COLORREF neutral_fill = g_uiHighContrast
        ? GetSysColor(COLOR_BTNFACE)
        : palette.input_surface;
    const COLORREF pressed_fill = g_uiHighContrast
        ? GetSysColor(COLOR_3DSHADOW)
        : (g_uiDarkMode ? palette.surface : palette.border);
    const COLORREF accent_fill = g_uiHighContrast
        ? GetSysColor(COLOR_HIGHLIGHT)
        : (pressed ? palette.accent_hover : palette.accent);
    const COLORREF fill = checked
        ? accent_fill
        : (pressed ? pressed_fill : neutral_fill);
    const COLORREF border = g_uiHighContrast
        ? GetSysColor(COLOR_WINDOWFRAME)
        : (checked ? palette.accent_hover : palette.border);

    FillRect(dc, &rect, CurrentSurfaceBrush());
    HBRUSH fill_brush = CreateSolidBrush(fill);
    HBRUSH border_brush = CreateSolidBrush(border);
    const int radius = ScaleUi(hwnd, 6);
    HRGN button_region = CreateRoundRectRgn(
        rect.left, rect.top, rect.right, rect.bottom, radius, radius);
    if (button_region) {
        FillRgn(dc, button_region, fill_brush);
        FrameRgn(dc, button_region, border_brush, 1, 1);
        DeleteObject(button_region);
    } else {
        FillRect(dc, &rect, fill_brush);
        FrameRect(dc, &rect, border_brush);
    }
    DeleteObject(border_brush);
    DeleteObject(fill_brush);

    wchar_t label[128]{};
    GetWindowTextW(hwnd, label, ARRAYSIZE(label));
    const bool method_button =
        control_id == IDC_RADIO_TELEX ||
        control_id == IDC_RADIO_SIMPLE_TELEX ||
        control_id == IDC_RADIO_VNI;
    HFONT label_font = method_button ? g_methodButtonFont : g_uiFont;
    HGDIOBJ old_font = label_font
        ? SelectObject(dc, label_font)
        : nullptr;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(
        dc,
        !enabled
            ? (g_uiHighContrast
                   ? GetSysColor(COLOR_GRAYTEXT)
                   : palette.disabled_text)
            : (checked
                   ? (g_uiHighContrast
                          ? GetSysColor(COLOR_HIGHLIGHTTEXT)
                          : RGB(255, 255, 255))
                   : (g_uiHighContrast
                          ? GetSysColor(COLOR_BTNTEXT)
                          : palette.text)));
    RECT text_rect = rect;
    DrawTextW(
        dc, label, -1, &text_rect,
        DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    if (focused) {
        RECT focus_rect = rect;
        InflateRect(&focus_rect, -ScaleUi(hwnd, 4), -ScaleUi(hwnd, 3));
        DrawFocusRect(dc, &focus_rect);
    }
    if (old_font) {
        SelectObject(dc, old_font);
    }
}

bool DrawStableActionButton(const DRAWITEMSTRUCT* draw_item) noexcept {
    if (!draw_item || draw_item->CtlType != ODT_BUTTON ||
        !IsStableActionButtonId(static_cast<int>(draw_item->CtlID))) {
        return false;
    }
    PaintAccentButton(
        draw_item->hwndItem, draw_item->hDC, draw_item->rcItem,
        false,
        (draw_item->itemState & ODS_SELECTED) != 0,
        (draw_item->itemState & ODS_DISABLED) == 0,
        (draw_item->itemState & ODS_FOCUS) != 0);
    return true;
}

void PaintStableActionButton(HWND hwnd, HDC dc) noexcept {
    RECT rect{};
    GetClientRect(hwnd, &rect);
    PaintAccentButton(
        hwnd, dc, rect, false,
        (SendMessageW(hwnd, BM_GETSTATE, 0, 0) & BST_PUSHED) != 0,
        IsWindowEnabled(hwnd) != FALSE,
        GetFocus() == hwnd);
}

LRESULT CALLBACK StableActionButtonProc(
    HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param,
    UINT_PTR subclass_id, DWORD_PTR) {
    switch (message) {
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(hwnd, &paint);
            PaintStableActionButton(hwnd, dc);
            EndPaint(hwnd, &paint);
            return 0;
        }
        case WM_PRINTCLIENT:
            PaintStableActionButton(
                hwnd, reinterpret_cast<HDC>(w_param));
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case BM_SETSTATE:
        case WM_ENABLE:
        case WM_SETFOCUS:
        case WM_KILLFOCUS:
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP: {
            const LRESULT result =
                DefSubclassProc(hwnd, message, w_param, l_param);
            RedrawWindow(
                hwnd, nullptr, nullptr,
                RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
            return result;
        }
        case WM_NCDESTROY:
            RemoveWindowSubclass(
                hwnd, StableActionButtonProc, subclass_id);
            break;
    }
    return DefSubclassProc(hwnd, message, w_param, l_param);
}

void InstallStableActionButtons(HWND hwnd) noexcept {
    for (const int control_id : {
             IDC_BUTTON_SHORTHAND_TABLE,
             IDC_BUTTON_DIRECT_APPS,
             IDC_BUTTON_APP_PROFILES,
             IDC_BUTTON_FUZZY_INPUT_CONFIG}) {
        if (HWND button = GetDlgItem(hwnd, control_id)) {
            // A themed Win32 paint pass can race the owner-draw transaction
            // during hover and temporarily replace the top border. Disable
            // theming for these controls and make their own paint handler the
            // single owner of every client repaint.
            SetWindowTheme(button, L"", L"");
            SetWindowSubclass(
                button, StableActionButtonProc,
                kStableActionButtonSubclassId, 0);
        }
    }
}

LRESULT CALLBACK AccentToggleButtonProc(
    HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param,
    UINT_PTR subclass_id, DWORD_PTR) {
    switch (message) {
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(hwnd, &paint);
            RECT rect{};
            GetClientRect(hwnd, &rect);
            PaintAccentButton(
                hwnd, dc, rect,
                SendMessageW(hwnd, BM_GETCHECK, 0, 0) == BST_CHECKED,
                (SendMessageW(hwnd, BM_GETSTATE, 0, 0) & BST_PUSHED) != 0,
                IsWindowEnabled(hwnd) != FALSE,
                GetFocus() == hwnd);
            EndPaint(hwnd, &paint);
            return 0;
        }
        case WM_PRINTCLIENT: {
            RECT rect{};
            GetClientRect(hwnd, &rect);
            PaintAccentButton(
                hwnd, reinterpret_cast<HDC>(w_param), rect,
                SendMessageW(hwnd, BM_GETCHECK, 0, 0) == BST_CHECKED,
                (SendMessageW(hwnd, BM_GETSTATE, 0, 0) & BST_PUSHED) != 0,
                IsWindowEnabled(hwnd) != FALSE,
                GetFocus() == hwnd);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case BM_SETCHECK: {
            const LRESULT result =
                DefSubclassProc(hwnd, message, w_param, l_param);
            InvalidateRect(hwnd, nullptr, TRUE);
            return result;
        }
        case WM_ENABLE:
        case WM_SETFOCUS:
        case WM_KILLFOCUS:
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP: {
            const LRESULT result =
                DefSubclassProc(hwnd, message, w_param, l_param);
            InvalidateRect(hwnd, nullptr, TRUE);
            return result;
        }
        case WM_NCDESTROY:
            RemoveWindowSubclass(hwnd, AccentToggleButtonProc, subclass_id);
            break;
    }
    return DefSubclassProc(hwnd, message, w_param, l_param);
}

void InstallAccentToggleButtons(HWND hwnd) noexcept {
    for (const int control_id : {
             IDC_RADIO_TELEX,
             IDC_RADIO_SIMPLE_TELEX,
             IDC_RADIO_VNI,
             IDC_RADIO_HOTKEY_CTRL_SHIFT,
             IDC_RADIO_HOTKEY_ALT_Z}) {
        if (HWND button = GetDlgItem(hwnd, control_id)) {
            SetWindowSubclass(
                button, AccentToggleButtonProc,
                kAccentToggleSubclassId, 0);
        }
    }
}

BOOL CALLBACK ApplyModernChildStyle(HWND child, LPARAM) {
    if (g_uiFont) {
        SendMessageW(
            child, WM_SETFONT, reinterpret_cast<WPARAM>(g_uiFont), TRUE);
    }

    wchar_t class_name[64]{};
    GetClassNameW(child, class_name, ARRAYSIZE(class_name));
    const bool is_button = _wcsicmp(class_name, L"Button") == 0;
    const bool is_combo_box = _wcsicmp(class_name, L"ComboBox") == 0;
    const bool is_rich_edit =
        _wcsicmp(class_name, MSFTEDIT_CLASS) == 0 ||
        _wcsicmp(class_name, L"RichEdit20W") == 0;
    const bool is_stable_action_button =
        is_button && IsStableActionButtonId(GetDlgCtrlID(child));
    const wchar_t* theme_name = g_uiDarkMode && !g_uiHighContrast
        ? (is_combo_box ? L"DarkMode_CFD" : L"DarkMode_Explorer")
        : L"Explorer";
    if (is_stable_action_button) {
        // Keep the custom painter as the sole owner after every theme refresh.
        // Re-enabling Explorer here reintroduced the one-pixel hover clipping.
        SetWindowTheme(child, L"", L"");
    } else if (is_button ||
        is_combo_box ||
        _wcsicmp(class_name, L"Edit") == 0 ||
        is_rich_edit ||
        _wcsicmp(class_name, WC_LISTVIEWW) == 0 ||
        _wcsicmp(class_name, L"Static") == 0) {
        SetWindowTheme(child, theme_name, nullptr);
    }

    if (_wcsicmp(class_name, WC_LISTVIEWW) == 0) {
        const UiPalette& palette = CurrentUiPalette();
        ListView_SetBkColor(child, palette.surface);
        ListView_SetTextBkColor(child, palette.surface);
        ListView_SetTextColor(child, palette.text);
    }
    return TRUE;
}

void ApplyDwmWindowStyle(HWND hwnd, bool main_window) noexcept {
    const BOOL dark_mode = g_uiDarkMode && !g_uiHighContrast;
    DwmSetWindowAttribute(
        hwnd, static_cast<DWMWINDOWATTRIBUTE>(kDwmUseImmersiveDarkMode),
        &dark_mode, sizeof(dark_mode));

    const DWORD corner_preference = kDwmCornerRound;
    DwmSetWindowAttribute(
        hwnd, static_cast<DWMWINDOWATTRIBUTE>(kDwmWindowCornerPreference),
        &corner_preference, sizeof(corner_preference));

    const bool transparency_enabled =
        ReadPersonalizeDword(L"EnableTransparency", 1) != 0;
    const DWORD backdrop = g_uiHighContrast || !transparency_enabled
        ? kDwmBackdropNone
        : (main_window
               ? kDwmBackdropMainWindow
               : kDwmBackdropTransientWindow);
    DwmSetWindowAttribute(
        hwnd, static_cast<DWMWINDOWATTRIBUTE>(kDwmSystemBackdropType),
        &backdrop, sizeof(backdrop));
}

void RefreshModernDialogStyle(HWND hwnd, bool main_window) noexcept {
    if (g_uiStyleRefreshInProgress) {
        return;
    }
    g_uiStyleRefreshInProgress = true;

    EnsureModernUiResources(hwnd);
    g_uiHighContrast = IsHighContrastEnabled();
    g_uiDarkMode = !g_uiHighContrast &&
        ReadPersonalizeDword(L"AppsUseLightTheme", 1) == 0;

    ApplyDwmWindowStyle(hwnd, main_window);
    SetWindowTheme(
        hwnd,
        g_uiDarkMode && !g_uiHighContrast
            ? L"DarkMode_Explorer"
            : L"Explorer",
        nullptr);
    EnumChildWindows(hwnd, ApplyModernChildStyle, 0);

    constexpr std::array<int, 10> kSectionIds{
        IDC_GROUP_METHOD,
        IDC_GROUP_OPTIONS,
        IDC_STATIC_CORRECTION_COLUMN,
        IDC_STATIC_PROTECTION_COLUMN,
        IDC_GROUP_UTILITIES,
        IDC_GROUP_APP_PROFILES,
        IDC_GROUP_HOTKEY,
        IDC_STATIC_STARTUP_SECTION,
        IDC_STATIC_SHORTHAND_DESC,
        IDC_STATIC_SHORTHAND_RULES_LABEL,
    };
    for (const int control_id : kSectionIds) {
        if (HWND control = GetDlgItem(hwnd, control_id); control && g_sectionFont) {
            SendMessageW(
                control, WM_SETFONT,
                reinterpret_cast<WPARAM>(g_sectionFont), TRUE);
        }
    }
    for (const int control_id : {
             IDC_STATIC_STARTUP_DESC,
             IDC_STATIC_VERSION,
             IDC_STATIC_SHORTHAND_HELP,
             IDC_STATIC_FUZZY_INPUT_STATUS,
             IDC_STATIC_FUZZY_INPUT_DESC}) {
        if (HWND control = GetDlgItem(hwnd, control_id);
            control && g_supportingFont) {
            SendMessageW(
                control, WM_SETFONT,
                reinterpret_cast<WPARAM>(g_supportingFont), TRUE);
        }
    }

    RedrawWindow(
        hwnd, nullptr, nullptr,
        RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);

    g_uiStyleRefreshInProgress = false;
}

void PaintModernDialogBackground(HWND hwnd, HDC dc, bool main_window) noexcept {
    RECT client_rect{};
    GetClientRect(hwnd, &client_rect);
    FillRect(dc, &client_rect, CurrentBackgroundBrush());

    DrawDialogSurfaceMarkers(hwnd, dc);

    if (!main_window) {
        return;
    }
    if (HWND separator = GetDlgItem(hwnd, IDC_STATIC_FOOTER_SEPARATOR)) {
        RECT footer_rect{};
        GetWindowRect(separator, &footer_rect);
        MapWindowPoints(nullptr, hwnd, reinterpret_cast<POINT*>(&footer_rect), 2);
        footer_rect.left = client_rect.left;
        footer_rect.right = client_rect.right;
        footer_rect.bottom = client_rect.bottom;
        FillRect(dc, &footer_rect, CurrentSurfaceBrush());
    }
}

bool IsSurfaceStaticControl(int control_id) noexcept {
    switch (control_id) {
        case IDC_GROUP_METHOD:
        case IDC_GROUP_OPTIONS:
        case IDC_STATIC_CORRECTION_COLUMN:
        case IDC_STATIC_PROTECTION_COLUMN:
        case IDC_STATIC_CORRECTION_LEVEL:
        case IDC_STATIC_ENGLISH_PROTECTION:
        case IDC_STATIC_FUZZY_INPUT_STATUS:
        case IDC_GROUP_UTILITIES:
        case IDC_STATIC_DIRECT_APPS:
        case IDC_GROUP_APP_PROFILES:
        case IDC_GROUP_HOTKEY:
        case IDC_STATIC_HOTKEY_MODE:
        case IDC_STATIC_STARTUP_SECTION:
        case IDC_STATIC_AUTO_START_MODE:
        case IDC_GROUP_LANGUAGE:
        case IDC_STATIC_STARTUP_DESC:
        case IDC_STATIC_FOOTER_SEPARATOR:
        case IDC_STATIC_VERSION:
            return true;
        default:
            return false;
    }
}

bool TryHandleModernDialogMessage(
    HWND hwnd,
    UINT message,
    WPARAM w_param,
    LPARAM l_param,
    bool main_window,
    INT_PTR& result) noexcept {
    EnsureModernUiResources(hwnd);
    switch (message) {
        case WM_DRAWITEM:
            if (DrawStableActionButton(
                    reinterpret_cast<const DRAWITEMSTRUCT*>(l_param))) {
                result = TRUE;
                return true;
            }
            return false;
        case WM_ERASEBKGND:
        case WM_PRINTCLIENT:
            PaintModernDialogBackground(
                hwnd, reinterpret_cast<HDC>(w_param), main_window);
            result = TRUE;
            return true;
        case WM_CTLCOLORDLG:
            result = reinterpret_cast<INT_PTR>(CurrentBackgroundBrush());
            return true;
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN:
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX: {
            HDC dc = reinterpret_cast<HDC>(w_param);
            const HWND control = reinterpret_cast<HWND>(l_param);
            const int control_id = control ? GetDlgCtrlID(control) : 0;
            const UiPalette& palette = CurrentUiPalette();
            COLORREF text_color = palette.text;
            if (control_id == IDC_STATIC_STARTUP_DESC ||
                control_id == IDC_STATIC_VERSION ||
                control_id == IDC_STATIC_SHORTHAND_HELP ||
                control_id == IDC_STATIC_FUZZY_INPUT_STATUS ||
                control_id == IDC_STATIC_FUZZY_INPUT_DESC) {
                text_color = palette.secondary_text;
            } else if (control_id == IDC_CHECK_ENABLE_LOG) {
                text_color = palette.warning_text;
            } else if (control && !IsWindowEnabled(control)) {
                text_color = palette.disabled_text;
            }
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(
                dc,
                g_uiHighContrast ? GetSysColor(COLOR_BTNTEXT) : text_color);
            SetBkColor(
                dc,
                g_uiHighContrast
                    ? GetSysColor(COLOR_WINDOW)
                    : palette.input_surface);
            if (!g_uiHighContrast && message == WM_CTLCOLORSTATIC) {
                result = reinterpret_cast<INT_PTR>(
                    IsSurfaceStaticControl(control_id)
                        ? CurrentSurfaceBrush()
                        : CurrentBackgroundBrush());
                return true;
            }
            if (!g_uiHighContrast && message == WM_CTLCOLORBTN) {
                result = reinterpret_cast<INT_PTR>(
                    GetStockObject(HOLLOW_BRUSH));
                return true;
            }
            const bool uses_input_surface =
                message == WM_CTLCOLOREDIT ||
                message == WM_CTLCOLORLISTBOX;
            const bool uses_surface = control_id == IDC_STATIC_VERSION;
            result = reinterpret_cast<INT_PTR>(
                uses_input_surface
                    ? CurrentInputBrush()
                    : (uses_surface
                           ? CurrentSurfaceBrush()
                           : CurrentBackgroundBrush()));
            return true;
        }
        case WM_SETTINGCHANGE:
        case WM_THEMECHANGED:
            RefreshModernDialogStyle(hwnd, main_window);
            if (GetDlgItem(hwnd, IDC_EDIT_SHORTHAND_RULES)) {
                PostMessageW(hwnd, kShorthandRecolorMessage, 0, 0);
            }
            result = TRUE;
            return true;
        default:
            return false;
    }
}

void ApplyShorthandEditorThemeAndSyntax(HWND hwnd) noexcept {
    HWND editor = GetDlgItem(hwnd, IDC_EDIT_SHORTHAND_RULES);
    if (!editor) {
        return;
    }

    const UiPalette& palette = CurrentUiPalette();
    SendMessageW(
        editor, EM_SETBKGNDCOLOR, 0,
        g_uiHighContrast ? GetSysColor(COLOR_WINDOW) : palette.input_surface);

    GETTEXTLENGTHEX length_options{};
    length_options.flags = GTL_NUMCHARS | GTL_PRECISE;
    length_options.codepage = 1200;
    const LRESULT text_length = SendMessageW(
        editor, EM_GETTEXTLENGTHEX,
        reinterpret_cast<WPARAM>(&length_options), 0);
    if (text_length < 0 || text_length > 1024 * 1024) {
        return;
    }

    std::wstring text(static_cast<size_t>(text_length) + 1, L'\0');
    GETTEXTEX text_options{};
    text_options.cb = static_cast<DWORD>(text.size() * sizeof(wchar_t));
    text_options.flags = GT_DEFAULT;
    text_options.codepage = 1200;
    const LRESULT copied = SendMessageW(
        editor, EM_GETTEXTEX,
        reinterpret_cast<WPARAM>(&text_options),
        reinterpret_cast<LPARAM>(text.data()));
    text.resize(static_cast<size_t>((std::max)(LRESULT{0}, copied)));

    CHARRANGE saved_selection{};
    SendMessageW(
        editor, EM_EXGETSEL, 0,
        reinterpret_cast<LPARAM>(&saved_selection));
    POINT saved_scroll{};
    SendMessageW(
        editor, EM_GETSCROLLPOS, 0,
        reinterpret_cast<LPARAM>(&saved_scroll));
    const DWORD event_mask = static_cast<DWORD>(
        SendMessageW(editor, EM_GETEVENTMASK, 0, 0));

    SendMessageW(editor, WM_SETREDRAW, FALSE, 0);
    SendMessageW(editor, EM_SETEVENTMASK, 0, 0);

    CHARFORMAT2W format{};
    format.cbSize = sizeof(format);
    format.dwMask = CFM_COLOR;
    format.crTextColor = g_uiHighContrast
        ? GetSysColor(COLOR_WINDOWTEXT)
        : palette.text;
    SendMessageW(
        editor, EM_SETCHARFORMAT, SCF_ALL,
        reinterpret_cast<LPARAM>(&format));

    if (!g_uiHighContrast) {
        format.crTextColor = palette.accent_hover;
        for (const ShorthandTemplateTagSpan& span :
             FindShorthandTemplateTagSpans(text)) {
            CHARRANGE range{
                static_cast<LONG>(span.start),
                static_cast<LONG>(span.start + span.length)};
            SendMessageW(
                editor, EM_EXSETSEL, 0,
                reinterpret_cast<LPARAM>(&range));
            SendMessageW(
                editor, EM_SETCHARFORMAT, SCF_SELECTION,
                reinterpret_cast<LPARAM>(&format));
        }
    }

    SendMessageW(
        editor, EM_EXSETSEL, 0,
        reinterpret_cast<LPARAM>(&saved_selection));
    SendMessageW(
        editor, EM_SETSCROLLPOS, 0,
        reinterpret_cast<LPARAM>(&saved_scroll));
    SendMessageW(editor, EM_SETEVENTMASK, 0, event_mask);
    SendMessageW(editor, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(editor, nullptr, TRUE);
}

} // namespace

std::wstring ReadShorthandFile(const std::wstring& filePath) {
    std::wstring content;
    ReadUtf8TextFile(filePath, content);
    return content;
}

bool WriteShorthandFile(const std::wstring& filePath, const std::wstring& content) {
    return WriteUtf8TextFileAtomic(filePath, content);
}

bool SaveConfigWithFeedback(HWND owner, const IMEConfig& config) {
    if (SaveConfigToRegistry(config, true)) {
        return true;
    }
    MessageBoxW(
        owner,
        L"Không thể lưu đầy đủ cấu hình Neokey. Hãy kiểm tra quyền ghi Registry và thử lại.",
        L"Neokey",
        MB_OK | MB_ICONERROR);
    return false;
}

std::wstring GetDlgItemTextString(HWND hwndDlg, int controlId) {
    HWND hwndEdit = GetDlgItem(hwndDlg, controlId);
    int len = GetWindowTextLengthW(hwndEdit);
    if (len <= 0) {
        return L"";
    }

    std::wstring text;
    text.resize(static_cast<size_t>(len) + 1);
    SendMessageW(hwndEdit, WM_GETTEXT, static_cast<WPARAM>(text.size()), reinterpret_cast<LPARAM>(&text[0]));
    text.resize(wcslen(text.c_str()));
    return text;
}

std::wstring GetConfigAppVersionText() {
    wchar_t modulePath[MAX_PATH] = {0};
    if (GetModuleFileNameW(nullptr, modulePath, MAX_PATH) == 0) {
        return L"Version: unknown";
    }

    std::wstring path(modulePath);
    size_t slash = path.find_last_of(L"\\/");
    std::wstring versionPath = (slash == std::wstring::npos ? L"" : path.substr(0, slash + 1)) + L"VERSION";

    std::wstring version;
    if (!ReadUtf8TextFile(versionPath, version)) {
        return L"Version: dev";
    }

    size_t first = version.find_first_not_of(L" \t\r\n");
    size_t last = version.find_last_not_of(L" \t\r\n");
    if (first == std::wstring::npos || last == std::wstring::npos) {
        return L"Version: unknown";
    }

    return L"Version: " + version.substr(first, last - first + 1);
}

void ShowCorrectionHelpDialog(HWND hwndDlg, int typingMode) {
    if (typingMode == 0) { // Vietnamese
        std::wstring text =
            L"BẢNG PHÂN CẤP TÍNH NĂNG TỰ ĐỘNG SỬA LỖI\n"
            L"=========================================\n\n"
            L"1. Tắt (Off):\n"
            L"   - Không tự động sửa bất kỳ từ nào.\n\n"
            L"2. Bình thường (Normal - Mặc định):\n"
            L"   - Tự đổi uo -> ươ / uô (dduocj -> được, muon -> muốn).\n"
            L"   - Thêm T cuối cho ie/uye (thuyes -> thuyết, vies -> viết).\n"
            L"   - Chuẩn hóa vị trí dấu thanh (hòa -> hoà).\n"
            L"   - Tự bù phím mũ/móc khi gõ nhầm dấu (kiẻm -> kiểm).\n\n"
            L"3. Nâng cao (Advanced):\n"
            L"   - Bao gồm toàn bộ tính năng mức Bình thường.\n"
            L"   - Đảo 2 phụ âm đầu (hcao -> chao, gnon -> ngon, hpong -> phong).\n"
            L"   - Đảo 2 ký tự cuối (đườgn -> đường, vern -> vẹn).\n"
            L"   - Gõ phím dấu trước nguyên âm (v6ạy -> vậy, vwatj -> vặt).\n"
            L"   - Tự bù phụ âm cuối bị thiếu (tuầ -> tuần).\n"
            L"   - Tự bù dấu thanh bị thiếu (thuyêt -> thuyết).\n\n"
            L"4. Thử nghiệm (Experimental):\n"
            L"   - Bao gồm toàn bộ tính năng mức Nâng cao.\n"
            L"   - Tự sửa từ gõ lộn xộn/sai 1-2 phím tổng quát (Damerau-Levenshtein).\n\n"
            L"GÕ SONG NGỮ VIỆT-ANH (độc lập):\n"
            L"   - Tắt: chỉ áp dụng quy tắc gõ tiếng Việt.\n"
            L"   - Cơ bản: dùng từ Anh phổ biến, ưu tiên chuỗi gõ Việt chuẩn.\n"
            L"   - Đa lĩnh vực: mở rộng toàn bộ từ vựng kỹ thuật, kinh tế, xã hội.\n\n"
            L"BẢO VỆ URL, EMAIL VÀ MÃ (độc lập):\n"
            L"   - Giữ nguyên URL, email và định danh mã rõ ràng.\n"
            L"   - Không thay các quy tắc VNI/Telex tiếng Việt chuẩn.\n\n"
            L"GÕ PHƯƠNG NGỮ (độc lập):\n"
            L"   - Chỉ áp dụng các nhóm phát âm người dùng tự chọn.\n"
            L"   - Kiểm tra từ hiện tại và bigram hai từ khi nhấn phím cách.\n"
            L"   - Nếu kết quả không duy nhất hoặc ngữ cảnh không khớp, Neokey giữ nguyên.\n\n"
            L"TỰ TÁCH TỪ KHI NHẤN PHÍM CÁCH:\n"
            L"   - Chỉ chạy ở mức Thử nghiệm và với cụm chắc chắn.";

        MessageBoxW(hwndDlg, text.c_str(), L"Thông tin Phân cấp Sửa lỗi - Neokey", MB_OK | MB_ICONINFORMATION);
    } else { // English
        std::wstring text =
            L"AUTO-CORRECTION FEATURE LEVELS\n"
            L"===============================\n\n"
            L"1. Off:\n"
            L"   - Disables all automatic corrections.\n\n"
            L"2. Normal (Default):\n"
            L"   - Converts uo -> ươ / uô (dduocj -> được, muon -> muốn).\n"
            L"   - Appends missing final T for ie/uye (thuyes -> thuyết, vies -> viết).\n"
            L"   - Standardizes tone mark placement (hòa -> hoà).\n"
            L"   - Fixes missing modifiers on accent typos (kiẻm -> kiểm).\n\n"
            L"3. Advanced:\n"
            L"   - Includes all Normal features.\n"
            L"   - Fixes swapped initial 2 consonants (hcao -> chao, gnon -> ngon, hpong -> phong).\n"
            L"   - Fixes swapped final 2 characters (đườgn -> đường, vern -> vẹn).\n"
            L"   - Allows tone/modifier before vowel (v6ạy -> vậy, vwatj -> vặt).\n"
            L"   - Auto-completes missing final consonant (tuầ -> tuần).\n"
            L"   - Auto-completes missing tone mark (thuyêt -> thuyết).\n\n"
            L"4. Experimental:\n"
            L"   - Includes all Advanced features.\n"
            L"   - General Damerau-Levenshtein typo correction (1-2 key distance).\n\n"
            L"VIETNAMESE-ENGLISH BILINGUAL TYPING (independent):\n"
            L"   - Off: applies Vietnamese typing rules only.\n"
            L"   - Basic: uses common English words but favors canonical Vietnamese input.\n"
            L"   - Multi-domain: uses the complete extended technical, economic, and social lexicon.\n\n"
            L"URL, EMAIL, AND CODE PROTECTION (independent):\n"
            L"   - Preserves clear URLs, email addresses, and code identifiers.\n"
            L"   - Keeps canonical Vietnamese VNI/Telex rules active.\n\n"
            L"FUZZY INPUT (independent):\n"
            L"   - Applies only the regional pronunciation groups selected by the user.\n"
            L"   - Checks the current token and an exact two-token bigram on Space.\n"
            L"   - Keeps the text unchanged when the result is ambiguous or context does not match.\n\n"
            L"SPLIT JOINED WORDS ON SPACE:\n"
            L"   - Runs only at Experimental for high-confidence phrases.";

        MessageBoxW(hwndDlg, text.c_str(), L"Auto-Correction Info - Neokey", MB_OK | MB_ICONINFORMATION);
    }
}

void PopulateMainModeCombos(HWND hwndDlg, int typingMode) {
    HWND auto_start_combo = GetDlgItem(hwndDlg, IDC_COMBO_AUTO_START);
    if (auto_start_combo) {
        LRESULT selected = SendMessageW(
            auto_start_combo, CB_GETCURSEL, 0, 0);
        if (selected == CB_ERR) {
            selected = 0;
        }
        SendMessageW(auto_start_combo, CB_RESETCONTENT, 0, 0);
        SendMessageW(
            auto_start_combo, CB_ADDSTRING, 0,
            reinterpret_cast<LPARAM>(typingMode == 0 ? L"Không" : L"No"));
        SendMessageW(
            auto_start_combo, CB_ADDSTRING, 0,
            reinterpret_cast<LPARAM>(typingMode == 0 ? L"Có" : L"Yes"));
        SendMessageW(
            auto_start_combo, CB_SETCURSEL,
            static_cast<WPARAM>(selected), 0);
    }

    HWND language_combo = GetDlgItem(hwndDlg, IDC_COMBO_LANGUAGE);
    if (language_combo) {
        SendMessageW(language_combo, CB_RESETCONTENT, 0, 0);
        SendMessageW(
            language_combo, CB_ADDSTRING, 0,
            reinterpret_cast<LPARAM>(L"Tiếng Việt"));
        SendMessageW(
            language_combo, CB_ADDSTRING, 0,
            reinterpret_cast<LPARAM>(L"English"));
        SendMessageW(
            language_combo, CB_SETCURSEL,
            static_cast<WPARAM>(typingMode == 0 ? 0 : 1), 0);
    }
}

bool IsMainDialogEnglish(HWND hwndDlg) noexcept {
    return SendDlgItemMessageW(
               hwndDlg, IDC_COMBO_LANGUAGE, CB_GETCURSEL, 0, 0) == 1;
}

MainFuzzyInputState* GetMainFuzzyInputState(HWND hwndDlg) noexcept {
    return reinterpret_cast<MainFuzzyInputState*>(
        GetWindowLongPtrW(hwndDlg, GWLP_USERDATA));
}

int CountSelectedFuzzyInputOptions(DWORD flags) noexcept {
    flags = NormalizeFuzzyInputFlags(flags);
    int count = 0;
    while (flags != 0) {
        count += static_cast<int>(flags & 1u);
        flags >>= 1;
    }
    return count;
}

void UpdateFuzzyInputStatus(HWND hwndDlg) {
    const MainFuzzyInputState* state = GetMainFuzzyInputState(hwndDlg);
    const DWORD flags = state ? NormalizeFuzzyInputFlags(state->pending_flags) : 0;
    const bool enabled =
        IsDlgButtonChecked(hwndDlg, IDC_CHECK_ENABLE_FUZZY_INPUT) ==
            BST_CHECKED &&
        flags != 0;
    std::wstring status;
    if (!enabled) {
        status = IsMainDialogEnglish(hwndDlg) ? L"Off" : L"Đang tắt";
    } else {
        const int count = CountSelectedFuzzyInputOptions(flags);
        status = IsMainDialogEnglish(hwndDlg)
            ? L"Selected: " + std::to_wstring(count)
            : L"Đã chọn: " + std::to_wstring(count);
    }
    SetDlgItemTextW(
        hwndDlg, IDC_STATIC_FUZZY_INPUT_STATUS, status.c_str());
}

void TranslateDialog(HWND hwndDlg, int typingMode) {
    if (typingMode == 0) { // Vietnamese
        SetWindowTextW(hwndDlg, L"Cấu hình Neokey");
        SetDlgItemTextW(hwndDlg, IDC_GROUP_METHOD, L"Kiểu gõ");
        SetDlgItemTextW(hwndDlg, IDC_RADIO_TELEX, L"Telex");
        SetDlgItemTextW(hwndDlg, IDC_RADIO_SIMPLE_TELEX, L"Simple Telex");
        SetDlgItemTextW(hwndDlg, IDC_RADIO_VNI, L"VNI");
        
        SetDlgItemTextW(hwndDlg, IDC_GROUP_OPTIONS, L"Sửa lỗi và gõ song ngữ");
        SetDlgItemTextW(hwndDlg, IDC_GROUP_UTILITIES, L"Tiện ích");
        SetDlgItemTextW(hwndDlg, IDC_STATIC_CORRECTION_COLUMN, L"Sửa lỗi");
        SetDlgItemTextW(hwndDlg, IDC_STATIC_PROTECTION_COLUMN, L"Gõ song ngữ Việt-Anh");
        SetDlgItemTextW(hwndDlg, IDC_STATIC_CORRECTION_LEVEL, L"Mức:");
        SetDlgItemTextW(hwndDlg, IDC_BUTTON_CORRECTION_HELP, L"?");
        SetDlgItemTextW(hwndDlg, IDC_STATIC_ENGLISH_PROTECTION, L"Chế độ:");
        
        HWND hwndCombo = GetDlgItem(hwndDlg, IDC_COMBO_CORRECTION_LEVEL);
        LRESULT curSel = SendMessageW(hwndCombo, CB_GETCURSEL, 0, 0);
        if (curSel == CB_ERR) curSel = 1;
        SendMessageW(hwndCombo, CB_RESETCONTENT, 0, 0);
        SendMessageW(hwndCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Tắt"));
        SendMessageW(hwndCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Bình thường"));
        SendMessageW(hwndCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Nâng cao"));
        SendMessageW(hwndCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Thử nghiệm"));
        SendMessageW(hwndCombo, CB_SETCURSEL, static_cast<WPARAM>(curSel), 0);

        HWND hwndEnglishCombo = GetDlgItem(hwndDlg, IDC_COMBO_ENGLISH_PROTECTION);
        LRESULT englishSel = SendMessageW(hwndEnglishCombo, CB_GETCURSEL, 0, 0);
        if (englishSel == CB_ERR) englishSel = 1;
        SendMessageW(hwndEnglishCombo, CB_RESETCONTENT, 0, 0);
        SendMessageW(hwndEnglishCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Tắt"));
        SendMessageW(hwndEnglishCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Cơ bản"));
        SendMessageW(hwndEnglishCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Đa lĩnh vực"));
        SendMessageW(hwndEnglishCombo, CB_SETCURSEL, static_cast<WPARAM>(englishSel), 0);
        
        SetDlgItemTextW(hwndDlg, IDC_CHECK_ENABLE_LOG, L"Bật file log để gỡ lỗi (Chỉ dùng khi debug)");
        SetDlgItemTextW(hwndDlg, IDC_CHECK_ENABLE_SHORTHAND, L"Bật tính năng gõ tắt");
        SetDlgItemTextW(hwndDlg, IDC_CHECK_SMART_CONTEXT_PROTECTION, L"Bảo vệ URL, email và mã");
        SetDlgItemTextW(hwndDlg, IDC_CHECK_ENABLE_FUZZY_INPUT, L"Bật gõ phương ngữ");
        SetDlgItemTextW(hwndDlg, IDC_BUTTON_FUZZY_INPUT_CONFIG, L"Cấu hình...");
        SetDlgItemTextW(hwndDlg, IDC_CHECK_SMART_UNDO, L"Backspace hoàn tác sửa/gõ tắt");
        SetDlgItemTextW(hwndDlg, IDC_CHECK_AUTO_WORD_SEGMENTATION, L"Tách từ dính (Thử nghiệm)");
        SetDlgItemTextW(hwndDlg, IDC_BUTTON_SHORTHAND_TABLE, L"Bảng gõ tắt...");
        SetDlgItemTextW(hwndDlg, IDC_CHECK_AUTO_CAPITALIZE, L"Tự viết hoa sau dấu chấm");
        SetDlgItemTextW(hwndDlg, IDC_GROUP_APP_PROFILES, L"Thiết lập theo ứng dụng");
        SetDlgItemTextW(hwndDlg, IDC_CHECK_ENABLE_APP_PROFILES, L"Dùng kiểu gõ riêng cho từng ứng dụng");
        SetDlgItemTextW(hwndDlg, IDC_CHECK_AUTO_APP_PROFILES, L"Tự nhớ kiểu gõ hoặc trạng thái tắt");
        SetDlgItemTextW(hwndDlg, IDC_BUTTON_APP_PROFILES, L"Ứng dụng...");
        SetDlgItemTextW(hwndDlg, IDC_STATIC_DIRECT_APPS, L"Chế độ Direct inline/commit");
        SetDlgItemTextW(hwndDlg, IDC_BUTTON_DIRECT_APPS, L"Cấu hình...");
        
        SetDlgItemTextW(hwndDlg, IDC_GROUP_HOTKEY, L"Phím tắt");
        SetDlgItemTextW(hwndDlg, IDC_STATIC_HOTKEY_MODE, L"Bật hoặc tắt Neokey");
        SetDlgItemTextW(hwndDlg, IDC_RADIO_HOTKEY_CTRL_SHIFT, L"Ctrl + Shift");
        SetDlgItemTextW(hwndDlg, IDC_RADIO_HOTKEY_ALT_Z, L"Alt + Z");
        
        SetDlgItemTextW(hwndDlg, IDC_GROUP_LANGUAGE, L"Ngôn ngữ:");
        SetDlgItemTextW(hwndDlg, IDC_STATIC_STARTUP_SECTION, L"Khởi động");
        SetDlgItemTextW(hwndDlg, IDC_STATIC_AUTO_START_MODE, L"Khởi động cùng Windows");
        SetDlgItemTextW(hwndDlg, IDC_STATIC_STARTUP_DESC, L"Chỉ khởi động ứng dụng cấu hình; bộ gõ hoạt động độc lập.");
        PopulateMainModeCombos(hwndDlg, typingMode);
        
        wchar_t verText[128];
        GetDlgItemTextW(hwndDlg, IDC_STATIC_VERSION, verText, 128);
        std::wstring sVer(verText);
        if (sVer.find(L"Version:") != std::wstring::npos) {
            sVer.replace(sVer.find(L"Version:"), 8, L"Phiên bản:");
            SetDlgItemTextW(hwndDlg, IDC_STATIC_VERSION, sVer.c_str());
        }
        
        SetDlgItemTextW(hwndDlg, IDOK, L"OK");
        SetDlgItemTextW(hwndDlg, IDCANCEL, L"Hủy bỏ");
        SetDlgItemTextW(hwndDlg, IDAPPLY, L"Áp dụng");
    } else { // English
        SetWindowTextW(hwndDlg, L"Neokey Configuration");
        SetDlgItemTextW(hwndDlg, IDC_GROUP_METHOD, L"Typing method");
        SetDlgItemTextW(hwndDlg, IDC_RADIO_TELEX, L"Telex");
        SetDlgItemTextW(hwndDlg, IDC_RADIO_SIMPLE_TELEX, L"Simple Telex");
        SetDlgItemTextW(hwndDlg, IDC_RADIO_VNI, L"VNI");
        
        SetDlgItemTextW(hwndDlg, IDC_GROUP_OPTIONS, L"Correction and bilingual typing");
        SetDlgItemTextW(hwndDlg, IDC_GROUP_UTILITIES, L"Utilities");
        SetDlgItemTextW(hwndDlg, IDC_STATIC_CORRECTION_COLUMN, L"Correction");
        SetDlgItemTextW(hwndDlg, IDC_STATIC_PROTECTION_COLUMN, L"Vietnamese-English typing");
        SetDlgItemTextW(hwndDlg, IDC_STATIC_CORRECTION_LEVEL, L"Level:");
        
        HWND hwndCombo = GetDlgItem(hwndDlg, IDC_COMBO_CORRECTION_LEVEL);
        LRESULT curSel = SendMessageW(hwndCombo, CB_GETCURSEL, 0, 0);
        if (curSel == CB_ERR) curSel = 1;
        SendMessageW(hwndCombo, CB_RESETCONTENT, 0, 0);
        SendMessageW(hwndCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Off"));
        SendMessageW(hwndCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Normal"));
        SendMessageW(hwndCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Advanced"));
        SendMessageW(hwndCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Experimental"));
        SendMessageW(hwndCombo, CB_SETCURSEL, static_cast<WPARAM>(curSel), 0);
        
        SetDlgItemTextW(hwndDlg, IDC_STATIC_ENGLISH_PROTECTION, L"Mode:");
        HWND hwndEnglishCombo = GetDlgItem(hwndDlg, IDC_COMBO_ENGLISH_PROTECTION);
        LRESULT englishSel = SendMessageW(hwndEnglishCombo, CB_GETCURSEL, 0, 0);
        if (englishSel == CB_ERR) englishSel = 1;
        SendMessageW(hwndEnglishCombo, CB_RESETCONTENT, 0, 0);
        SendMessageW(hwndEnglishCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Off"));
        SendMessageW(hwndEnglishCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Basic"));
        SendMessageW(hwndEnglishCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Multi-domain"));
        SendMessageW(hwndEnglishCombo, CB_SETCURSEL, static_cast<WPARAM>(englishSel), 0);
        SetDlgItemTextW(hwndDlg, IDC_CHECK_ENABLE_LOG, L"Enable debug logging (Use for debugging only)");
        SetDlgItemTextW(hwndDlg, IDC_CHECK_ENABLE_SHORTHAND, L"Enable shorthand");
        SetDlgItemTextW(hwndDlg, IDC_CHECK_SMART_CONTEXT_PROTECTION, L"Protect URL, email, and code");
        SetDlgItemTextW(hwndDlg, IDC_CHECK_ENABLE_FUZZY_INPUT, L"Enable fuzzy input");
        SetDlgItemTextW(hwndDlg, IDC_BUTTON_FUZZY_INPUT_CONFIG, L"Configure...");
        SetDlgItemTextW(hwndDlg, IDC_CHECK_SMART_UNDO, L"Backspace undoes correction/shorthand");
        SetDlgItemTextW(hwndDlg, IDC_CHECK_AUTO_WORD_SEGMENTATION, L"Split joined words (Experimental)");
        SetDlgItemTextW(hwndDlg, IDC_BUTTON_SHORTHAND_TABLE, L"Shorthand table...");
        SetDlgItemTextW(hwndDlg, IDC_CHECK_AUTO_CAPITALIZE, L"Auto-capitalize after period");
        SetDlgItemTextW(hwndDlg, IDC_GROUP_APP_PROFILES, L"Per-app typing modes");
        SetDlgItemTextW(hwndDlg, IDC_CHECK_ENABLE_APP_PROFILES, L"Use per-app typing settings");
        SetDlgItemTextW(hwndDlg, IDC_CHECK_AUTO_APP_PROFILES, L"Automatically remember typing mode/off per app");
        SetDlgItemTextW(hwndDlg, IDC_BUTTON_APP_PROFILES, L"Configure apps...");
        SetDlgItemTextW(hwndDlg, IDC_STATIC_DIRECT_APPS, L"Direct inline/commit modes");
        SetDlgItemTextW(hwndDlg, IDC_BUTTON_DIRECT_APPS, L"Configure...");
        
        SetDlgItemTextW(hwndDlg, IDC_GROUP_HOTKEY, L"Hotkey");
        SetDlgItemTextW(hwndDlg, IDC_STATIC_HOTKEY_MODE, L"Turn Neokey on/off");
        SetDlgItemTextW(hwndDlg, IDC_RADIO_HOTKEY_CTRL_SHIFT, L"Ctrl + Shift");
        SetDlgItemTextW(hwndDlg, IDC_RADIO_HOTKEY_ALT_Z, L"Alt + Z");
        
        SetDlgItemTextW(hwndDlg, IDC_GROUP_LANGUAGE, L"Language:");
        SetDlgItemTextW(hwndDlg, IDC_STATIC_STARTUP_SECTION, L"Startup");
        SetDlgItemTextW(hwndDlg, IDC_STATIC_AUTO_START_MODE, L"Start with Windows");
        SetDlgItemTextW(hwndDlg, IDC_STATIC_STARTUP_DESC, L"Starts only the configuration app; the IME runs independently.");
        PopulateMainModeCombos(hwndDlg, typingMode);
        
        wchar_t verText[128];
        GetDlgItemTextW(hwndDlg, IDC_STATIC_VERSION, verText, 128);
        std::wstring sVer(verText);
        if (sVer.find(L"Phiên bản:") != std::wstring::npos) {
            sVer.replace(sVer.find(L"Phiên bản:"), 10, L"Version:");
            SetDlgItemTextW(hwndDlg, IDC_STATIC_VERSION, sVer.c_str());
        }
        
        SetDlgItemTextW(hwndDlg, IDOK, L"OK");
        SetDlgItemTextW(hwndDlg, IDCANCEL, L"Cancel");
        SetDlgItemTextW(hwndDlg, IDAPPLY, L"Apply");
    }
    UpdateFuzzyInputStatus(hwndDlg);
}

IMEConfig ReadConfigFromDialog(HWND hwndDlg) {
    IMEConfig config = LoadConfigFromRegistry();
    if (IsDlgButtonChecked(hwndDlg, IDC_RADIO_TELEX) == BST_CHECKED) {
        config.input_method = core::InputMethod::Telex;
    } else if (IsDlgButtonChecked(hwndDlg, IDC_RADIO_SIMPLE_TELEX) == BST_CHECKED) {
        config.input_method = core::InputMethod::SimpleTelex;
    } else if (IsDlgButtonChecked(hwndDlg, IDC_RADIO_VNI) == BST_CHECKED) {
        config.input_method = core::InputMethod::VNI;
    }
    LRESULT index = SendDlgItemMessageW(hwndDlg, IDC_COMBO_CORRECTION_LEVEL, CB_GETCURSEL, 0, 0);
    if (index == CB_ERR) {
        config.auto_correct_level = CorrectionLevel::Normal;
    } else {
        config.auto_correct_level = NormalizeCorrectionLevelValue(static_cast<DWORD>(index));
    }
    config.enable_auto_correct = (config.auto_correct_level != CorrectionLevel::Off);
    LRESULT englishIndex = SendDlgItemMessageW(
        hwndDlg, IDC_COMBO_ENGLISH_PROTECTION, CB_GETCURSEL, 0, 0);
    config.english_protection_level = englishIndex == CB_ERR
        ? EnglishProtectionLevel::Balanced
        : NormalizeEnglishProtectionLevelValue(static_cast<DWORD>(englishIndex));
    config.enable_log = (IsDlgButtonChecked(hwndDlg, IDC_CHECK_ENABLE_LOG) == BST_CHECKED);
    config.enable_shorthand = (IsDlgButtonChecked(hwndDlg, IDC_CHECK_ENABLE_SHORTHAND) == BST_CHECKED);
    config.enable_smart_undo =
        IsDlgButtonChecked(hwndDlg, IDC_CHECK_SMART_UNDO) == BST_CHECKED;
    config.enable_smart_context_protection =
        IsDlgButtonChecked(
            hwndDlg, IDC_CHECK_SMART_CONTEXT_PROTECTION) == BST_CHECKED;
    if (const MainFuzzyInputState* fuzzy_state =
            GetMainFuzzyInputState(hwndDlg)) {
        config.fuzzy_input_flags =
            NormalizeFuzzyInputFlags(fuzzy_state->pending_flags);
    }
    config.enable_fuzzy_input = IsFuzzyInputEffectivelyEnabled(
        IsDlgButtonChecked(hwndDlg, IDC_CHECK_ENABLE_FUZZY_INPUT) ==
            BST_CHECKED,
        config.fuzzy_input_flags);
    config.enable_auto_word_segmentation =
        NormalizeAutoWordSegmentationEnabled(
            IsDlgButtonChecked(
                hwndDlg, IDC_CHECK_AUTO_WORD_SEGMENTATION) == BST_CHECKED,
            config.auto_correct_level);
    config.enable_auto_capitalize = (IsDlgButtonChecked(hwndDlg, IDC_CHECK_AUTO_CAPITALIZE) == BST_CHECKED);
    config.enable_app_input_profiles =
        IsDlgButtonChecked(hwndDlg, IDC_CHECK_ENABLE_APP_PROFILES) ==
        BST_CHECKED;
    config.enable_auto_app_input_profiles =
        IsDlgButtonChecked(hwndDlg, IDC_CHECK_AUTO_APP_PROFILES) ==
        BST_CHECKED;
    config.enable_app_blocklist = config.enable_app_input_profiles;
    config.enable_auto_exclude = config.enable_auto_app_input_profiles;
    config.enable_auto_start = SendDlgItemMessageW(
        hwndDlg, IDC_COMBO_AUTO_START, CB_GETCURSEL, 0, 0) == 1;
    
    if (IsDlgButtonChecked(hwndDlg, IDC_RADIO_HOTKEY_CTRL_SHIFT) == BST_CHECKED) {
        config.hotkey_mode = 0;
    } else if (IsDlgButtonChecked(hwndDlg, IDC_RADIO_HOTKEY_ALT_Z) == BST_CHECKED) {
        config.hotkey_mode = 1;
    }

    config.typing_mode = IsMainDialogEnglish(hwndDlg) ? 1 : 0;
    SyncLegacyAppProfileViews(config);
    
    return config;
}

INT_PTR CALLBACK ShorthandDialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    INT_PTR modern_result = FALSE;
    if (TryHandleModernDialogMessage(
            hwndDlg, uMsg, wParam, lParam, false, modern_result)) {
        return modern_result;
    }
    switch (uMsg) {
        case WM_INITDIALOG: {
            HideSurfaceLayoutMarkers(hwndDlg);
            // Set text limit of the multiline edit to 16MB
            SendDlgItemMessage(hwndDlg, IDC_EDIT_SHORTHAND_RULES, EM_SETLIMITTEXT, 16 * 1024 * 1024, 0);

            // Load shorthand rules
            std::wstring filePath = GetShorthandFilePath(nullptr);
            std::wstring content = ReadShorthandFile(filePath);
            SetDlgItemTextW(hwndDlg, IDC_EDIT_SHORTHAND_RULES, content.c_str());

            // Translate dialog UI based on config.typing_mode
            IMEConfig config = LoadConfigFromRegistry();
            SetWindowLongPtrW(hwndDlg, DWLP_USER, config.typing_mode);
            if (config.typing_mode == 0) { // VIE
                SetWindowTextW(hwndDlg, L"Bảng Từ Gõ Tắt");
                SetDlgItemTextW(hwndDlg, IDC_STATIC_SHORTHAND_DESC, L"Hướng dẫn sử dụng");
                SetDlgItemTextW(
                    hwndDlg, IDC_STATIC_SHORTHAND_HELP,
                    L"1. Mỗi dòng là một quy tắc: phím=nội dung. Gõ phím rồi nhấn Space để bung.\r\n"
                    L"   Ví dụ: dc=Được rồi.  (Gõ dc rồi nhấn Space)\r\n"
                    L"2. Ngày/giờ: {{DATE}}, {{DD/MM/YYYY}}, {{TIME}}, {{WEEKDAY}}; mã ngẫu nhiên: {{UUID}}.\r\n"
                    L"3. Clipboard: {{CLIPBOARD}}; thêm |TRIM, |UPPER hoặc |LOWER để biến đổi.\r\n"
                    L"4. SELECTION lấy phần đang bôi đen. Phải bôi đen rồi gõ mã ngay; chuyển caret sẽ hủy vùng chọn.\r\n"
                    L"   Ví dụ: wrap=[{{SELECTION}}]{{CURSOR}}\r\n"
                    L"5. CURSOR là vị trí caret sau khi bung; mỗi quy tắc chỉ dùng tối đa một {{CURSOR}}.\r\n"
                    L"6. {{NEWLINE}} tạo dòng mới; {{TAB}} tạo tab. Tên biến phải viết HOA đúng như trên.\r\n"
                    L"Dòng trống hoặc dòng bắt đầu bằng # hay ; được xem là ghi chú.");
                SetDlgItemTextW(hwndDlg, IDC_STATIC_SHORTHAND_RULES_LABEL, L"Quy tắc của bạn");
                SetDlgItemTextW(hwndDlg, IDC_BUTTON_IMPORT, L"Nhập file...");
                SetDlgItemTextW(hwndDlg, IDC_BUTTON_EXPORT, L"Xuất file...");
                SetDlgItemTextW(hwndDlg, IDOK, L"Lưu");
                SetDlgItemTextW(hwndDlg, IDCANCEL, L"Hủy bỏ");
            } else { // ENG
                SetWindowTextW(hwndDlg, L"Shorthand Rules");
                SetDlgItemTextW(hwndDlg, IDC_STATIC_SHORTHAND_DESC, L"How to use shorthand");
                SetDlgItemTextW(
                    hwndDlg, IDC_STATIC_SHORTHAND_HELP,
                    L"1. One rule per line: key=text. Type the key, then press Space to expand.\r\n"
                    L"   Example: ok=All done.  (Type ok, then press Space)\r\n"
                    L"2. Date/time: {{DATE}}, {{DD/MM/YYYY}}, {{TIME}}, {{WEEKDAY}}; random ID: {{UUID}}.\r\n"
                    L"3. Clipboard: {{CLIPBOARD}}; add |TRIM, |UPPER or |LOWER to transform it.\r\n"
                    L"4. SELECTION uses the currently selected text. Type the key immediately; moving the caret cancels it.\r\n"
                    L"   Example: wrap=[{{SELECTION}}]{{CURSOR}}\r\n"
                    L"5. CURSOR marks the caret after expansion; use at most one {{CURSOR}} in each rule.\r\n"
                    L"6. {{NEWLINE}} inserts a new line; {{TAB}} inserts a tab. Variable names are uppercase.\r\n"
                    L"Blank lines and lines beginning with # or ; are comments.");
                SetDlgItemTextW(hwndDlg, IDC_STATIC_SHORTHAND_RULES_LABEL, L"Your rules");
                SetDlgItemTextW(hwndDlg, IDC_BUTTON_IMPORT, L"Import...");
                SetDlgItemTextW(hwndDlg, IDC_BUTTON_EXPORT, L"Export...");
                SetDlgItemTextW(hwndDlg, IDOK, L"Save");
                SetDlgItemTextW(hwndDlg, IDCANCEL, L"Cancel");
            }
            RefreshModernDialogStyle(hwndDlg, false);
            ApplyShorthandEditorThemeAndSyntax(hwndDlg);
            HWND rules_edit = GetDlgItem(hwndDlg, IDC_EDIT_SHORTHAND_RULES);
            if (rules_edit) {
                SetFocus(rules_edit);
                SendMessageW(rules_edit, EM_SETSEL, 0, 0);
                return FALSE;
            }
            return TRUE;
        }
        case WM_COMMAND: {
            WORD controlId = LOWORD(wParam);
            if (controlId == IDC_EDIT_SHORTHAND_RULES &&
                HIWORD(wParam) == EN_CHANGE) {
                SetTimer(hwndDlg, kShorthandSyntaxTimerId, 90, nullptr);
                return TRUE;
            } else if (controlId == IDOK) {
                std::wstring content = GetDlgItemTextString(hwndDlg, IDC_EDIT_SHORTHAND_RULES);
                const ShorthandParseResult parsed =
                    ParseShorthandRules(content);
                if (parsed.limit_exceeded_lines != 0) {
                    MessageBoxW(
                        hwndDlg,
                        L"Bảng gõ tắt vượt giới hạn an toàn (tối đa 4096 quy tắc, khóa 128 ký tự, nội dung 16384 ký tự). Hãy rút gọn rồi lưu lại.",
                        L"Neokey",
                        MB_OK | MB_ICONERROR);
                    return TRUE;
                }

                // Save rules
                std::wstring filePath = GetShorthandFilePath(nullptr);
                if (WriteShorthandFile(filePath, content)) {
                    TouchConfigRevision();
                } else {
                    MessageBoxW(
                        hwndDlg,
                        L"Không thể lưu bảng gõ tắt. Hãy kiểm tra quyền ghi và dung lượng ổ đĩa.",
                        L"Neokey",
                        MB_OK | MB_ICONERROR);
                    return TRUE;
                }

                EndDialog(hwndDlg, IDOK);
                return TRUE;
            } else if (controlId == IDCANCEL) {
                EndDialog(hwndDlg, IDCANCEL);
                return TRUE;
            } else if (controlId == IDC_BUTTON_IMPORT) {
                WCHAR szFile[MAX_PATH] = { 0 };
                OPENFILENAMEW ofn = { 0 };
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = hwndDlg;
                ofn.lpstrFilter = L"Text Files (*.txt)\0*.txt\0All Files (*.*)\0*.*\0";
                ofn.lpstrFile = szFile;
                ofn.nMaxFile = MAX_PATH;
                ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
                if (GetOpenFileNameW(&ofn)) {
                    std::wstring content = ReadShorthandFile(szFile);
                    SetDlgItemTextW(hwndDlg, IDC_EDIT_SHORTHAND_RULES, content.c_str());
                }
                return TRUE;
            } else if (controlId == IDC_BUTTON_EXPORT) {
                WCHAR szFile[MAX_PATH] = { 0 };
                OPENFILENAMEW ofn = { 0 };
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = hwndDlg;
                ofn.lpstrFilter = L"Text Files (*.txt)\0*.txt\0All Files (*.*)\0*.*\0";
                ofn.lpstrFile = szFile;
                ofn.nMaxFile = MAX_PATH;
                ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY;
                if (GetSaveFileNameW(&ofn)) {
                    // Force text extension if not present
                    std::wstring exportPath = szFile;
                    if (exportPath.find(L'.') == std::wstring::npos) {
                        exportPath += L".txt";
                    }
                    std::wstring content = GetDlgItemTextString(hwndDlg, IDC_EDIT_SHORTHAND_RULES);
                    if (!WriteShorthandFile(exportPath, content)) {
                        MessageBoxW(
                            hwndDlg,
                            L"Không thể xuất bảng gõ tắt tới tệp đã chọn.",
                            L"Neokey",
                            MB_OK | MB_ICONERROR);
                    }
                }
                return TRUE;
            }
            break;
        }
        case WM_TIMER:
            if (wParam == kShorthandSyntaxTimerId) {
                KillTimer(hwndDlg, kShorthandSyntaxTimerId);
                ApplyShorthandEditorThemeAndSyntax(hwndDlg);
                return TRUE;
            }
            break;
        case kShorthandRecolorMessage:
            ApplyShorthandEditorThemeAndSyntax(hwndDlg);
            return TRUE;
        case WM_CLOSE: {
            KillTimer(hwndDlg, kShorthandSyntaxTimerId);
            EndDialog(hwndDlg, IDCANCEL);
            return TRUE;
        }
        case WM_NCDESTROY:
            KillTimer(hwndDlg, kShorthandSyntaxTimerId);
            break;
    }
    return FALSE;
}

struct AppProfilesDialogState {
    std::vector<AppInputProfile> profiles;
    core::InputMethod global_method = core::InputMethod::VNI;
    bool vietnamese = true;
};

struct AppProfilesDialogInit {
    core::InputMethod global_method = core::InputMethod::VNI;
    bool vietnamese = true;
};

AppProfilesDialogState* GetAppProfilesDialogState(HWND hwndDlg) {
    return reinterpret_cast<AppProfilesDialogState*>(
        GetWindowLongPtrW(hwndDlg, DWLP_USER));
}

const wchar_t* GetAppInputModeLabel(AppInputMode mode, bool vietnamese) {
    switch (mode) {
        case AppInputMode::Telex:
            return L"Telex";
        case AppInputMode::SimpleTelex:
            return L"Simple Telex";
        case AppInputMode::VNI:
            return L"VNI";
        case AppInputMode::Off:
        default:
            return vietnamese ? L"Tắt" : L"Off";
    }
}

int AppInputModeToComboIndex(AppInputMode mode) {
    switch (mode) {
        case AppInputMode::Telex:
            return 0;
        case AppInputMode::SimpleTelex:
            return 1;
        case AppInputMode::VNI:
            return 2;
        case AppInputMode::Off:
            return 3;
        default:
            return 2;
    }
}

std::optional<AppInputMode> GetAppInputModeFromCombo(HWND hwndDlg) {
    const LRESULT selected = SendDlgItemMessageW(
        hwndDlg, IDC_COMBO_APP_PROFILE_MODE, CB_GETCURSEL, 0, 0);
    switch (selected) {
        case 0:
            return AppInputMode::Telex;
        case 1:
            return AppInputMode::SimpleTelex;
        case 2:
            return AppInputMode::VNI;
        case 3:
            return AppInputMode::Off;
        default:
            return std::nullopt;
    }
}

std::wstring GetSelectedAppProfileProcess(HWND hwndDlg) {
    HWND list = GetDlgItem(hwndDlg, IDC_LIST_APP_PROFILES);
    const int selected = ListView_GetNextItem(list, -1, LVNI_SELECTED);
    if (selected < 0) {
        return {};
    }
    wchar_t process_name[MAX_APP_INPUT_PROFILE_PROCESS_NAME_CHARS + 1] = {};
    LVITEMW item = {};
    item.iSubItem = 0;
    item.pszText = process_name;
    item.cchTextMax = static_cast<int>(
        MAX_APP_INPUT_PROFILE_PROCESS_NAME_CHARS + 1);
    SendMessageW(
        list, LVM_GETITEMTEXTW, static_cast<WPARAM>(selected),
        reinterpret_cast<LPARAM>(&item));
    return NormalizeProcessName(process_name);
}

void UpdateAppProfilesDialogSelection(HWND hwndDlg) {
    AppProfilesDialogState* state = GetAppProfilesDialogState(hwndDlg);
    if (!state) {
        return;
    }
    const std::wstring process_name = GetSelectedAppProfileProcess(hwndDlg);
    const auto profile = LookupAppInputProfile(state->profiles, process_name);
    EnableWindow(
        GetDlgItem(hwndDlg, IDC_BUTTON_REMOVE_APP_PROFILE),
        profile.has_value());
    if (profile.has_value()) {
        SendDlgItemMessageW(
            hwndDlg, IDC_COMBO_APP_PROFILE_MODE, CB_SETCURSEL,
            AppInputModeToComboIndex(AppInputModeForProfile(*profile)), 0);
    }
}

void RefreshAppProfilesList(
    HWND hwndDlg,
    std::wstring_view process_to_select = {}) {
    AppProfilesDialogState* state = GetAppProfilesDialogState(hwndDlg);
    if (!state) {
        return;
    }
    state->profiles = NormalizeAppInputProfiles(state->profiles);
    const std::wstring selected_name = NormalizeProcessName(
        std::wstring(process_to_select));
    HWND list = GetDlgItem(hwndDlg, IDC_LIST_APP_PROFILES);
    ListView_DeleteAllItems(list);

    int selected_row = -1;
    for (size_t i = 0; i < state->profiles.size(); ++i) {
        const AppInputProfile& profile = state->profiles[i];
        LVITEMW item = {};
        item.mask = LVIF_TEXT;
        item.iItem = static_cast<int>(i);
        item.pszText = const_cast<wchar_t*>(profile.process_name.c_str());
        const int row = static_cast<int>(SendMessageW(
            list, LVM_INSERTITEMW, 0, reinterpret_cast<LPARAM>(&item)));
        if (row < 0) {
            continue;
        }
        const wchar_t* mode = GetAppInputModeLabel(
            AppInputModeForProfile(profile), state->vietnamese);
        LVITEMW mode_item = {};
        mode_item.iSubItem = 1;
        mode_item.pszText = const_cast<wchar_t*>(mode);
        SendMessageW(
            list, LVM_SETITEMTEXTW, static_cast<WPARAM>(row),
            reinterpret_cast<LPARAM>(&mode_item));
        if (!selected_name.empty() && profile.process_name == selected_name) {
            selected_row = row;
        }
    }

    if (selected_row >= 0) {
        ListView_SetItemState(
            list, selected_row, LVIS_SELECTED | LVIS_FOCUSED,
            LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(list, selected_row, FALSE);
    }
    UpdateAppProfilesDialogSelection(hwndDlg);
}

void TranslateAppProfilesDialog(HWND hwndDlg) {
    AppProfilesDialogState* state = GetAppProfilesDialogState(hwndDlg);
    if (!state) {
        return;
    }
    if (state->vietnamese) {
        SetWindowTextW(hwndDlg, L"Thiết lập theo ứng dụng");
        SetDlgItemTextW(
            hwndDlg, IDC_STATIC_APP_PROFILES_DESC,
            L"Chọn kiểu gõ cho từng ứng dụng. Xóa một dòng để kế thừa thiết lập chung.");
        SetDlgItemTextW(hwndDlg, IDC_STATIC_APP_PROFILE_MODE, L"Kiểu gõ:");
        SetDlgItemTextW(hwndDlg, IDC_BUTTON_ADD_CURRENT_APP, L"Thêm hiện tại");
        SetDlgItemTextW(hwndDlg, IDC_BUTTON_BROWSE_APP, L"Chọn tệp...");
        SetDlgItemTextW(hwndDlg, IDC_BUTTON_REMOVE_APP_PROFILE, L"Xóa");
        SetDlgItemTextW(hwndDlg, IDCANCEL, L"Hủy");
    } else {
        SetWindowTextW(hwndDlg, L"Per-app Typing Modes");
        SetDlgItemTextW(
            hwndDlg, IDC_STATIC_APP_PROFILES_DESC,
            L"Choose a typing mode for each app. Remove a row to inherit global settings.");
        SetDlgItemTextW(hwndDlg, IDC_STATIC_APP_PROFILE_MODE, L"Mode:");
        SetDlgItemTextW(hwndDlg, IDC_BUTTON_ADD_CURRENT_APP, L"Add current");
        SetDlgItemTextW(hwndDlg, IDC_BUTTON_BROWSE_APP, L"Browse...");
        SetDlgItemTextW(hwndDlg, IDC_BUTTON_REMOVE_APP_PROFILE, L"Remove");
        SetDlgItemTextW(hwndDlg, IDCANCEL, L"Cancel");
    }
    SetDlgItemTextW(hwndDlg, IDOK, L"OK");

    const int previous_mode = static_cast<int>(SendDlgItemMessageW(
        hwndDlg, IDC_COMBO_APP_PROFILE_MODE, CB_GETCURSEL, 0, 0));
    SendDlgItemMessageW(
        hwndDlg, IDC_COMBO_APP_PROFILE_MODE, CB_RESETCONTENT, 0, 0);
    for (const AppInputMode mode : {
             AppInputMode::Telex, AppInputMode::SimpleTelex,
             AppInputMode::VNI, AppInputMode::Off}) {
        SendDlgItemMessageW(
            hwndDlg, IDC_COMBO_APP_PROFILE_MODE, CB_ADDSTRING, 0,
            reinterpret_cast<LPARAM>(
                GetAppInputModeLabel(mode, state->vietnamese)));
    }
    const int default_mode = AppInputModeToComboIndex(
        AppInputModeForMethod(state->global_method));
    SendDlgItemMessageW(
        hwndDlg, IDC_COMBO_APP_PROFILE_MODE, CB_SETCURSEL,
        previous_mode >= 0 && previous_mode <= 3
            ? previous_mode
            : default_mode,
        0);
}

void InitializeAppProfilesList(HWND hwndDlg) {
    AppProfilesDialogState* state = GetAppProfilesDialogState(hwndDlg);
    HWND list = GetDlgItem(hwndDlg, IDC_LIST_APP_PROFILES);
    ListView_SetExtendedListViewStyle(
        list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    RECT rect = {};
    GetClientRect(list, &rect);
    const int total_width = rect.right - rect.left;
    const int mode_width = total_width * 35 / 100;

    LVCOLUMNW column = {};
    column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    column.cx = total_width - mode_width - 4;
    column.pszText = const_cast<wchar_t*>(
        state && state->vietnamese ? L"Ứng dụng" : L"App");
    SendMessageW(
        list, LVM_INSERTCOLUMNW, 0,
        reinterpret_cast<LPARAM>(&column));
    column.iSubItem = 1;
    column.cx = mode_width;
    column.pszText = const_cast<wchar_t*>(
        state && state->vietnamese ? L"Kiểu gõ" : L"Mode");
    SendMessageW(
        list, LVM_INSERTCOLUMNW, 1,
        reinterpret_cast<LPARAM>(&column));
}

void ShowInvalidAppProfileMessage(HWND hwndDlg) {
    AppProfilesDialogState* state = GetAppProfilesDialogState(hwndDlg);
    const bool vietnamese = state && state->vietnamese;
    MessageBoxW(
        hwndDlg,
        vietnamese
            ? L"Không thể thêm ứng dụng này. Hãy chọn một tệp .exe hợp lệ không thuộc tiến trình hệ thống của Windows hoặc Neokey."
            : L"This app cannot be added. Choose a valid .exe that is not a Windows or Neokey system process.",
        vietnamese ? L"Ứng dụng không hợp lệ" : L"Invalid app",
        MB_OK | MB_ICONWARNING);
}

bool AddOrUpdateManualAppProfile(
    HWND hwndDlg,
    std::wstring_view process_name) {
    AppProfilesDialogState* state = GetAppProfilesDialogState(hwndDlg);
    const auto mode = GetAppInputModeFromCombo(hwndDlg);
    const std::wstring normalized = NormalizeProcessName(
        std::wstring(process_name));
    if (!state || !mode.has_value() ||
        !IsConfigurableAppProcessName(normalized)) {
        return false;
    }
    UpsertManualAppInputMode(
        state->profiles, normalized, *mode, state->global_method);
    if (!LookupAppInputProfile(state->profiles, normalized).has_value()) {
        return false;
    }
    RefreshAppProfilesList(hwndDlg, normalized);
    return true;
}

INT_PTR CALLBACK AppProfilesDialogProc(
    HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    INT_PTR modern_result = FALSE;
    if (TryHandleModernDialogMessage(
            hwndDlg, uMsg, wParam, lParam, false, modern_result)) {
        return modern_result;
    }
    switch (uMsg) {
        case WM_INITDIALOG: {
            const IMEConfig config = LoadConfigFromRegistry();
            const auto* init = reinterpret_cast<const AppProfilesDialogInit*>(
                lParam);
            core::InputMethod global_method = config.input_method;
            bool vietnamese = config.typing_mode == 0;
            if (init) {
                global_method = init->global_method;
                vietnamese = init->vietnamese;
            }
            auto* state = new (std::nothrow) AppProfilesDialogState{
                NormalizeAppInputProfiles(config.app_input_profiles),
                global_method,
                vietnamese};
            if (!state) {
                EndDialog(hwndDlg, IDCANCEL);
                return TRUE;
            }
            SetWindowLongPtrW(
                hwndDlg, DWLP_USER, reinterpret_cast<LONG_PTR>(state));
            TranslateAppProfilesDialog(hwndDlg);
            InitializeAppProfilesList(hwndDlg);
            RefreshAppProfilesList(hwndDlg);
            RefreshModernDialogStyle(hwndDlg, false);
            return TRUE;
        }
        case WM_NOTIFY: {
            const auto* header = reinterpret_cast<const NMHDR*>(lParam);
            if (header && header->idFrom == IDC_LIST_APP_PROFILES &&
                header->code == LVN_ITEMCHANGED) {
                UpdateAppProfilesDialogSelection(hwndDlg);
            }
            break;
        }
        case WM_COMMAND: {
            const WORD control_id = LOWORD(wParam);
            const WORD notification = HIWORD(wParam);
            AppProfilesDialogState* state = GetAppProfilesDialogState(hwndDlg);
            if (control_id == IDOK && state) {
                IMEConfig config = LoadConfigFromRegistry();
                config.app_input_profiles = NormalizeAppInputProfiles(
                    state->profiles);
                SyncLegacyAppProfileViews(config);
                if (!SaveConfigWithFeedback(hwndDlg, config)) {
                    return TRUE;
                }
                EndDialog(hwndDlg, IDOK);
                return TRUE;
            }
            if (control_id == IDCANCEL) {
                EndDialog(hwndDlg, IDCANCEL);
                return TRUE;
            }
            if (control_id == IDC_BUTTON_ADD_CURRENT_APP) {
                if (!AddOrUpdateManualAppProfile(
                        hwndDlg, g_lastActiveProcessName)) {
                    ShowInvalidAppProfileMessage(hwndDlg);
                }
                return TRUE;
            }
            if (control_id == IDC_BUTTON_BROWSE_APP) {
                wchar_t file_path[MAX_PATH] = {};
                const wchar_t filter_vi[] =
                    L"Ứng dụng (*.exe)\0*.exe\0";
                const wchar_t filter_en[] =
                    L"Applications (*.exe)\0*.exe\0";
                OPENFILENAMEW ofn = {};
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = hwndDlg;
                ofn.lpstrFile = file_path;
                ofn.nMaxFile = MAX_PATH;
                ofn.lpstrFilter = state && state->vietnamese
                    ? filter_vi
                    : filter_en;
                ofn.nFilterIndex = 1;
                ofn.lpstrDefExt = L"exe";
                ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST |
                    OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
                if (GetOpenFileNameW(&ofn) &&
                    !AddOrUpdateManualAppProfile(hwndDlg, file_path)) {
                    ShowInvalidAppProfileMessage(hwndDlg);
                }
                return TRUE;
            }
            if (control_id == IDC_BUTTON_REMOVE_APP_PROFILE && state) {
                const std::wstring process_name =
                    GetSelectedAppProfileProcess(hwndDlg);
                if (RemoveAppInputProfile(
                        state->profiles, process_name)) {
                    RefreshAppProfilesList(hwndDlg);
                }
                return TRUE;
            }
            if (control_id == IDC_COMBO_APP_PROFILE_MODE &&
                notification == CBN_SELCHANGE && state) {
                const std::wstring process_name =
                    GetSelectedAppProfileProcess(hwndDlg);
                const auto mode = GetAppInputModeFromCombo(hwndDlg);
                if (!process_name.empty() && mode.has_value()) {
                    UpsertManualAppInputMode(
                        state->profiles, process_name, *mode,
                        state->global_method);
                    RefreshAppProfilesList(hwndDlg, process_name);
                }
                return TRUE;
            }
            break;
        }
        case WM_CLOSE:
            EndDialog(hwndDlg, IDCANCEL);
            return TRUE;
        case WM_NCDESTROY: {
            AppProfilesDialogState* state = GetAppProfilesDialogState(hwndDlg);
            SetWindowLongPtrW(hwndDlg, DWLP_USER, 0);
            delete state;
            break;
        }
    }
    return FALSE;
}

INT_PTR CALLBACK DirectAppsDialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    INT_PTR modern_result = FALSE;
    if (TryHandleModernDialogMessage(
            hwndDlg, uMsg, wParam, lParam, false, modern_result)) {
        return modern_result;
    }
    switch (uMsg) {
        case WM_INITDIALOG: {
            HideSurfaceLayoutMarkers(hwndDlg);
            SendDlgItemMessage(hwndDlg, IDC_EDIT_DIRECT_APPS, EM_SETLIMITTEXT, 1024 * 1024, 0);
            IMEConfig config = LoadConfigFromRegistry();
            SetWindowLongPtrW(hwndDlg, DWLP_USER, config.typing_mode);
            config.direct_apps = NormalizeDirectAppsList(config.direct_apps);
            std::wstring text = ProcessListToText(config.direct_apps);
            SetDlgItemTextW(hwndDlg, IDC_EDIT_DIRECT_APPS, text.c_str());

            // Translate dialog UI based on config.typing_mode
            if (config.typing_mode == 0) { // VIE
                SetWindowTextW(hwndDlg, L"Ứng dụng Direct Inline/Commit");
                SetDlgItemTextW(hwndDlg, IDC_STATIC_DIRECT_DESC, L"Mỗi dòng nhập một tiến trình kèm chế độ. Ví dụ:\n- app.exe hoặc app.exe:inline (Direct Inline, hoàn tác khi bấm ESC)\n- app.exe:commit (Direct Commit, không chặn phím ESC)\nCác app mặc định (notepad++, explorer, filezilla) tự động hỗ trợ direct inline/commit.");
                SetDlgItemTextW(hwndDlg, IDOK, L"OK");
                SetDlgItemTextW(hwndDlg, IDCANCEL, L"Hủy bỏ");
            } else { // ENG
                SetWindowTextW(hwndDlg, L"Direct Inline/Commit Applications");
                SetDlgItemTextW(hwndDlg, IDC_STATIC_DIRECT_DESC, L"One process name with mode per line. Example:\n- app.exe or app.exe:inline (Direct Inline, reverted on ESC)\n- app.exe:commit (Direct Commit, ESC is not eaten)\nDefault apps (notepad++, explorer, filezilla) are supported automatically.");
                SetDlgItemTextW(hwndDlg, IDOK, L"OK");
                SetDlgItemTextW(hwndDlg, IDCANCEL, L"Cancel");
            }
            RefreshModernDialogStyle(hwndDlg, false);
            return TRUE;
        }
        case WM_COMMAND: {
            WORD controlId = LOWORD(wParam);
            if (controlId == IDOK) {
                IMEConfig config = LoadConfigFromRegistry();
                std::wstring text = GetDlgItemTextString(hwndDlg, IDC_EDIT_DIRECT_APPS);
                config.direct_apps = ParseDirectAppsListText(text);

                if (!SaveConfigWithFeedback(hwndDlg, config)) {
                    return TRUE;
                }
                EndDialog(hwndDlg, IDOK);
                return TRUE;
            } else if (controlId == IDCANCEL) {
                EndDialog(hwndDlg, IDCANCEL);
                return TRUE;
            }
            break;
        }
        case WM_CLOSE: {
            EndDialog(hwndDlg, IDCANCEL);
            return TRUE;
        }
    }
    return FALSE;
}

#define WM_TRAYICON_MSG             (WM_USER + 100)
#define WM_USER_SHOW_SETTINGS       (WM_USER + 101)
#define WM_USER_CONFIG_CHANGED      (WM_USER + 102)

inline constexpr UINT_PTR kForegroundPollTimerId = 1;
inline constexpr UINT_PTR kTraySingleClickTimerId = 2;

HWND g_hwndTray = nullptr;
HWND g_hwndDlg = nullptr;
bool g_isDialogActive = false;
HICON g_hIconV = nullptr;
HICON g_hIconE = nullptr;
HICON g_hDlgIconBig = nullptr;
HICON g_hDlgIconSmall = nullptr;
std::wstring g_lastActiveProcessName;
HWND g_lastForegroundHwnd = nullptr;
TrayClickState g_trayClickState;

HANDLE g_registryWatchThread = nullptr;
HANDLE g_registryWatchShutdownEvent = nullptr;
HANDLE g_registryWatchEvent = nullptr;

ResolvedAppInputProfile ResolveTrayAppInputProfile(
    const IMEConfig& config) {
    return ResolveEffectiveAppInputProfile(
        config.enable_app_input_profiles,
        config.app_input_profiles,
        g_lastActiveProcessName,
        config.typing_mode == 0,
        config.input_method);
}

bool ApplyTrayInputMode(AppInputMode mode) {
    IMEConfig config = LoadConfigFromRegistry();
    const AppInputUpdateResult result = ApplyUserSelectedInputMode(
        config, g_lastActiveProcessName, mode);
    if (!result.changed) {
        return false;
    }
    return SaveConfigWithFeedback(g_hwndTray, config);
}

bool ToggleTrayInputMode() {
    IMEConfig config = LoadConfigFromRegistry();
    const AppInputUpdateResult result = ToggleUserInputMode(
        config, g_lastActiveProcessName);
    if (!result.changed) {
        return false;
    }
    return SaveConfigWithFeedback(g_hwndTray, config);
}

std::wstring GetForegroundProcessName(HWND hwnd) {
    if (!hwnd) {
        return L"";
    }

    DWORD process_id = 0;
    GetWindowThreadProcessId(hwnd, &process_id);
    if (process_id == 0) {
        return L"";
    }

    std::wstring process_name;
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id);
    if (hProcess) {
        std::wstring path(32768, L'\0');
        DWORD size = static_cast<DWORD>(path.size());
        if (QueryFullProcessImageNameW(hProcess, 0, &path[0], &size) && size > 0) {
            path.resize(size);
            // Extract file name
            size_t pos = path.find_last_of(L"\\/");
            if (pos != std::wstring::npos) {
                process_name = path.substr(pos + 1);
            } else {
                process_name = path;
            }
            // Lowercase
            for (wchar_t& c : process_name) {
                c = towlower(c);
            }
        }
        CloseHandle(hProcess);
    }
    return process_name;
}

int GetSystemMetricsForDpiCompat(int index, UINT dpi) noexcept {
    using GetSystemMetricsForDpiFn = int(WINAPI*)(int, UINT);
    static const auto get_system_metrics_for_dpi = []() noexcept {
        const HMODULE user32 = GetModuleHandleW(L"user32.dll");
        return user32
            ? reinterpret_cast<GetSystemMetricsForDpiFn>(
                  GetProcAddress(user32, "GetSystemMetricsForDpi"))
            : nullptr;
    }();
    if (get_system_metrics_for_dpi && dpi != 0) {
        return get_system_metrics_for_dpi(index, dpi);
    }

    const int metric = GetSystemMetrics(index);
    return dpi == 0 ? metric : MulDiv(metric, static_cast<int>(dpi), 96);
}

HICON LoadIconResourceForSize(UINT resource_id, int size) noexcept {
    if (size <= 0) {
        return nullptr;
    }
    return reinterpret_cast<HICON>(LoadImageW(
        GetModuleHandleW(nullptr), MAKEINTRESOURCEW(resource_id), IMAGE_ICON,
        size, size, LR_DEFAULTCOLOR));
}

void NotifyShellExecutableIconChanged() noexcept {
    std::wstring executable_path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(
        nullptr, executable_path.data(),
        static_cast<DWORD>(executable_path.size()));
    if (length == 0 || length >= executable_path.size()) {
        return;
    }
    executable_path.resize(length);
    SHChangeNotify(
        SHCNE_UPDATEITEM, SHCNF_PATHW | SHCNF_FLUSH,
        executable_path.c_str(), nullptr);
}

void UpdateDialogIcon(HWND hwndDlg, UINT dpi = 0) {
    if (!hwndDlg) return;

    if (dpi == 0) {
        dpi = GetWindowDpiCompat(hwndDlg);
    }
    int sizeBig = GetSystemMetricsForDpiCompat(SM_CXICON, dpi);
    if (sizeBig <= 0) sizeBig = 32;
    int sizeSmall = GetSystemMetricsForDpiCompat(SM_CXSMICON, dpi);
    if (sizeSmall <= 0) sizeSmall = 16;

    if (HICON icon = LoadIconResourceForSize(IDI_APP_ICON, sizeBig)) {
        SendMessageW(
            hwndDlg, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icon));
        if (g_hDlgIconBig) {
            DestroyIcon(g_hDlgIconBig);
        }
        g_hDlgIconBig = icon;
    }
    if (HICON icon = LoadIconResourceForSize(IDI_APP_ICON, sizeSmall)) {
        SendMessageW(
            hwndDlg, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon));
        if (g_hDlgIconSmall) {
            DestroyIcon(g_hDlgIconSmall);
        }
        g_hDlgIconSmall = icon;
    }
}

void UpdateTrayIcon(HWND hwnd) {
    IMEConfig config = LoadConfigFromRegistry();
    const ResolvedAppInputProfile effective = ResolveTrayAppInputProfile(config);
    HICON hIcon = effective.enabled ? g_hIconV : g_hIconE;

    NOTIFYICONDATAW nid = { 0 };
    nid.cbSize = sizeof(NOTIFYICONDATAW);
    nid.hWnd = hwnd;
    nid.uID = IDI_TRAY_ICON;
    nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    nid.uCallbackMessage = WM_TRAYICON_MSG;
    nid.hIcon = hIcon;
    wcscpy_s(nid.szTip, L"Neokey");

    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

DWORD WINAPI TrayRegistryWatchThreadProc(LPVOID lpParam) {
    HWND hwnd = reinterpret_cast<HWND>(lpParam);
    HKEY hKey = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, REG_KEY_PATH, 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_READ, nullptr, &hKey, nullptr) != ERROR_SUCCESS) {
        return 0;
    }

    HANDLE waitHandles[2] = { g_registryWatchShutdownEvent, g_registryWatchEvent };

    while (true) {
        LONG status = RegNotifyChangeKeyValue(hKey, FALSE, REG_NOTIFY_CHANGE_LAST_SET, g_registryWatchEvent, TRUE);
        if (status != ERROR_SUCCESS) {
            break;
        }

        DWORD waitResult = WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE);
        if (waitResult == WAIT_OBJECT_0) {
            break;
        } else if (waitResult == WAIT_OBJECT_0 + 1) {
            PostMessageW(hwnd, WM_USER_CONFIG_CHANGED, 0, 0);
        } else {
            break;
        }
    }

    if (hKey) {
        RegCloseKey(hKey);
    }
    return 0;
}

constexpr std::array<std::pair<int, DWORD>, 5> kFuzzyInputOptionControls{{
    {IDC_CHECK_FUZZY_L_N, FUZZY_INPUT_FLAG_L_N},
    {IDC_CHECK_FUZZY_TR_CH, FUZZY_INPUT_FLAG_TR_CH},
    {IDC_CHECK_FUZZY_S_X, FUZZY_INPUT_FLAG_S_X},
    {IDC_CHECK_FUZZY_R_D_GI, FUZZY_INPUT_FLAG_R_D_GI},
    {IDC_CHECK_FUZZY_HOI_NGA, FUZZY_INPUT_FLAG_HOI_NGA},
}};

void SetFuzzyInputOptionChecks(HWND hwndDlg, DWORD flags) {
    flags = NormalizeFuzzyInputFlags(flags);
    for (const auto& [control_id, flag] : kFuzzyInputOptionControls) {
        CheckDlgButton(
            hwndDlg, control_id,
            (flags & flag) != 0 ? BST_CHECKED : BST_UNCHECKED);
    }
}

DWORD ReadFuzzyInputOptionChecks(HWND hwndDlg) noexcept {
    DWORD flags = 0;
    for (const auto& [control_id, flag] : kFuzzyInputOptionControls) {
        if (IsDlgButtonChecked(hwndDlg, control_id) == BST_CHECKED) {
            flags |= flag;
        }
    }
    return NormalizeFuzzyInputFlags(flags);
}

void TranslateFuzzyInputDialog(HWND hwndDlg, bool english) {
    if (english) {
        SetWindowTextW(hwndDlg, L"Configure Fuzzy Input");
        SetDlgItemTextW(
            hwndDlg, IDC_STATIC_FUZZY_INPUT_DESC,
            L"Choose the regional pronunciation differences you commonly type.");
        SetDlgItemTextW(hwndDlg, IDC_CHECK_FUZZY_L_N, L"Mix up L and N");
        SetDlgItemTextW(hwndDlg, IDC_CHECK_FUZZY_TR_CH, L"Mix up Tr and Ch");
        SetDlgItemTextW(hwndDlg, IDC_CHECK_FUZZY_S_X, L"Mix up S and X");
        SetDlgItemTextW(hwndDlg, IDC_CHECK_FUZZY_R_D_GI, L"Mix up R, D, and Gi");
        SetDlgItemTextW(hwndDlg, IDC_CHECK_FUZZY_HOI_NGA, L"Mix up Hỏi and Ngã tones");
        SetDlgItemTextW(hwndDlg, IDC_BUTTON_FUZZY_CLEAR_ALL, L"Clear all");
        SetDlgItemTextW(hwndDlg, IDOK, L"OK");
        SetDlgItemTextW(hwndDlg, IDCANCEL, L"Cancel");
    } else {
        SetWindowTextW(hwndDlg, L"Cấu hình Gõ Phương Ngữ");
        SetDlgItemTextW(
            hwndDlg, IDC_STATIC_FUZZY_INPUT_DESC,
            L"Chọn những lỗi phát âm vùng miền bạn thường gõ lẫn.");
        SetDlgItemTextW(hwndDlg, IDC_CHECK_FUZZY_L_N, L"Lẫn lộn L và N");
        SetDlgItemTextW(hwndDlg, IDC_CHECK_FUZZY_TR_CH, L"Lẫn lộn Tr và Ch");
        SetDlgItemTextW(hwndDlg, IDC_CHECK_FUZZY_S_X, L"Lẫn lộn S và X");
        SetDlgItemTextW(hwndDlg, IDC_CHECK_FUZZY_R_D_GI, L"Lẫn lộn R, D và Gi");
        SetDlgItemTextW(hwndDlg, IDC_CHECK_FUZZY_HOI_NGA, L"Lẫn lộn dấu Hỏi và Ngã");
        SetDlgItemTextW(hwndDlg, IDC_BUTTON_FUZZY_CLEAR_ALL, L"Tắt tất cả");
        SetDlgItemTextW(hwndDlg, IDOK, L"OK");
        SetDlgItemTextW(hwndDlg, IDCANCEL, L"Hủy bỏ");
    }
}

INT_PTR CALLBACK FuzzyInputDialogProc(
    HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    INT_PTR modern_result = FALSE;
    if (TryHandleModernDialogMessage(
            hwndDlg, uMsg, wParam, lParam, false, modern_result)) {
        return modern_result;
    }
    switch (uMsg) {
        case WM_INITDIALOG: {
            auto* state = reinterpret_cast<FuzzyInputDialogState*>(lParam);
            if (!state) {
                EndDialog(hwndDlg, IDCANCEL);
                return TRUE;
            }
            state->flags = NormalizeFuzzyInputFlags(state->flags);
            SetWindowLongPtrW(
                hwndDlg, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
            SetFuzzyInputOptionChecks(hwndDlg, state->flags);
            TranslateFuzzyInputDialog(hwndDlg, state->english);
            RefreshModernDialogStyle(hwndDlg, false);
            return TRUE;
        }
        case WM_COMMAND: {
            const WORD control_id = LOWORD(wParam);
            if (control_id == IDC_BUTTON_FUZZY_CLEAR_ALL &&
                HIWORD(wParam) == BN_CLICKED) {
                SetFuzzyInputOptionChecks(hwndDlg, 0);
                return TRUE;
            }
            if (control_id == IDOK) {
                if (auto* state = reinterpret_cast<FuzzyInputDialogState*>(
                        GetWindowLongPtrW(hwndDlg, GWLP_USERDATA))) {
                    state->flags = ReadFuzzyInputOptionChecks(hwndDlg);
                }
                EndDialog(hwndDlg, IDOK);
                return TRUE;
            }
            if (control_id == IDCANCEL) {
                EndDialog(hwndDlg, IDCANCEL);
                return TRUE;
            }
            break;
        }
        case WM_CLOSE:
            EndDialog(hwndDlg, IDCANCEL);
            return TRUE;
        case WM_NCDESTROY:
            SetWindowLongPtrW(hwndDlg, GWLP_USERDATA, 0);
            break;
    }
    return FALSE;
}

bool ConfigurePendingFuzzyInput(HWND hwndDlg) {
    MainFuzzyInputState* state = GetMainFuzzyInputState(hwndDlg);
    if (!state) {
        return false;
    }
    FuzzyInputDialogState dialog_state{
        NormalizeFuzzyInputFlags(state->pending_flags),
        IsMainDialogEnglish(hwndDlg)};
    const INT_PTR result = DialogBoxParamW(
        GetModuleHandleW(nullptr),
        MAKEINTRESOURCEW(IDD_FUZZY_INPUT_DIALOG), hwndDlg,
        FuzzyInputDialogProc,
        reinterpret_cast<LPARAM>(&dialog_state));
    if (result != IDOK) {
        return false;
    }
    state->pending_flags = NormalizeFuzzyInputFlags(dialog_state.flags);
    if (state->pending_flags == 0) {
        CheckDlgButton(
            hwndDlg, IDC_CHECK_ENABLE_FUZZY_INPUT, BST_UNCHECKED);
    }
    UpdateFuzzyInputStatus(hwndDlg);
    return true;
}

INT_PTR CALLBACK DialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    INT_PTR modern_result = FALSE;
    if (TryHandleModernDialogMessage(
            hwndDlg, uMsg, wParam, lParam, true, modern_result)) {
        return modern_result;
    }
    switch (uMsg) {
        case WM_INITDIALOG: {
            g_hwndDlg = hwndDlg;
            HideSurfaceLayoutMarkers(hwndDlg);
            InstallAccentToggleButtons(hwndDlg);
            InstallStableActionButtons(hwndDlg);

            // Load current config
            IMEConfig config = LoadConfigFromRegistry();
            auto* fuzzy_state = new (std::nothrow) MainFuzzyInputState{
                NormalizeFuzzyInputFlags(config.fuzzy_input_flags)};
            SetWindowLongPtrW(
                hwndDlg, GWLP_USERDATA,
                reinterpret_cast<LONG_PTR>(fuzzy_state));

            // Set layout selection
            if (config.input_method == core::InputMethod::Telex) {
                CheckRadioButton(hwndDlg, IDC_RADIO_TELEX, IDC_RADIO_VNI, IDC_RADIO_TELEX);
            } else if (config.input_method == core::InputMethod::SimpleTelex) {
                CheckRadioButton(hwndDlg, IDC_RADIO_TELEX, IDC_RADIO_VNI, IDC_RADIO_SIMPLE_TELEX);
            } else if (config.input_method == core::InputMethod::VNI) {
                CheckRadioButton(hwndDlg, IDC_RADIO_TELEX, IDC_RADIO_VNI, IDC_RADIO_VNI);
            }

            // Set checks
            CheckDlgButton(hwndDlg, IDC_CHECK_ENABLE_LOG, config.enable_log ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(hwndDlg, IDC_CHECK_ENABLE_SHORTHAND, config.enable_shorthand ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(
                hwndDlg, IDC_CHECK_SMART_CONTEXT_PROTECTION,
                config.enable_smart_context_protection
                    ? BST_CHECKED
                    : BST_UNCHECKED);
            CheckDlgButton(
                hwndDlg, IDC_CHECK_ENABLE_FUZZY_INPUT,
                IsFuzzyInputEffectivelyEnabled(
                    config.enable_fuzzy_input, config.fuzzy_input_flags)
                    ? BST_CHECKED
                    : BST_UNCHECKED);
            if (!fuzzy_state) {
                EnableWindow(
                    GetDlgItem(hwndDlg, IDC_CHECK_ENABLE_FUZZY_INPUT), FALSE);
                EnableWindow(
                    GetDlgItem(hwndDlg, IDC_BUTTON_FUZZY_INPUT_CONFIG), FALSE);
            }
            CheckDlgButton(hwndDlg, IDC_CHECK_SMART_UNDO, config.enable_smart_undo ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(
                hwndDlg, IDC_CHECK_AUTO_WORD_SEGMENTATION,
                config.enable_auto_word_segmentation
                    ? BST_CHECKED
                    : BST_UNCHECKED);
            CheckDlgButton(hwndDlg, IDC_CHECK_AUTO_CAPITALIZE, config.enable_auto_capitalize ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(
                hwndDlg, IDC_CHECK_ENABLE_APP_PROFILES,
                config.enable_app_input_profiles
                    ? BST_CHECKED
                    : BST_UNCHECKED);
            CheckDlgButton(
                hwndDlg, IDC_CHECK_AUTO_APP_PROFILES,
                config.enable_auto_app_input_profiles
                    ? BST_CHECKED
                    : BST_UNCHECKED);
            // Set hotkey checks
            if (config.hotkey_mode == 0) {
                CheckRadioButton(hwndDlg, IDC_RADIO_HOTKEY_CTRL_SHIFT, IDC_RADIO_HOTKEY_ALT_Z, IDC_RADIO_HOTKEY_CTRL_SHIFT);
            } else {
                CheckRadioButton(hwndDlg, IDC_RADIO_HOTKEY_CTRL_SHIFT, IDC_RADIO_HOTKEY_ALT_Z, IDC_RADIO_HOTKEY_ALT_Z);
            }

            UpdateDialogIcon(hwndDlg);

            // Translate dialog UI based on loaded typing_mode
            TranslateDialog(hwndDlg, config.typing_mode);
            HWND hwndCombo = GetDlgItem(hwndDlg, IDC_COMBO_CORRECTION_LEVEL);
            SendMessageW(hwndCombo, CB_SETCURSEL, static_cast<WPARAM>(CorrectionLevelToConfigIndex(config.auto_correct_level)), 0);
            HWND hwndEnglishCombo = GetDlgItem(hwndDlg, IDC_COMBO_ENGLISH_PROTECTION);
            SendMessageW(hwndEnglishCombo, CB_SETCURSEL, static_cast<WPARAM>(EnglishProtectionLevelToConfigIndex(config.english_protection_level)), 0);
            SendDlgItemMessageW(
                hwndDlg, IDC_COMBO_AUTO_START, CB_SETCURSEL,
                config.enable_auto_start ? 1 : 0, 0);
            SendDlgItemMessageW(
                hwndDlg, IDC_COMBO_LANGUAGE, CB_SETCURSEL,
                config.typing_mode == 0 ? 0 : 1, 0);

            std::wstring versionText = GetConfigAppVersionText();
            if (config.typing_mode == 0 &&
                versionText.starts_with(L"Version:")) {
                versionText.replace(0, 8, L"Phiên bản:");
            }
            SetDlgItemTextW(hwndDlg, IDC_STATIC_VERSION, versionText.c_str());
            RefreshModernDialogStyle(hwndDlg, true);
            FitMainDialogToWorkArea(hwndDlg);
            return TRUE;
        }
        case WM_DPICHANGED:
            UpdateDialogIcon(hwndDlg, HIWORD(wParam));
            return FALSE;
        case WM_VSCROLL: {
            SCROLLINFO info{};
            info.cbSize = sizeof(info);
            info.fMask = SIF_ALL;
            GetScrollInfo(hwndDlg, SB_VERT, &info);
            int next = g_mainDialogLayout.scroll_offset;
            const int line = MulDiv(
                24, static_cast<int>(GetWindowDpiCompat(hwndDlg)), 96);
            switch (LOWORD(wParam)) {
                case SB_LINEUP: next -= line; break;
                case SB_LINEDOWN: next += line; break;
                case SB_PAGEUP: next -= (std::max)(line, info.nPage > 0 ? static_cast<int>(info.nPage) : line); break;
                case SB_PAGEDOWN: next += (std::max)(line, info.nPage > 0 ? static_cast<int>(info.nPage) : line); break;
                case SB_THUMBTRACK:
                case SB_THUMBPOSITION: next = info.nTrackPos; break;
                case SB_TOP: next = 0; break;
                case SB_BOTTOM: next = g_mainDialogLayout.max_scroll; break;
                default: return TRUE;
            }
            ScrollMainDialog(hwndDlg, next);
            return TRUE;
        }
        case WM_MOUSEWHEEL: {
            const int line = MulDiv(
                48, static_cast<int>(GetWindowDpiCompat(hwndDlg)), 96);
            const int wheel_delta = static_cast<short>(HIWORD(wParam));
            const int direction = wheel_delta > 0 ? -1 : 1;
            ScrollMainDialog(
                hwndDlg, g_mainDialogLayout.scroll_offset + direction * line);
            return TRUE;
        }
        case WM_SIZE:
            if (g_mainDialogLayout.hwnd == hwndDlg) {
                ApplyMainDialogViewport(hwndDlg);
                return TRUE;
            }
            break;
        case WM_COMMAND: {
            WORD controlId = LOWORD(wParam);
            if (controlId == IDOK) {
                IMEConfig config = ReadConfigFromDialog(hwndDlg);
                if (!SaveConfigWithFeedback(hwndDlg, config)) {
                    return TRUE;
                }

                EndDialog(hwndDlg, IDOK);
                g_isDialogActive = false;
                g_hwndDlg = nullptr;
                return TRUE;
            } else if (controlId == IDCANCEL) {
                EndDialog(hwndDlg, IDCANCEL);
                g_isDialogActive = false;
                g_hwndDlg = nullptr;
                return TRUE;
            } else if (controlId == IDAPPLY) {
                IMEConfig config = ReadConfigFromDialog(hwndDlg);
                SaveConfigWithFeedback(hwndDlg, config);
                return TRUE;
            } else if (controlId == IDC_CHECK_AUTO_WORD_SEGMENTATION &&
                       HIWORD(wParam) == BN_CLICKED) {
                if (IsDlgButtonChecked(
                        hwndDlg, IDC_CHECK_AUTO_WORD_SEGMENTATION) ==
                    BST_CHECKED) {
                    SendDlgItemMessageW(
                        hwndDlg, IDC_COMBO_CORRECTION_LEVEL, CB_SETCURSEL,
                        CorrectionLevelToConfigIndex(
                            CorrectionLevel::Experimental),
                        0);
                }
                return TRUE;
            } else if (controlId == IDC_CHECK_ENABLE_FUZZY_INPUT &&
                       HIWORD(wParam) == BN_CLICKED) {
                MainFuzzyInputState* state =
                    GetMainFuzzyInputState(hwndDlg);
                if (IsDlgButtonChecked(
                        hwndDlg, IDC_CHECK_ENABLE_FUZZY_INPUT) ==
                        BST_CHECKED &&
                    (!state || NormalizeFuzzyInputFlags(
                                   state->pending_flags) == 0)) {
                    const bool configured =
                        ConfigurePendingFuzzyInput(hwndDlg);
                    if (!configured || !state ||
                        NormalizeFuzzyInputFlags(state->pending_flags) == 0) {
                        CheckDlgButton(
                            hwndDlg, IDC_CHECK_ENABLE_FUZZY_INPUT,
                            BST_UNCHECKED);
                    }
                }
                UpdateFuzzyInputStatus(hwndDlg);
                return TRUE;
            } else if (controlId == IDC_BUTTON_FUZZY_INPUT_CONFIG &&
                       HIWORD(wParam) == BN_CLICKED) {
                ConfigurePendingFuzzyInput(hwndDlg);
                UpdateFuzzyInputStatus(hwndDlg);
                return TRUE;
            } else if (controlId == IDC_COMBO_CORRECTION_LEVEL &&
                       HIWORD(wParam) == CBN_SELCHANGE) {
                const LRESULT selected = SendDlgItemMessageW(
                    hwndDlg, IDC_COMBO_CORRECTION_LEVEL,
                    CB_GETCURSEL, 0, 0);
                if (selected == CB_ERR ||
                    NormalizeCorrectionLevelValue(
                        static_cast<DWORD>(selected)) !=
                        CorrectionLevel::Experimental) {
                    CheckDlgButton(
                        hwndDlg, IDC_CHECK_AUTO_WORD_SEGMENTATION,
                        BST_UNCHECKED);
                }
                return TRUE;
            } else if (controlId == IDC_BUTTON_CORRECTION_HELP) {
                const bool isEng = IsMainDialogEnglish(hwndDlg);
                ShowCorrectionHelpDialog(hwndDlg, isEng ? 1 : 0);
                return TRUE;
            } else if (controlId == IDC_COMBO_LANGUAGE) {
                if (HIWORD(wParam) == CBN_SELCHANGE) {
                    const bool isEng = IsMainDialogEnglish(hwndDlg);
                    TranslateDialog(hwndDlg, isEng ? 1 : 0);
                    UpdateDialogIcon(hwndDlg);
                }
                return TRUE;
            } else if (controlId == IDC_CHECK_ENABLE_LOG) {
                if (HIWORD(wParam) == BN_CLICKED) {
                    if (IsDlgButtonChecked(hwndDlg, IDC_CHECK_ENABLE_LOG) == BST_CHECKED) {
                        const bool isEng = IsMainDialogEnglish(hwndDlg);
                        int result = 0;
                        if (isEng) {
                            result = MessageBoxW(
                                hwndDlg,
                                L"WARNING: Enabling debug logging may slightly affect typing performance and will record raw keystroke information for troubleshooting. Only enable this option if absolutely necessary for debugging.\n\nAre you sure you want to enable logging?",
                                L"Debug Warning",
                                MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2
                            );
                        } else {
                            result = MessageBoxW(
                                hwndDlg,
                                L"CẢNH BÁO: Bật ghi nhật ký (log) có thể ảnh hưởng nhỏ đến hiệu năng gõ và ghi lại thông tin phím nhấn thô phục vụ gỡ lỗi. Chỉ bật tùy chọn này khi thực sự cần thiết để debug.\n\nBạn có chắc chắn muốn bật ghi log không?",
                                L"Cảnh báo gỡ lỗi",
                                MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2
                            );
                        }
                        if (result != IDYES) {
                            CheckDlgButton(hwndDlg, IDC_CHECK_ENABLE_LOG, BST_UNCHECKED);
                        }
                    }
                }
                return TRUE;
            } else if (controlId == IDC_BUTTON_SHORTHAND_TABLE) {
                DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_SHORTHAND_DIALOG), hwndDlg, ShorthandDialogProc, 0);
                return TRUE;
            } else if (controlId == IDC_BUTTON_APP_PROFILES) {
                const IMEConfig dialog_config = ReadConfigFromDialog(hwndDlg);
                const AppProfilesDialogInit init{
                    dialog_config.input_method,
                    dialog_config.typing_mode == 0};
                DialogBoxParamW(
                    GetModuleHandleW(nullptr),
                    MAKEINTRESOURCEW(IDD_APP_PROFILES_DIALOG), hwndDlg,
                    AppProfilesDialogProc,
                    reinterpret_cast<LPARAM>(&init));
                return TRUE;
            } else if (controlId == IDC_BUTTON_DIRECT_APPS) {
                DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_DIRECT_APPS_DIALOG), hwndDlg, DirectAppsDialogProc, 0);
                return TRUE;
            }
            break;
        }
        case WM_CLOSE: {
            EndDialog(hwndDlg, IDCANCEL);
            g_isDialogActive = false;
            g_hwndDlg = nullptr;
            return TRUE;
        }
        case WM_NCDESTROY: {
            delete GetMainFuzzyInputState(hwndDlg);
            SetWindowLongPtrW(hwndDlg, GWLP_USERDATA, 0);
            if (g_mainDialogLayout.hwnd == hwndDlg) {
                g_mainDialogLayout = {};
            }
            if (g_hDlgIconBig) {
                DestroyIcon(g_hDlgIconBig);
                g_hDlgIconBig = nullptr;
            }
            if (g_hDlgIconSmall) {
                DestroyIcon(g_hDlgIconSmall);
                g_hDlgIconSmall = nullptr;
            }
            break;
        }
    }
    return FALSE;
}

LRESULT CALLBACK TrayWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE: {
            const HWND taskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
            const UINT taskbar_dpi = GetWindowDpiCompat(taskbar);
            int tray_icon_size =
                GetSystemMetricsForDpiCompat(SM_CXSMICON, taskbar_dpi);
            if (tray_icon_size <= 0) {
                tray_icon_size = 16;
            }
            g_hIconV =
                LoadIconResourceForSize(IDI_TRAY_V_ICON, tray_icon_size);
            g_hIconE =
                LoadIconResourceForSize(IDI_TRAY_E_ICON, tray_icon_size);

            // Add tray icon
            NOTIFYICONDATAW nid = { 0 };
            nid.cbSize = sizeof(NOTIFYICONDATAW);
            nid.hWnd = hwnd;
            nid.uID = IDI_TRAY_ICON;
            nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
            nid.uCallbackMessage = WM_TRAYICON_MSG;
            nid.hIcon = g_hIconV ? g_hIconV : g_hIconE;
            wcscpy_s(nid.szTip, L"Neokey");
            Shell_NotifyIconW(NIM_ADD, &nid);

            UpdateTrayIcon(hwnd);

            // Start registry watching
            g_registryWatchShutdownEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            g_registryWatchEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            if (g_registryWatchShutdownEvent && g_registryWatchEvent) {
                g_registryWatchThread = CreateThread(
                    nullptr, 0, TrayRegistryWatchThreadProc, hwnd, 0, nullptr);
            }
            if (!g_registryWatchThread) {
                if (g_registryWatchShutdownEvent) {
                    CloseHandle(g_registryWatchShutdownEvent);
                    g_registryWatchShutdownEvent = nullptr;
                }
                if (g_registryWatchEvent) {
                    CloseHandle(g_registryWatchEvent);
                    g_registryWatchEvent = nullptr;
                }
            }

            // Set timer for active app polling (every 200ms)
            SetTimer(hwnd, kForegroundPollTimerId, 200, nullptr);
            return 0;
        }
        case WM_USER_SHOW_SETTINGS: {
            if (!g_isDialogActive) {
                g_isDialogActive = true;
                DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_CONFIG_DIALOG), hwnd, DialogProc, 0);
            } else {
                if (g_hwndDlg) {
                    SetForegroundWindow(g_hwndDlg);
                }
            }
            return 0;
        }
        case WM_USER_CONFIG_CHANGED: {
            UpdateTrayIcon(hwnd);
            if (g_isDialogActive && g_hwndDlg) {
                UpdateDialogIcon(g_hwndDlg);
            }
            return 0;
        }
        case WM_TIMER: {
            if (wParam == kForegroundPollTimerId) {
                g_trayClickState.Advance(TrayClickEvent::ForegroundTimer);
                HWND hwndFg = GetForegroundWindow();
                if (hwndFg != g_lastForegroundHwnd) {
                    g_lastForegroundHwnd = hwndFg;
                    std::wstring fg_proc = GetForegroundProcessName(hwndFg);
                    if (!fg_proc.empty() && 
                        fg_proc != L"explorer.exe" && 
                        fg_proc != L"neokey_config.exe" &&
                        fg_proc != L"searchhost.exe" &&
                        fg_proc != L"startmenuexperiencehost.exe") {
                        
                        if (g_lastActiveProcessName != fg_proc) {
                            g_lastActiveProcessName = fg_proc;
                            UpdateTrayIcon(hwnd);
                            if (g_isDialogActive && g_hwndDlg) {
                                UpdateDialogIcon(g_hwndDlg);
                            }
                        }
                    }
                }
            } else if (wParam == kTraySingleClickTimerId) {
                KillTimer(hwnd, kTraySingleClickTimerId);
                if (g_trayClickState.Advance(
                        TrayClickEvent::SingleClickTimer) ==
                    TrayClickAction::ToggleInputMode) {
                    ToggleTrayInputMode();
                    UpdateTrayIcon(hwnd);
                    if (g_isDialogActive && g_hwndDlg) {
                        UpdateDialogIcon(g_hwndDlg);
                    }
                }
            }
            return 0;
        }
        case WM_TRAYICON_MSG: {
            if (lParam == WM_LBUTTONDOWN) {
                if (g_trayClickState.Advance(
                        TrayClickEvent::LeftButtonDown) ==
                    TrayClickAction::ArmSingleClickTimer) {
                    if (SetTimer(
                            hwnd, kTraySingleClickTimerId,
                            GetDoubleClickTime(), nullptr) == 0) {
                        if (g_trayClickState.Advance(
                                TrayClickEvent::SingleClickTimerArmFailed) ==
                            TrayClickAction::ToggleInputMode) {
                            ToggleTrayInputMode();
                            UpdateTrayIcon(hwnd);
                            if (g_isDialogActive && g_hwndDlg) {
                                UpdateDialogIcon(g_hwndDlg);
                            }
                        }
                    }
                }
            } else if (lParam == WM_LBUTTONDBLCLK) {
                if (g_trayClickState.Advance(
                        TrayClickEvent::LeftButtonDoubleClick) ==
                    TrayClickAction::CancelSingleClickTimerAndOpenConfig) {
                    KillTimer(hwnd, kTraySingleClickTimerId);
                    PostMessageW(hwnd, WM_USER_SHOW_SETTINGS, 0, 0);
                }
            } else if (lParam == WM_RBUTTONUP) {
                POINT pt;
                GetCursorPos(&pt);
                HMENU hMenu = CreatePopupMenu();
                if (hMenu) {
                    IMEConfig config = LoadConfigFromRegistry();
                    const ResolvedAppInputProfile effective =
                        ResolveTrayAppInputProfile(config);
                    
                    // Input method checks
                    UINT telexCheck = (effective.enabled && effective.input_method == core::InputMethod::Telex) ? MF_CHECKED : MF_UNCHECKED;
                    UINT stelexCheck = (effective.enabled && effective.input_method == core::InputMethod::SimpleTelex) ? MF_CHECKED : MF_UNCHECKED;
                    UINT vniCheck = (effective.enabled && effective.input_method == core::InputMethod::VNI) ? MF_CHECKED : MF_UNCHECKED;
                    UINT offCheck = !effective.enabled ? MF_CHECKED : MF_UNCHECKED;

                    // Options checks
                    UINT shorthandCheck = config.enable_shorthand ? MF_CHECKED : MF_UNCHECKED;
                    UINT autocorrectCheck = config.enable_auto_correct ? MF_CHECKED : MF_UNCHECKED;

                    // Add items based on active language/mode
                    if (effective.enabled) { // VIE
                        AppendMenuW(hMenu, MF_STRING | telexCheck, ID_TRAY_METHOD_TELEX, L"Kiểu gõ: Telex");
                        AppendMenuW(hMenu, MF_STRING | stelexCheck, ID_TRAY_METHOD_STELEX, L"Kiểu gõ: Simple Telex");
                        AppendMenuW(hMenu, MF_STRING | vniCheck, ID_TRAY_METHOD_VNI, L"Kiểu gõ: VNI");
                        AppendMenuW(hMenu, MF_STRING | offCheck, ID_TRAY_METHOD_OFF, L"Tắt cho ứng dụng");
                        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
                        AppendMenuW(hMenu, MF_STRING | autocorrectCheck, ID_TRAY_TOGGLE_AUTOCORRECT, L"Tự động sửa lỗi");
                        AppendMenuW(hMenu, MF_STRING | shorthandCheck, ID_TRAY_TOGGLE_SHORTHAND, L"Cho phép gõ tắt");
                        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
                        AppendMenuW(hMenu, MF_STRING, ID_TRAY_SETTINGS, L"Cài đặt...");
                        AppendMenuW(hMenu, MF_STRING, ID_TRAY_SHORTHAND, L"Bảng gõ tắt...");
                        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
                        AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT, L"Thoát");
                    } else { // ENG
                        AppendMenuW(hMenu, MF_STRING | telexCheck, ID_TRAY_METHOD_TELEX, L"Typing method: Telex");
                        AppendMenuW(hMenu, MF_STRING | stelexCheck, ID_TRAY_METHOD_STELEX, L"Typing method: Simple Telex");
                        AppendMenuW(hMenu, MF_STRING | vniCheck, ID_TRAY_METHOD_VNI, L"Typing method: VNI");
                        AppendMenuW(hMenu, MF_STRING | offCheck, ID_TRAY_METHOD_OFF, L"Off for this app");
                        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
                        AppendMenuW(hMenu, MF_STRING | autocorrectCheck, ID_TRAY_TOGGLE_AUTOCORRECT, L"Auto-correct");
                        AppendMenuW(hMenu, MF_STRING | shorthandCheck, ID_TRAY_TOGGLE_SHORTHAND, L"Enable shorthand");
                        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
                        AppendMenuW(hMenu, MF_STRING, ID_TRAY_SETTINGS, L"Settings...");
                        AppendMenuW(hMenu, MF_STRING, ID_TRAY_SHORTHAND, L"Shorthand table...");
                        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
                        AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT, L"Exit");
                    }

                    SetForegroundWindow(hwnd);
                    TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
                    DestroyMenu(hMenu);
                }
            }
            return 0;
        }
        case WM_COMMAND: {
            WORD commandId = LOWORD(wParam);
            if (commandId == ID_TRAY_EXIT) {
                DestroyWindow(hwnd);
            } else if (commandId == ID_TRAY_SETTINGS) {
                PostMessageW(hwnd, WM_USER_SHOW_SETTINGS, 0, 0);
            } else if (commandId == ID_TRAY_SHORTHAND) {
                DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_SHORTHAND_DIALOG), hwnd, ShorthandDialogProc, 0);
            } else if (commandId == ID_TRAY_METHOD_TELEX) {
                ApplyTrayInputMode(AppInputMode::Telex);
                UpdateTrayIcon(hwnd);
            } else if (commandId == ID_TRAY_METHOD_STELEX) {
                ApplyTrayInputMode(AppInputMode::SimpleTelex);
                UpdateTrayIcon(hwnd);
            } else if (commandId == ID_TRAY_METHOD_VNI) {
                ApplyTrayInputMode(AppInputMode::VNI);
                UpdateTrayIcon(hwnd);
            } else if (commandId == ID_TRAY_METHOD_OFF) {
                ApplyTrayInputMode(AppInputMode::Off);
                UpdateTrayIcon(hwnd);
            } else if (commandId == ID_TRAY_TOGGLE_SHORTHAND) {
                IMEConfig config = LoadConfigFromRegistry();
                config.enable_shorthand = !config.enable_shorthand;
                SaveConfigWithFeedback(hwnd, config);
            } else if (commandId == ID_TRAY_TOGGLE_AUTOCORRECT) {
                IMEConfig config = LoadConfigFromRegistry();
                config.enable_auto_correct = !config.enable_auto_correct;
                config.auto_correct_level = config.enable_auto_correct ? CorrectionLevel::Normal : CorrectionLevel::Off;
                SaveConfigWithFeedback(hwnd, config);
            }
            return 0;
        }
        case WM_DESTROY: {
            KillTimer(hwnd, kForegroundPollTimerId);
            KillTimer(hwnd, kTraySingleClickTimerId);
            g_trayClickState.Reset();

            // Remove tray icon
            NOTIFYICONDATAW nid = { 0 };
            nid.cbSize = sizeof(NOTIFYICONDATAW);
            nid.hWnd = hwnd;
            nid.uID = IDI_TRAY_ICON;
            Shell_NotifyIconW(NIM_DELETE, &nid);

            // Shutdown registry watch
            if (g_registryWatchShutdownEvent) {
                SetEvent(g_registryWatchShutdownEvent);
            }
            if (g_registryWatchThread) {
                WaitForSingleObject(g_registryWatchThread, INFINITE);
                CloseHandle(g_registryWatchThread);
                g_registryWatchThread = nullptr;
            }
            if (g_registryWatchShutdownEvent) {
                CloseHandle(g_registryWatchShutdownEvent);
                g_registryWatchShutdownEvent = nullptr;
            }
            if (g_registryWatchEvent) {
                CloseHandle(g_registryWatchEvent);
                g_registryWatchEvent = nullptr;
            }

            if (g_hIconV) DestroyIcon(g_hIconV);
            if (g_hIconE) DestroyIcon(g_hIconE);
            if (g_hDlgIconBig) {
                DestroyIcon(g_hDlgIconBig);
                g_hDlgIconBig = nullptr;
            }
            if (g_hDlgIconSmall) {
                DestroyIcon(g_hDlgIconSmall);
                g_hDlgIconSmall = nullptr;
            }
            DestroyModernUiResources();

            PostQuitMessage(0);
            return 0;
        }
        default:
            return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    }
}

int WINAPI WinMain(HINSTANCE hInstance, [[maybe_unused]] HINSTANCE hPrevInstance, [[maybe_unused]] LPSTR lpCmdLine, [[maybe_unused]] int nCmdShow) {
    // Initialize common controls for modern styling (DPI and themes)
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES;
    InitCommonControlsEx(&icex);

    // Single instance check using named Mutex
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"Local\\NeokeyConfigMutex");
    if (hMutex == nullptr) {
        return 0;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(hMutex);
        // Find existing hidden window and signal it to show settings
        HWND hwndExisting = FindWindowW(L"NeokeyTrayWindowClass", nullptr);
        if (hwndExisting) {
            PostMessageW(hwndExisting, WM_USER_SHOW_SETTINGS, 0, 0);
        }
        return 0;
    }

    // RICHEDIT50W powers syntax highlighting in the shorthand editor.
    HMODULE richEditModule = LoadLibraryW(L"Msftedit.dll");

    NotifyShellExecutableIconChanged();

    // Register hidden window class for tray icon and registry notifications
    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = TrayWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"NeokeyTrayWindowClass";
    RegisterClassW(&wc);

    HWND hwndTray = CreateWindowExW(0, L"NeokeyTrayWindowClass", L"NeokeyTray", 0, 0, 0, 0, 0, nullptr, nullptr, hInstance, nullptr);
    if (!hwndTray) {
        if (richEditModule) {
            FreeLibrary(richEditModule);
        }
        CloseHandle(hMutex);
        return 0;
    }

    g_hwndTray = hwndTray;

    // Parse command line arguments: if user runs with "-silent", start in background
    std::wstring cmdLine = GetCommandLineW();
    if (cmdLine.find(L"-silent") == std::wstring::npos) {
        PostMessageW(hwndTray, WM_USER_SHOW_SETTINGS, 0, 0);
    }

    // Message loop
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    DestroyWindow(hwndTray);
    if (richEditModule) {
        FreeLibrary(richEditModule);
    }
    CloseHandle(hMutex);
    return 0;
}
