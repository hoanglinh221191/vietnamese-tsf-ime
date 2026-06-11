#include <windows.h>
#include <commctrl.h>
#include <string>
#include "resources.h"
#include "config.hpp"

using namespace vn_ime;

std::wstring ReadShorthandFile(const std::wstring& filePath) {
    std::wstring content;
    ReadUtf8TextFile(filePath, content);
    return content;
}

bool WriteShorthandFile(const std::wstring& filePath, const std::wstring& content) {
    return WriteUtf8TextFileAtomic(filePath, content);
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
    config.enable_log = (IsDlgButtonChecked(hwndDlg, IDC_CHECK_ENABLE_LOG) == BST_CHECKED);
    config.enable_shorthand = (IsDlgButtonChecked(hwndDlg, IDC_CHECK_ENABLE_SHORTHAND) == BST_CHECKED);
    config.enable_auto_capitalize = (IsDlgButtonChecked(hwndDlg, IDC_CHECK_AUTO_CAPITALIZE) == BST_CHECKED);
    config.enable_app_blocklist = (IsDlgButtonChecked(hwndDlg, IDC_CHECK_ENABLE_APP_BLOCKLIST) == BST_CHECKED);
    config.enable_auto_exclude = (IsDlgButtonChecked(hwndDlg, IDC_CHECK_AUTO_EXCLUDE) == BST_CHECKED);
    
    if (IsDlgButtonChecked(hwndDlg, IDC_RADIO_HOTKEY_CTRL_SHIFT) == BST_CHECKED) {
        config.hotkey_mode = 0;
    } else if (IsDlgButtonChecked(hwndDlg, IDC_RADIO_HOTKEY_ALT_Z) == BST_CHECKED) {
        config.hotkey_mode = 1;
    }
    
    return config;
}

