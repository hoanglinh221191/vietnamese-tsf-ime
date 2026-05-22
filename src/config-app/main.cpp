#include <windows.h>
#include <commctrl.h>
#include <string>
#include "resources.h"
#include "config.hpp"

using namespace vn_ime;

std::wstring ReadShorthandFile(const std::wstring& filePath) {
    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        return L"";
    }
    DWORD fileSize = GetFileSize(hFile, nullptr);
    if (fileSize == INVALID_FILE_SIZE || fileSize == 0) {
        CloseHandle(hFile);
        return L"";
    }
    std::string utf8Content;
    utf8Content.resize(fileSize);
    DWORD bytesRead = 0;
    if (!ReadFile(hFile, &utf8Content[0], fileSize, &bytesRead, nullptr) || bytesRead == 0) {
        CloseHandle(hFile);
        return L"";
    }
    CloseHandle(hFile);
    utf8Content.resize(bytesRead);

    // Convert UTF-8 to UTF-16
    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8Content.data(), static_cast<int>(utf8Content.length()), nullptr, 0);
    if (wlen <= 0) return L"";

    std::wstring utf16Content;
    utf16Content.resize(wlen);
    MultiByteToWideChar(CP_UTF8, 0, utf8Content.data(), static_cast<int>(utf8Content.length()), &utf16Content[0], wlen);

    // Skip BOM if present
    if (!utf16Content.empty() && utf16Content[0] == L'\xFEFF') {
        return utf16Content.substr(1);
    }
    return utf16Content;
}

bool WriteShorthandFile(const std::wstring& filePath, const std::wstring& content) {
    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        return false;
    }

    // Write UTF-8 BOM first
    const unsigned char BOM[] = { 0xEF, 0xBB, 0xBF };
    DWORD bytesWritten = 0;
    WriteFile(hFile, BOM, sizeof(BOM), &bytesWritten, nullptr);

    // Convert content from UTF-16 to UTF-8
    if (!content.empty()) {
        int len = WideCharToMultiByte(CP_UTF8, 0, content.data(), static_cast<int>(content.length()), nullptr, 0, nullptr, nullptr);
        if (len > 0) {
            std::string utf8Content;
            utf8Content.resize(len);
            WideCharToMultiByte(CP_UTF8, 0, content.data(), static_cast<int>(content.length()), &utf8Content[0], len, nullptr, nullptr);
            WriteFile(hFile, utf8Content.data(), static_cast<DWORD>(utf8Content.length()), &bytesWritten, nullptr);
        }
    }
    CloseHandle(hFile);
    return true;
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
                // Get rules text
                int len = GetWindowTextLengthW(GetDlgItem(hwndDlg, IDC_EDIT_SHORTHAND_RULES));
                std::wstring content;
                content.resize(len);
                GetDlgItemTextW(hwndDlg, IDC_EDIT_SHORTHAND_RULES, &content[0], len + 1);

                // Save rules
                std::wstring filePath = GetShorthandFilePath(nullptr);
                WriteShorthandFile(filePath, content);

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
                    int len = GetWindowTextLengthW(GetDlgItem(hwndDlg, IDC_EDIT_SHORTHAND_RULES));
                    std::wstring content;
                    content.resize(len);
                    GetDlgItemTextW(hwndDlg, IDC_EDIT_SHORTHAND_RULES, &content[0], len + 1);
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
            CheckDlgButton(hwndDlg, IDC_CHECK_AUTO_CORRECT, config.enable_auto_correct ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(hwndDlg, IDC_CHECK_ENABLE_LOG, config.enable_log ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(hwndDlg, IDC_CHECK_ENABLE_SHORTHAND, config.enable_shorthand ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(hwndDlg, IDC_CHECK_AUTO_CAPITALIZE, config.enable_auto_capitalize ? BST_CHECKED : BST_UNCHECKED);
            return TRUE;
        }
        case WM_COMMAND: {
            WORD controlId = LOWORD(wParam);
            if (controlId == IDOK) {
                // Save config
                IMEConfig config;
                if (IsDlgButtonChecked(hwndDlg, IDC_RADIO_TELEX) == BST_CHECKED) {
                    config.input_method = core::InputMethod::Telex;
                } else if (IsDlgButtonChecked(hwndDlg, IDC_RADIO_SIMPLE_TELEX) == BST_CHECKED) {
                    config.input_method = core::InputMethod::SimpleTelex;
                } else if (IsDlgButtonChecked(hwndDlg, IDC_RADIO_VNI) == BST_CHECKED) {
                    config.input_method = core::InputMethod::VNI;
                }
                config.enable_auto_correct = (IsDlgButtonChecked(hwndDlg, IDC_CHECK_AUTO_CORRECT) == BST_CHECKED);
                config.enable_log = (IsDlgButtonChecked(hwndDlg, IDC_CHECK_ENABLE_LOG) == BST_CHECKED);
                config.enable_shorthand = (IsDlgButtonChecked(hwndDlg, IDC_CHECK_ENABLE_SHORTHAND) == BST_CHECKED);
                config.enable_auto_capitalize = (IsDlgButtonChecked(hwndDlg, IDC_CHECK_AUTO_CAPITALIZE) == BST_CHECKED);
                SaveConfigToRegistry(config);

                EndDialog(hwndDlg, IDOK);
                return TRUE;
            } else if (controlId == IDCANCEL) {
                EndDialog(hwndDlg, IDCANCEL);
                return TRUE;
            } else if (controlId == IDAPPLY) {
                // Save config
                IMEConfig config;
                if (IsDlgButtonChecked(hwndDlg, IDC_RADIO_TELEX) == BST_CHECKED) {
                    config.input_method = core::InputMethod::Telex;
                } else if (IsDlgButtonChecked(hwndDlg, IDC_RADIO_SIMPLE_TELEX) == BST_CHECKED) {
                    config.input_method = core::InputMethod::SimpleTelex;
                } else if (IsDlgButtonChecked(hwndDlg, IDC_RADIO_VNI) == BST_CHECKED) {
                    config.input_method = core::InputMethod::VNI;
                }
                config.enable_auto_correct = (IsDlgButtonChecked(hwndDlg, IDC_CHECK_AUTO_CORRECT) == BST_CHECKED);
                config.enable_log = (IsDlgButtonChecked(hwndDlg, IDC_CHECK_ENABLE_LOG) == BST_CHECKED);
                config.enable_shorthand = (IsDlgButtonChecked(hwndDlg, IDC_CHECK_ENABLE_SHORTHAND) == BST_CHECKED);
                config.enable_auto_capitalize = (IsDlgButtonChecked(hwndDlg, IDC_CHECK_AUTO_CAPITALIZE) == BST_CHECKED);
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
