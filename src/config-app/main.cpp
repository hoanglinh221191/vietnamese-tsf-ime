#include <windows.h>
#include <commctrl.h>
#include <new>
#include <string>
#include "resources.h"
#include "config.hpp"
#include "tray_click_state.hpp"

using namespace vn_ime;

extern std::wstring g_lastActiveProcessName;

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
            L"BẢO VỆ TIẾNG ANH (độc lập):\n"
            L"   - Tắt: không bảo vệ từ tiếng Anh.\n"
            L"   - Cân bằng: bảo vệ từ Anh/mã, ưu tiên chuỗi gõ Việt chuẩn.\n"
            L"   - Ưu tiên tiếng Anh: giữ nguyên từ Anh phổ biến.";

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
            L"ENGLISH PROTECTION (independent):\n"
            L"   - Off: no English-word protection.\n"
            L"   - Balanced: protects English/code, but favors canonical Vietnamese input.\n"
            L"   - English First: preserves common English words.";

        MessageBoxW(hwndDlg, text.c_str(), L"Auto-Correction Info - Neokey", MB_OK | MB_ICONINFORMATION);
    }
}

void TranslateDialog(HWND hwndDlg, int typingMode) {
    if (typingMode == 0) { // Vietnamese
        SetWindowTextW(hwndDlg, L"Cấu hình Neokey");
        SetDlgItemTextW(hwndDlg, IDC_GROUP_METHOD, L"Phương pháp gõ");
        SetDlgItemTextW(hwndDlg, IDC_RADIO_TELEX, L"Telex");
        SetDlgItemTextW(hwndDlg, IDC_RADIO_SIMPLE_TELEX, L"Simple Telex");
        SetDlgItemTextW(hwndDlg, IDC_RADIO_VNI, L"VNI");
        
        SetDlgItemTextW(hwndDlg, IDC_GROUP_OPTIONS, L"Tùy chọn");
        SetDlgItemTextW(hwndDlg, IDC_STATIC_CORRECTION_LEVEL, L"Mức tự động sửa lỗi:");
        SetDlgItemTextW(hwndDlg, IDC_BUTTON_CORRECTION_HELP, L"?");
        SetDlgItemTextW(hwndDlg, IDC_STATIC_ENGLISH_PROTECTION, L"Bảo vệ tiếng Anh:");
        
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
        SendMessageW(hwndEnglishCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Cân bằng"));
        SendMessageW(hwndEnglishCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Ưu tiên tiếng Anh"));
        SendMessageW(hwndEnglishCombo, CB_SETCURSEL, static_cast<WPARAM>(englishSel), 0);
        
        SetDlgItemTextW(hwndDlg, IDC_CHECK_ENABLE_LOG, L"Bật file log để gỡ lỗi (Chỉ dùng khi debug)");
        SetDlgItemTextW(hwndDlg, IDC_CHECK_ENABLE_SHORTHAND, L"Bật tính năng gõ tắt");
        SetDlgItemTextW(hwndDlg, IDC_BUTTON_SHORTHAND_TABLE, L"Bảng gõ tắt...");
        SetDlgItemTextW(hwndDlg, IDC_CHECK_AUTO_CAPITALIZE, L"Tự động viết hoa sau dấu chấm");
        SetDlgItemTextW(hwndDlg, IDC_GROUP_APP_PROFILES, L"Thiết lập theo ứng dụng");
        SetDlgItemTextW(hwndDlg, IDC_CHECK_ENABLE_APP_PROFILES, L"Dùng thiết lập theo ứng dụng");
        SetDlgItemTextW(hwndDlg, IDC_CHECK_AUTO_APP_PROFILES, L"Tự động ghi nhớ kiểu gõ/tắt theo ứng dụng");
        SetDlgItemTextW(hwndDlg, IDC_BUTTON_APP_PROFILES, L"Cấu hình ứng dụng...");
        SetDlgItemTextW(hwndDlg, IDC_STATIC_DIRECT_APPS, L"Chế độ direct inline/commit:");
        SetDlgItemTextW(hwndDlg, IDC_BUTTON_DIRECT_APPS, L"Ứng dụng...");
        
        SetDlgItemTextW(hwndDlg, IDC_GROUP_HOTKEY, L"Phím tắt bật/tắt NeoKey");
        SetDlgItemTextW(hwndDlg, IDC_RADIO_HOTKEY_CTRL_SHIFT, L"Ctrl + Shift");
        SetDlgItemTextW(hwndDlg, IDC_RADIO_HOTKEY_ALT_Z, L"Alt + Z");
        
        SetDlgItemTextW(hwndDlg, IDC_GROUP_LANGUAGE, L"Giao diện & Chế độ gõ");
        SetDlgItemTextW(hwndDlg, IDC_RADIO_LANG_VIE, L"Tiếng Việt (VIE)");
        SetDlgItemTextW(hwndDlg, IDC_RADIO_LANG_ENG, L"English (ENG)");
        
        SetDlgItemTextW(hwndDlg, IDC_CHECK_AUTO_START, L"Khởi động ứng dụng cấu hình cùng Windows");
        SetDlgItemTextW(hwndDlg, IDC_STATIC_STARTUP_DESC, L"Chỉ khởi động ứng dụng cấu hình; bộ gõ hoạt động độc lập.");
        
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
        SetDlgItemTextW(hwndDlg, IDC_GROUP_METHOD, L"Typing Method");
        SetDlgItemTextW(hwndDlg, IDC_RADIO_TELEX, L"Telex");
        SetDlgItemTextW(hwndDlg, IDC_RADIO_SIMPLE_TELEX, L"Simple Telex");
        SetDlgItemTextW(hwndDlg, IDC_RADIO_VNI, L"VNI");
        
        SetDlgItemTextW(hwndDlg, IDC_GROUP_OPTIONS, L"Options");
        SetDlgItemTextW(hwndDlg, IDC_STATIC_CORRECTION_LEVEL, L"Auto-Correction Level:");
        
        HWND hwndCombo = GetDlgItem(hwndDlg, IDC_COMBO_CORRECTION_LEVEL);
        LRESULT curSel = SendMessageW(hwndCombo, CB_GETCURSEL, 0, 0);
        if (curSel == CB_ERR) curSel = 1;
        SendMessageW(hwndCombo, CB_RESETCONTENT, 0, 0);
        SendMessageW(hwndCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Off"));
        SendMessageW(hwndCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Normal"));
        SendMessageW(hwndCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Advanced"));
        SendMessageW(hwndCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Experimental"));
        SendMessageW(hwndCombo, CB_SETCURSEL, static_cast<WPARAM>(curSel), 0);
        
        SetDlgItemTextW(hwndDlg, IDC_STATIC_ENGLISH_PROTECTION, L"English protection:");
        HWND hwndEnglishCombo = GetDlgItem(hwndDlg, IDC_COMBO_ENGLISH_PROTECTION);
        LRESULT englishSel = SendMessageW(hwndEnglishCombo, CB_GETCURSEL, 0, 0);
        if (englishSel == CB_ERR) englishSel = 1;
        SendMessageW(hwndEnglishCombo, CB_RESETCONTENT, 0, 0);
        SendMessageW(hwndEnglishCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Off"));
        SendMessageW(hwndEnglishCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Balanced"));
        SendMessageW(hwndEnglishCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"English First"));
        SendMessageW(hwndEnglishCombo, CB_SETCURSEL, static_cast<WPARAM>(englishSel), 0);
        SetDlgItemTextW(hwndDlg, IDC_CHECK_ENABLE_LOG, L"Enable debug logging (Use for debugging only)");
        SetDlgItemTextW(hwndDlg, IDC_CHECK_ENABLE_SHORTHAND, L"Enable shorthand");
        SetDlgItemTextW(hwndDlg, IDC_BUTTON_SHORTHAND_TABLE, L"Shorthand table...");
        SetDlgItemTextW(hwndDlg, IDC_CHECK_AUTO_CAPITALIZE, L"Auto-capitalize after period");
        SetDlgItemTextW(hwndDlg, IDC_GROUP_APP_PROFILES, L"Per-app typing modes");
        SetDlgItemTextW(hwndDlg, IDC_CHECK_ENABLE_APP_PROFILES, L"Use per-app typing settings");
        SetDlgItemTextW(hwndDlg, IDC_CHECK_AUTO_APP_PROFILES, L"Automatically remember typing mode/off per app");
        SetDlgItemTextW(hwndDlg, IDC_BUTTON_APP_PROFILES, L"Configure apps...");
        SetDlgItemTextW(hwndDlg, IDC_STATIC_DIRECT_APPS, L"Direct inline/commit modes:");
        SetDlgItemTextW(hwndDlg, IDC_BUTTON_DIRECT_APPS, L"Configure...");
        
        SetDlgItemTextW(hwndDlg, IDC_GROUP_HOTKEY, L"Hotkey to turn NeoKey on/off");
        SetDlgItemTextW(hwndDlg, IDC_RADIO_HOTKEY_CTRL_SHIFT, L"Ctrl + Shift");
        SetDlgItemTextW(hwndDlg, IDC_RADIO_HOTKEY_ALT_Z, L"Alt + Z");
        
        SetDlgItemTextW(hwndDlg, IDC_GROUP_LANGUAGE, L"Language & Typing Mode");
        SetDlgItemTextW(hwndDlg, IDC_RADIO_LANG_VIE, L"Tiếng Việt (VIE)");
        SetDlgItemTextW(hwndDlg, IDC_RADIO_LANG_ENG, L"English (ENG)");
        
        SetDlgItemTextW(hwndDlg, IDC_CHECK_AUTO_START, L"Start the configuration app with Windows");
        SetDlgItemTextW(hwndDlg, IDC_STATIC_STARTUP_DESC, L"Starts only the configuration app; the IME runs independently.");
        
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
    config.enable_auto_capitalize = (IsDlgButtonChecked(hwndDlg, IDC_CHECK_AUTO_CAPITALIZE) == BST_CHECKED);
    config.enable_app_input_profiles =
        IsDlgButtonChecked(hwndDlg, IDC_CHECK_ENABLE_APP_PROFILES) ==
        BST_CHECKED;
    config.enable_auto_app_input_profiles =
        IsDlgButtonChecked(hwndDlg, IDC_CHECK_AUTO_APP_PROFILES) ==
        BST_CHECKED;
    config.enable_app_blocklist = config.enable_app_input_profiles;
    config.enable_auto_exclude = config.enable_auto_app_input_profiles;
    config.enable_auto_start = (IsDlgButtonChecked(hwndDlg, IDC_CHECK_AUTO_START) == BST_CHECKED);
    
    if (IsDlgButtonChecked(hwndDlg, IDC_RADIO_HOTKEY_CTRL_SHIFT) == BST_CHECKED) {
        config.hotkey_mode = 0;
    } else if (IsDlgButtonChecked(hwndDlg, IDC_RADIO_HOTKEY_ALT_Z) == BST_CHECKED) {
        config.hotkey_mode = 1;
    }

    if (IsDlgButtonChecked(hwndDlg, IDC_RADIO_LANG_VIE) == BST_CHECKED) {
        config.typing_mode = 0;
    } else if (IsDlgButtonChecked(hwndDlg, IDC_RADIO_LANG_ENG) == BST_CHECKED) {
        config.typing_mode = 1;
    }
    SyncLegacyAppProfileViews(config);
    
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

            // Translate dialog UI based on config.typing_mode
            IMEConfig config = LoadConfigFromRegistry();
            if (config.typing_mode == 0) { // VIE
                SetWindowTextW(hwndDlg, L"Bảng Từ Gõ Tắt");
                SetDlgItemTextW(hwndDlg, IDC_STATIC_SHORTHAND_DESC, L"Nhập các quy tắc gõ tắt (ví dụ: vn=Việt Nam). Mỗi quy tắc trên một dòng.");
                SetDlgItemTextW(hwndDlg, IDC_BUTTON_IMPORT, L"Nhập file...");
                SetDlgItemTextW(hwndDlg, IDC_BUTTON_EXPORT, L"Xuất file...");
                SetDlgItemTextW(hwndDlg, IDOK, L"Lưu");
                SetDlgItemTextW(hwndDlg, IDCANCEL, L"Hủy bỏ");
            } else { // ENG
                SetWindowTextW(hwndDlg, L"Shorthand Rules");
                SetDlgItemTextW(hwndDlg, IDC_STATIC_SHORTHAND_DESC, L"Enter shorthand rules (e.g. vn=Việt Nam). One rule per line.");
                SetDlgItemTextW(hwndDlg, IDC_BUTTON_IMPORT, L"Import...");
                SetDlgItemTextW(hwndDlg, IDC_BUTTON_EXPORT, L"Export...");
                SetDlgItemTextW(hwndDlg, IDOK, L"Save");
                SetDlgItemTextW(hwndDlg, IDCANCEL, L"Cancel");
            }
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
        SetDlgItemTextW(hwndDlg, IDCANCEL, L"Hủy bỏ");
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
    switch (uMsg) {
        case WM_INITDIALOG: {
            const IMEConfig config = LoadConfigFromRegistry();
            const auto* init = reinterpret_cast<const AppProfilesDialogInit*>(
                lParam);
            auto* state = new (std::nothrow) AppProfilesDialogState{
                NormalizeAppInputProfiles(config.app_input_profiles),
                init ? init->global_method : config.input_method,
                init ? init->vietnamese : config.typing_mode == 0};
            if (!state) {
                EndDialog(hwndDlg, IDCANCEL);
                return TRUE;
            }
            SetWindowLongPtrW(
                hwndDlg, DWLP_USER, reinterpret_cast<LONG_PTR>(state));
            TranslateAppProfilesDialog(hwndDlg);
            InitializeAppProfilesList(hwndDlg);
            RefreshAppProfilesList(hwndDlg);
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
                SaveConfigToRegistry(config);
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
    switch (uMsg) {
        case WM_INITDIALOG: {
            SendDlgItemMessage(hwndDlg, IDC_EDIT_DIRECT_APPS, EM_SETLIMITTEXT, 1024 * 1024, 0);
            IMEConfig config = LoadConfigFromRegistry();
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
            return TRUE;
        }
        case WM_COMMAND: {
            WORD controlId = LOWORD(wParam);
            if (controlId == IDOK) {
                IMEConfig config = LoadConfigFromRegistry();
                std::wstring text = GetDlgItemTextString(hwndDlg, IDC_EDIT_DIRECT_APPS);
                config.direct_apps = ParseDirectAppsListText(text);

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
    SaveConfigToRegistry(config);
    return true;
}

bool ToggleTrayInputMode() {
    IMEConfig config = LoadConfigFromRegistry();
    const AppInputUpdateResult result = ToggleUserInputMode(
        config, g_lastActiveProcessName);
    if (!result.changed) {
        return false;
    }
    SaveConfigToRegistry(config);
    return true;
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

HICON CreateDynamicAppIcon(bool isEnglish, int size) {
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

    // Color: Red for VIE, Blue/Navy for ENG
    COLORREF bgColor = isEnglish ? RGB(30, 136, 229) : RGB(229, 57, 53);

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

    // Choose text based on size
    std::wstring text = (size <= 16) ? L"NK" : L"NeoK";

    // Dynamically calculate font size to fit rect width without clipping
    int font_size = size - 4;
    HFONT hFont = nullptr;
    HFONT hOldFont = nullptr;
    SIZE sz;

    while (font_size > 4) {
        hFont = CreateFontW(
            font_size, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
        );
        hOldFont = (HFONT)SelectObject(hMemDC, hFont);
        
        if (GetTextExtentPoint32W(hMemDC, text.c_str(), static_cast<int>(text.length()), &sz)) {
            if (sz.cx <= size - 4 && sz.cy <= size - 4) {
                break; // Fits!
            }
        }
        
        SelectObject(hMemDC, hOldFont);
        DeleteObject(hFont);
        hFont = nullptr;
        font_size--;
    }

    if (hFont) {
        DrawTextW(hMemDC, text.c_str(), static_cast<int>(text.length()), &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hMemDC, hOldFont);
        DeleteObject(hFont);
    }

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

void UpdateDialogIcon(HWND hwndDlg) {
    if (!hwndDlg) return;

    bool isEnglish = false;
    if (g_isDialogActive && g_hwndDlg) {
        isEnglish = (IsDlgButtonChecked(g_hwndDlg, IDC_RADIO_LANG_ENG) == BST_CHECKED);
    } else {
        IMEConfig config = LoadConfigFromRegistry();
        isEnglish = !ResolveTrayAppInputProfile(config).enabled;
    }

    // Clean up old icons if any
    if (g_hDlgIconBig) {
        DestroyIcon(g_hDlgIconBig);
        g_hDlgIconBig = nullptr;
    }
    if (g_hDlgIconSmall) {
        DestroyIcon(g_hDlgIconSmall);
        g_hDlgIconSmall = nullptr;
    }

    int sizeBig = GetSystemMetrics(SM_CXICON);
    if (sizeBig <= 0) sizeBig = 32;
    int sizeSmall = GetSystemMetrics(SM_CXSMICON);
    if (sizeSmall <= 0) sizeSmall = 16;

    g_hDlgIconBig = CreateDynamicAppIcon(isEnglish, sizeBig);
    g_hDlgIconSmall = CreateDynamicAppIcon(isEnglish, sizeSmall);

    if (g_hDlgIconBig) {
        SendMessageW(hwndDlg, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(g_hDlgIconBig));
    }
    if (g_hDlgIconSmall) {
        SendMessageW(hwndDlg, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(g_hDlgIconSmall));
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

INT_PTR CALLBACK DialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_INITDIALOG: {
            g_hwndDlg = hwndDlg;

            // Load and set window icon dynamically based on active typing mode (VIE = Red, ENG = Blue)
            UpdateDialogIcon(hwndDlg);
            
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
            CheckDlgButton(hwndDlg, IDC_CHECK_ENABLE_LOG, config.enable_log ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(hwndDlg, IDC_CHECK_ENABLE_SHORTHAND, config.enable_shorthand ? BST_CHECKED : BST_UNCHECKED);
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
            CheckDlgButton(hwndDlg, IDC_CHECK_AUTO_START, config.enable_auto_start ? BST_CHECKED : BST_UNCHECKED);
            
            // Set hotkey checks
            if (config.hotkey_mode == 0) {
                CheckRadioButton(hwndDlg, IDC_RADIO_HOTKEY_CTRL_SHIFT, IDC_RADIO_HOTKEY_ALT_Z, IDC_RADIO_HOTKEY_CTRL_SHIFT);
            } else {
                CheckRadioButton(hwndDlg, IDC_RADIO_HOTKEY_CTRL_SHIFT, IDC_RADIO_HOTKEY_ALT_Z, IDC_RADIO_HOTKEY_ALT_Z);
            }

            // Set language selection radio buttons
            if (config.typing_mode == 0) {
                CheckRadioButton(hwndDlg, IDC_RADIO_LANG_VIE, IDC_RADIO_LANG_ENG, IDC_RADIO_LANG_VIE);
            } else {
                CheckRadioButton(hwndDlg, IDC_RADIO_LANG_VIE, IDC_RADIO_LANG_ENG, IDC_RADIO_LANG_ENG);
            }

            // Translate dialog UI based on loaded typing_mode
            TranslateDialog(hwndDlg, config.typing_mode);
            HWND hwndCombo = GetDlgItem(hwndDlg, IDC_COMBO_CORRECTION_LEVEL);
            SendMessageW(hwndCombo, CB_SETCURSEL, static_cast<WPARAM>(CorrectionLevelToConfigIndex(config.auto_correct_level)), 0);
            HWND hwndEnglishCombo = GetDlgItem(hwndDlg, IDC_COMBO_ENGLISH_PROTECTION);
            SendMessageW(hwndEnglishCombo, CB_SETCURSEL, static_cast<WPARAM>(EnglishProtectionLevelToConfigIndex(config.english_protection_level)), 0);

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
                return TRUE;
            } else if (controlId == IDC_BUTTON_CORRECTION_HELP) {
                bool isEng = (IsDlgButtonChecked(hwndDlg, IDC_RADIO_LANG_ENG) == BST_CHECKED);
                ShowCorrectionHelpDialog(hwndDlg, isEng ? 1 : 0);
                return TRUE;
            } else if (controlId == IDC_RADIO_LANG_VIE || controlId == IDC_RADIO_LANG_ENG) {
                if (HIWORD(wParam) == BN_CLICKED) {
                    bool isEng = (IsDlgButtonChecked(hwndDlg, IDC_RADIO_LANG_ENG) == BST_CHECKED);
                    TranslateDialog(hwndDlg, isEng ? 1 : 0);
                    UpdateDialogIcon(hwndDlg);
                }
                return TRUE;
            } else if (controlId == IDC_CHECK_ENABLE_LOG) {
                if (HIWORD(wParam) == BN_CLICKED) {
                    if (IsDlgButtonChecked(hwndDlg, IDC_CHECK_ENABLE_LOG) == BST_CHECKED) {
                        bool isEng = (IsDlgButtonChecked(hwndDlg, IDC_RADIO_LANG_ENG) == BST_CHECKED);
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
            if (g_hDlgIconBig) {
                DestroyIcon(g_hDlgIconBig);
                g_hDlgIconBig = nullptr;
            }
            if (g_hDlgIconSmall) {
                DestroyIcon(g_hDlgIconSmall);
                g_hDlgIconSmall = nullptr;
            }

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