INT_PTR CALLBACK ShorthandDialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_INITDIALOG: {
            // Set text limit of the multiline edit to 16MB
            SendDlgItemMessage(hwndDlg, IDC_EDIT_SHORTHAND_RULES, EM_SETLIMITTEXT, 16 * 1024 * 1024, 0);

            // Load shorthand rules
            std::wstring filePath = GetShorthandFilePath(nullptr);
            std::wstring content = ReadShorthandFile(filePath);
            SetDlgItemTextW(hwndDlg, IDC_EDIT_SHORTHAND_RULES, content.c_str());
            return TRUE;
        }
        case WM_COMMAND: {
            WORD controlId = LOWORD(wParam);
            if (controlId == IDOK) {
                std::wstring content = GetDlgItemTextString(hwndDlg, IDC_EDIT_SHORTHAND_RULES);

                // Save rules
                std::wstring filePath = GetShorthandFilePath(nullptr);
                if (WriteShorthandFile(filePath, content)) {
                    TouchConfigRevision();
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
                    WriteShorthandFile(exportPath, content);
                }
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

INT_PTR CALLBACK AppBlocklistDialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_INITDIALOG: {
            SendDlgItemMessage(hwndDlg, IDC_EDIT_APP_BLOCKLIST, EM_SETLIMITTEXT, 1024 * 1024, 0);
            IMEConfig config = LoadConfigFromRegistry();
            std::wstring text = ProcessListToText(config.blocked_apps);
            SetDlgItemTextW(hwndDlg, IDC_EDIT_APP_BLOCKLIST, text.c_str());
            return TRUE;
        }
        case WM_COMMAND: {
            WORD controlId = LOWORD(wParam);
            if (controlId == IDOK) {
                IMEConfig config = LoadConfigFromRegistry();
                std::wstring text = GetDlgItemTextString(hwndDlg, IDC_EDIT_APP_BLOCKLIST);
                config.blocked_apps = ParseProcessListText(text);
                config.auto_blocked_apps = PreserveAutoBlockedAppsForBlocklist(config.auto_blocked_apps, config.blocked_apps);

                SaveConfigToRegistry(config);
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

HWND g_hwndTray = nullptr;
HWND g_hwndDlg = nullptr;
bool g_isDialogActive = false;
HICON g_hIconV = nullptr;
HICON g_hIconE = nullptr;

HANDLE g_registryWatchThread = nullptr;
HANDLE g_registryWatchShutdownEvent = nullptr;
HANDLE g_registryWatchEvent = nullptr;

HICON CreateDynamicTrayIcon(wchar_t letter, COLORREF bgColor) {
    int size = GetSystemMetrics(SM_CXSMICON);
    if (size <= 0) size = 16;

    HDC hScreenDC = GetDC(nullptr);
    HDC hMemDC = CreateCompatibleDC(hScreenDC);

    BITMAPINFO bmi = { 0 };
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = size;
    bmi.bmiHeader.biHeight = size;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pBits = nullptr;
    HBITMAP hColorBmp = CreateDIBSection(hMemDC, &bmi, DIB_RGB_COLORS, &pBits, nullptr, 0);
    HBITMAP hOldBmp = (HBITMAP)SelectObject(hMemDC, hColorBmp);

    // Draw background (mask black)
    RECT rect = { 0, 0, size, size };
    HBRUSH hBlackBrush = (HBRUSH)GetStockObject(BLACK_BRUSH);
    FillRect(hMemDC, &rect, hBlackBrush);

    // Draw rounded rectangle in color
    HPEN hNullPen = CreatePen(PS_NULL, 0, 0);
    HPEN hOldPen = (HPEN)SelectObject(hMemDC, hNullPen);
    HBRUSH hBrush = CreateSolidBrush(bgColor);
    HBRUSH hOldBrush = (HBRUSH)SelectObject(hMemDC, hBrush);
    RoundRect(hMemDC, 0, 0, size, size, size / 3, size / 3);
    SelectObject(hMemDC, hOldBrush);
    DeleteObject(hBrush);
    SelectObject(hMemDC, hOldPen);
    DeleteObject(hNullPen);

    // Draw text
    SetTextColor(hMemDC, RGB(255, 255, 255));
    SetBkMode(hMemDC, TRANSPARENT);

    HFONT hFont = CreateFontW(
        size - 4, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
    );
    HFONT hOldFont = (HFONT)SelectObject(hMemDC, hFont);

    wchar_t text[2] = { letter, L'\0' };
    DrawTextW(hMemDC, text, 1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    SelectObject(hMemDC, hOldFont);
    DeleteObject(hFont);
    SelectObject(hMemDC, hOldBmp);

    // Create mask bitmap
    HBITMAP hMaskBmp = CreateBitmap(size, size, 1, 1, nullptr);
    HDC hMaskDC = CreateCompatibleDC(hScreenDC);
    HBITMAP hOldMaskBmp = (HBITMAP)SelectObject(hMaskDC, hMaskBmp);
    RECT maskRect = { 0, 0, size, size };
    HBRUSH hWhiteBrush = (HBRUSH)GetStockObject(WHITE_BRUSH);
    FillRect(hMaskDC, &maskRect, hWhiteBrush);

    // Draw black rounded rect (visible area)
    hNullPen = CreatePen(PS_NULL, 0, 0);
    hOldPen = (HPEN)SelectObject(hMaskDC, hNullPen);
    hBlackBrush = (HBRUSH)GetStockObject(BLACK_BRUSH);
    hOldBrush = (HBRUSH)SelectObject(hMaskDC, hBlackBrush);
    RoundRect(hMaskDC, 0, 0, size, size, size / 3, size / 3);
    SelectObject(hMaskDC, hOldBrush);
    SelectObject(hMaskDC, hOldPen);
    DeleteObject(hNullPen);

    SelectObject(hMaskDC, hOldMaskBmp);
    DeleteDC(hMaskDC);

    ICONINFO ii = { 0 };
    ii.fIcon = TRUE;
    ii.xHotspot = 0;
    ii.yHotspot = 0;
    ii.hbmMask = hMaskBmp;
    ii.hbmColor = hColorBmp;

    HICON hIcon = CreateIconIndirect(&ii);

    DeleteObject(hColorBmp);
    DeleteObject(hMaskBmp);
    DeleteDC(hMemDC);
    ReleaseDC(nullptr, hScreenDC);

    return hIcon;
}

void UpdateTrayIcon(HWND hwnd) {
    IMEConfig config = LoadConfigFromRegistry();
    HICON hIcon = (config.typing_mode == 0) ? g_hIconV : g_hIconE;

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

INT_PTR CALLBACK DialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_INITDIALOG: {
            g_hwndDlg = hwndDlg;
            
            // Load current config
            IMEConfig config = LoadConfigFromRegistry();

            // Set layout selection
            if (config.input_method == core::InputMethod::Telex) {
                CheckRadioButton(hwndDlg, IDC_RADIO_TELEX, IDC_RADIO_VNI, IDC_RADIO_TELEX);
            } else if (config.input_method == core::InputMethod::SimpleTelex) {
                CheckRadioButton(hwndDlg, IDC_RADIO_TELEX, IDC_RADIO_VNI, IDC_RADIO_SIMPLE_TELEX);
            } else if (config.input_method == core::InputMethod::VNI) {
                CheckRadioButton(hwndDlg, IDC_RADIO_TELEX, IDC_RADIO_VNI, IDC_RADIO_VNI);
            }

            // Set checks
            HWND hwndCombo = GetDlgItem(hwndDlg, IDC_COMBO_CORRECTION_LEVEL);
            SendMessageW(hwndCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Off"));
            SendMessageW(hwndCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Normal"));
            SendMessageW(hwndCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Advanced"));
            SendMessageW(hwndCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Experimental"));
            SendMessageW(hwndCombo, CB_SETCURSEL, static_cast<WPARAM>(CorrectionLevelToConfigIndex(config.auto_correct_level)), 0);
            CheckDlgButton(hwndDlg, IDC_CHECK_ENABLE_LOG, config.enable_log ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(hwndDlg, IDC_CHECK_ENABLE_SHORTHAND, config.enable_shorthand ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(hwndDlg, IDC_CHECK_AUTO_CAPITALIZE, config.enable_auto_capitalize ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(hwndDlg, IDC_CHECK_ENABLE_APP_BLOCKLIST, config.enable_app_blocklist ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(hwndDlg, IDC_CHECK_AUTO_EXCLUDE, config.enable_auto_exclude ? BST_CHECKED : BST_UNCHECKED);
            
            // Set hotkey checks
            if (config.hotkey_mode == 0) {
                CheckRadioButton(hwndDlg, IDC_RADIO_HOTKEY_CTRL_SHIFT, IDC_RADIO_HOTKEY_ALT_Z, IDC_RADIO_HOTKEY_CTRL_SHIFT);
            } else {
                CheckRadioButton(hwndDlg, IDC_RADIO_HOTKEY_CTRL_SHIFT, IDC_RADIO_HOTKEY_ALT_Z, IDC_RADIO_HOTKEY_ALT_Z);
            }

            std::wstring versionText = GetConfigAppVersionText();
            SetDlgItemTextW(hwndDlg, IDC_STATIC_VERSION, versionText.c_str());
            return TRUE;
        }
        case WM_COMMAND: {
            WORD controlId = LOWORD(wParam);
            if (controlId == IDOK) {
                IMEConfig config = ReadConfigFromDialog(hwndDlg);
                SaveConfigToRegistry(config);

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
                SaveConfigToRegistry(config);
                TouchConfigRevision();
                return TRUE;
            } else if (controlId == IDC_CHECK_ENABLE_LOG) {
                if (HIWORD(wParam) == BN_CLICKED) {
                    if (IsDlgButtonChecked(hwndDlg, IDC_CHECK_ENABLE_LOG) == BST_CHECKED) {
                        int result = MessageBoxW(
                            hwndDlg,
                            L"CẢNH BÁO: Bật ghi nhật ký (log) có thể ảnh hưởng nhỏ đến hiệu năng gõ và ghi lại thông tin phím nhấn thô phục vụ gỡ lỗi. Chỉ bật tùy chọn này khi thực sự cần thiết để debug.\n\nBạn có chắc chắn muốn bật ghi log không?",
                            L"Cảnh báo gỡ lỗi",
                            MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2
                        );
                        if (result != IDYES) {
                            CheckDlgButton(hwndDlg, IDC_CHECK_ENABLE_LOG, BST_UNCHECKED);
                        }
                    }
                }
                return TRUE;
            } else if (controlId == IDC_BUTTON_SHORTHAND_TABLE) {
                DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_SHORTHAND_DIALOG), hwndDlg, ShorthandDialogProc, 0);
                return TRUE;
            } else if (controlId == IDC_BUTTON_APP_BLOCKLIST) {
                DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_APP_BLOCKLIST_DIALOG), hwndDlg, AppBlocklistDialogProc, 0);
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
    }
    return FALSE;
}

LRESULT CALLBACK TrayWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE: {
            g_hIconV = CreateDynamicTrayIcon(L'V', RGB(229, 57, 53)); // Red rounded icon
            g_hIconE = CreateDynamicTrayIcon(L'E', RGB(30, 136, 229)); // Blue rounded icon

            // Add tray icon
            NOTIFYICONDATAW nid = { 0 };
            nid.cbSize = sizeof(NOTIFYICONDATAW);
            nid.hWnd = hwnd;
            nid.uID = IDI_TRAY_ICON;
            nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
            nid.uCallbackMessage = WM_TRAYICON_MSG;
            nid.hIcon = g_hIconV;
            wcscpy_s(nid.szTip, L"Neokey");
            Shell_NotifyIconW(NIM_ADD, &nid);

            UpdateTrayIcon(hwnd);

            // Start registry watching
            g_registryWatchShutdownEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            g_registryWatchEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            g_registryWatchThread = CreateThread(nullptr, 0, TrayRegistryWatchThreadProc, hwnd, 0, nullptr);
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
            return 0;
        }
        case WM_TRAYICON_MSG: {
            if (lParam == WM_LBUTTONDBLCLK || lParam == WM_LBUTTONDOWN) {
                // Toggle mode
                IMEConfig config = LoadConfigFromRegistry();
                config.typing_mode = (config.typing_mode == 0) ? 1 : 0;
                SaveConfigToRegistry(config);
                TouchConfigRevision();
                UpdateTrayIcon(hwnd);
            } else if (lParam == WM_RBUTTONUP) {
                POINT pt;
                GetCursorPos(&pt);
                HMENU hMenu = CreatePopupMenu();
                if (hMenu) {
                    IMEConfig config = LoadConfigFromRegistry();
                    
                    // Input method checks
                    UINT telexCheck = (config.input_method == core::InputMethod::Telex) ? MF_CHECKED : MF_UNCHECKED;
                    UINT stelexCheck = (config.input_method == core::InputMethod::SimpleTelex) ? MF_CHECKED : MF_UNCHECKED;
                    UINT vniCheck = (config.input_method == core::InputMethod::VNI) ? MF_CHECKED : MF_UNCHECKED;

                    // Options checks
                    UINT shorthandCheck = config.enable_shorthand ? MF_CHECKED : MF_UNCHECKED;
                    UINT autocorrectCheck = config.enable_auto_correct ? MF_CHECKED : MF_UNCHECKED;

                    // Add items
                    AppendMenuW(hMenu, MF_STRING | telexCheck, ID_TRAY_METHOD_TELEX, L"Kiểu gõ: Telex");
                    AppendMenuW(hMenu, MF_STRING | stelexCheck, ID_TRAY_METHOD_STELEX, L"Kiểu gõ: Simple Telex");
                    AppendMenuW(hMenu, MF_STRING | vniCheck, ID_TRAY_METHOD_VNI, L"Kiểu gõ: VNI");
                    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
                    AppendMenuW(hMenu, MF_STRING | autocorrectCheck, ID_TRAY_TOGGLE_AUTOCORRECT, L"Tự động sửa lỗi");
                    AppendMenuW(hMenu, MF_STRING | shorthandCheck, ID_TRAY_TOGGLE_SHORTHAND, L"Cho phép gõ tắt");
                    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
                    AppendMenuW(hMenu, MF_STRING, ID_TRAY_SETTINGS, L"Cài đặt...");
                    AppendMenuW(hMenu, MF_STRING, ID_TRAY_SHORTHAND, L"Bảng gõ tắt...");
                    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
                    AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT, L"Thoát");

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
                IMEConfig config = LoadConfigFromRegistry();
                config.input_method = core::InputMethod::Telex;
                SaveConfigToRegistry(config);
                TouchConfigRevision();
            } else if (commandId == ID_TRAY_METHOD_STELEX) {
                IMEConfig config = LoadConfigFromRegistry();
                config.input_method = core::InputMethod::SimpleTelex;
                SaveConfigToRegistry(config);
                TouchConfigRevision();
            } else if (commandId == ID_TRAY_METHOD_VNI) {
                IMEConfig config = LoadConfigFromRegistry();
                config.input_method = core::InputMethod::VNI;
                SaveConfigToRegistry(config);
                TouchConfigRevision();
            } else if (commandId == ID_TRAY_TOGGLE_SHORTHAND) {
                IMEConfig config = LoadConfigFromRegistry();
                config.enable_shorthand = !config.enable_shorthand;
                SaveConfigToRegistry(config);
                TouchConfigRevision();
            } else if (commandId == ID_TRAY_TOGGLE_AUTOCORRECT) {
                IMEConfig config = LoadConfigFromRegistry();
                config.enable_auto_correct = !config.enable_auto_correct;
                config.auto_correct_level = config.enable_auto_correct ? CorrectionLevel::Normal : CorrectionLevel::Off;
                SaveConfigToRegistry(config);
                TouchConfigRevision();
            }
            return 0;
        }
        case WM_DESTROY: {
            // Remove tray icon
            NOTIFYICONDATAW nid = { 0 };
            nid.cbSize = sizeof(NOTIFYICONDATAW);
            nid.hWnd = hwnd;
            nid.uID = IDI_TRAY_ICON;
            Shell_NotifyIconW(NIM_DELETE, &nid);

            // Shutdown registry watch
            if (g_registryWatchShutdownEvent) {
                SetEvent(g_registryWatchShutdownEvent);
                if (g_registryWatchThread) {
                    WaitForSingleObject(g_registryWatchThread, 2000);
                    CloseHandle(g_registryWatchThread);
                    g_registryWatchThread = nullptr;
                }
                CloseHandle(g_registryWatchShutdownEvent);
                g_registryWatchShutdownEvent = nullptr;
            }
            if (g_registryWatchEvent) {
                CloseHandle(g_registryWatchEvent);
                g_registryWatchEvent = nullptr;
            }

            if (g_hIconV) DestroyIcon(g_hIconV);
            if (g_hIconE) DestroyIcon(g_hIconE);

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
    icex.dwICC = ICC_STANDARD_CLASSES;
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

    // Register hidden window class for tray icon and registry notifications
    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = TrayWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"NeokeyTrayWindowClass";
    RegisterClassW(&wc);

    HWND hwndTray = CreateWindowExW(0, L"NeokeyTrayWindowClass", L"NeokeyTray", 0, 0, 0, 0, 0, nullptr, nullptr, hInstance, nullptr);
    if (!hwndTray) {
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
    CloseHandle(hMutex);
    return 0;
}
