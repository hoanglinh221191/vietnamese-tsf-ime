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

INT_PTR CALLBACK DialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_INITDIALOG: {
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
                return TRUE;
            } else if (controlId == IDCANCEL) {
                EndDialog(hwndDlg, IDCANCEL);
                return TRUE;
            } else if (controlId == IDAPPLY) {
                IMEConfig config = ReadConfigFromDialog(hwndDlg);
                SaveConfigToRegistry(config);
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
            return TRUE;
        }
    }
    return FALSE;
}

int WINAPI WinMain(HINSTANCE hInstance, [[maybe_unused]] HINSTANCE hPrevInstance, [[maybe_unused]] LPSTR lpCmdLine, [[maybe_unused]] int nCmdShow) {
    // Initialize common controls for modern styling (DPI and themes)
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icex);

    DialogBoxParamW(hInstance, MAKEINTRESOURCEW(IDD_CONFIG_DIALOG), nullptr, DialogProc, 0);
    return 0;
}
