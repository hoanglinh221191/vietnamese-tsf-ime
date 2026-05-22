#include <windows.h>
#include <msctf.h>
#include "ime_processor.hpp"
#include "com_ptr.hpp"
#include <strsafe.h>

static void LogDebug(LPCWSTR format, ...) {
    wchar_t buffer[512];
    va_list args;
    va_start(args, format);
    StringCchVPrintfW(buffer, 512, format, args);
    va_end(args);
    OutputDebugStringW(buffer);
}

// Define modern TSF Category CAP GUIDs if not in msctf.h
#ifndef _MSC_VER
#ifndef GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT
inline constexpr GUID GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT = {
    0x5aae106b, 0x502f, 0x435f, { 0xa3, 0x30, 0x46, 0xcb, 0xdf, 0xbe, 0x6e, 0xf7 }
};
#endif

#ifndef GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT
inline constexpr GUID GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT = {
    0x0ab7753c, 0x05e1, 0x49c6, { 0xba, 0xb8, 0x81, 0x9d, 0x2e, 0x84, 0x47, 0x47 }
};
#endif

#ifndef GUID_TFCAT_TIPCAP_SECUREKEYBOARDONLY
inline constexpr GUID GUID_TFCAT_TIPCAP_SECUREKEYBOARDONLY = {
    0xad05c751, 0x74b4, 0x43cc, { 0xbe, 0xa5, 0x80, 0x09, 0xb8, 0x56, 0x0f, 0xa9 }
};
#endif
#endif

namespace vn_ime {

// Helper to write string values to registry
static HRESULT CreateRegistryKeyAndSetValue(HKEY rootKey, LPCWSTR subKeyPath, LPCWSTR valueName, LPCWSTR data) {
    HKEY hKey = nullptr;
    LSTATUS status = RegCreateKeyExW(rootKey, subKeyPath, 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr);
    if (status != ERROR_SUCCESS) {
        LogDebug(L"RegCreateKeyExW failed for path %s with status %d", subKeyPath, status);
        return HRESULT_FROM_WIN32(status);
    }

    status = RegSetValueExW(hKey, valueName, 0, REG_SZ, reinterpret_cast<const BYTE*>(data), static_cast<DWORD>((wcslen(data) + 1) * sizeof(wchar_t)));
    if (status != ERROR_SUCCESS) {
        LogDebug(L"RegSetValueExW failed for value %s with status %d", valueName ? valueName : L"(Default)", status);
    }
    RegCloseKey(hKey);
    return HRESULT_FROM_WIN32(status);
}

// Full COM registration
HRESULT RegisterCOMServer(HINSTANCE hInst) {
    LogDebug(L"RegisterCOMServer started");
    wchar_t modulePath[MAX_PATH] = { 0 };
    if (GetModuleFileNameW(hInst, modulePath, MAX_PATH) == 0) {
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        LogDebug(L"GetModuleFileNameW failed with hr 0x%08X", hr);
        return hr;
    }
    LogDebug(L"Module path: %s", modulePath);

    wchar_t clsidStr[64] = { 0 };
    if (StringFromGUID2(CLSID_VietnameseIME, clsidStr, 64) == 0) {
        return E_FAIL;
    }

    // Key: Software\Classes\CLSID\{A85F2C8C-7DE6-4F7F-9B67-4EBEA54D4A4B}
    wchar_t clsidKeyPath[256] = { 0 };
    wsprintfW(clsidKeyPath, L"Software\\Classes\\CLSID\\%s", clsidStr);

    // Determine root key: try HKLM first, fallback to HKCU
    HKEY rootKey = HKEY_CURRENT_USER;
    HKEY hKeyTest = nullptr;
    LSTATUS status = RegCreateKeyExW(HKEY_LOCAL_MACHINE, L"Software\\Classes\\CLSID", 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKeyTest, nullptr);
    if (status == ERROR_SUCCESS) {
        RegCloseKey(hKeyTest);
        rootKey = HKEY_LOCAL_MACHINE;
        LogDebug(L"Using HKEY_LOCAL_MACHINE for COM registration");
    } else {
        rootKey = HKEY_CURRENT_USER;
        LogDebug(L"Using HKEY_CURRENT_USER for COM registration");
    }

    HRESULT hr = CreateRegistryKeyAndSetValue(rootKey, clsidKeyPath, nullptr, L"Neokey");
    if (FAILED(hr)) return hr;

    // Subkey: InprocServer32
    wchar_t inprocKeyPath[256] = { 0 };
    wsprintfW(inprocKeyPath, L"%s\\InprocServer32", clsidKeyPath);
    
    hr = CreateRegistryKeyAndSetValue(rootKey, inprocKeyPath, nullptr, modulePath);
    if (FAILED(hr)) return hr;

    hr = CreateRegistryKeyAndSetValue(rootKey, inprocKeyPath, L"ThreadingModel", L"Apartment");
    return hr;
}

// Full COM unregistration
HRESULT UnregisterCOMServer() {
    wchar_t clsidStr[64] = { 0 };
    if (StringFromGUID2(CLSID_VietnameseIME, clsidStr, 64) == 0) {
        return E_FAIL;
    }

    wchar_t clsidKeyPath[256] = { 0 };
    wsprintfW(clsidKeyPath, L"Software\\Classes\\CLSID\\%s", clsidStr);

    // Try deleting from HKLM first, then HKCU
    LogDebug(L"Deleting COM registry keys");
    LSTATUS status1 = RegDeleteTreeW(HKEY_LOCAL_MACHINE, clsidKeyPath);
    LSTATUS status2 = RegDeleteTreeW(HKEY_CURRENT_USER, clsidKeyPath);

    if (status1 == ERROR_SUCCESS || status2 == ERROR_SUCCESS) {
        return S_OK;
    }
    if (status1 == ERROR_FILE_NOT_FOUND && status2 == ERROR_FILE_NOT_FOUND) {
        return S_OK;
    }
    return HRESULT_FROM_WIN32(status1 != ERROR_SUCCESS ? status1 : status2);
}

// Full TSF registration
HRESULT RegisterTSFProfile() {
    LogDebug(L"RegisterTSFProfile started");
    ComPtr<ITfInputProcessorProfileMgr> profileMgr;
    HRESULT hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER, IID_ITfInputProcessorProfileMgr, reinterpret_cast<void**>(profileMgr.GetAddressOf()));
    if (FAILED(hr)) {
        LogDebug(L"CoCreateInstance(CLSID_TF_InputProcessorProfiles) failed with hr 0x%08X", hr);
        return MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x101);
    }

    // Language ID: 0x042a (Vietnamese)
    LogDebug(L"Calling RegisterProfile");
    hr = profileMgr->RegisterProfile(
        CLSID_VietnameseIME,
        0x042a,
        GUID_VietnameseProfile,
        L"Neokey",
        static_cast<ULONG>(wcslen(L"Neokey")),
        L"Neokey",
        static_cast<ULONG>(wcslen(L"Neokey")),
        0, nullptr, 0, // No icon details for now
        TRUE, 0
    );
    if (FAILED(hr)) {
        LogDebug(L"RegisterProfile failed with hr 0x%08X", hr);
        return hr;
    }

    // Register Categories using ITfCategoryMgr
    LogDebug(L"Creating ITfCategoryMgr");
    ComPtr<ITfCategoryMgr> categoryMgr;
    hr = CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER, IID_ITfCategoryMgr, reinterpret_cast<void**>(categoryMgr.GetAddressOf()));
    if (FAILED(hr)) {
        LogDebug(L"CoCreateInstance(CLSID_TF_CategoryMgr) failed with hr 0x%08X", hr);
        return MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x103);
    }

    // Category: TIP Keyboard
    LogDebug(L"Registering GUID_TFCAT_TIP_KEYBOARD");
    hr = categoryMgr->RegisterCategory(CLSID_VietnameseIME, GUID_TFCAT_TIP_KEYBOARD, CLSID_VietnameseIME);
    if (FAILED(hr)) {
        LogDebug(L"RegisterCategory(GUID_TFCAT_TIP_KEYBOARD) failed with hr 0x%08X", hr);
        return MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x104);
    }

    // Category: Modern UWP/Immersive apps support
    LogDebug(L"Registering GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT");
    hr = categoryMgr->RegisterCategory(CLSID_VietnameseIME, GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT, CLSID_VietnameseIME);
    if (FAILED(hr)) {
        LogDebug(L"RegisterCategory(GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT) failed with hr 0x%08X", hr);
        return MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x105);
    }

    // Category: System Tray integration support
    LogDebug(L"Registering GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT");
    hr = categoryMgr->RegisterCategory(CLSID_VietnameseIME, GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT, CLSID_VietnameseIME);
    if (FAILED(hr)) {
        LogDebug(L"RegisterCategory(GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT) failed with hr 0x%08X", hr);
        return MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x106);
    }

    // Category: Display Attribute Provider
    LogDebug(L"Registering GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER");
    hr = categoryMgr->RegisterCategory(CLSID_VietnameseIME, GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER, CLSID_VietnameseIME);
    if (FAILED(hr)) {
        LogDebug(L"RegisterCategory(GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER) failed with hr 0x%08X", hr);
        return MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x108);
    }

    // Category: Display Attribute
    LogDebug(L"Registering GUID_VietnameseDisplayAttribute");
    hr = categoryMgr->RegisterCategory(CLSID_VietnameseIME, GUID_TFCAT_DISPLAYATTRIBUTE, GUID_VietnameseDisplayAttribute);
    if (FAILED(hr)) {
        LogDebug(L"RegisterCategory(GUID_VietnameseDisplayAttribute) failed with hr 0x%08X", hr);
        return MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x109);
    }

    LogDebug(L"RegisterTSFProfile succeeded");
    return S_OK;
}

// Full TSF unregistration
HRESULT UnregisterTSFProfile() {
    ComPtr<ITfInputProcessorProfileMgr> profileMgr;
    HRESULT hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER, IID_ITfInputProcessorProfileMgr, reinterpret_cast<void**>(profileMgr.GetAddressOf()));
    if (FAILED(hr)) return hr;

    hr = profileMgr->UnregisterProfile(CLSID_VietnameseIME, 0x042a, GUID_VietnameseProfile, 0);
    if (FAILED(hr)) return hr;

    ComPtr<ITfCategoryMgr> categoryMgr;
    hr = CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER, IID_ITfCategoryMgr, reinterpret_cast<void**>(categoryMgr.GetAddressOf()));
    if (FAILED(hr)) return hr;

    // Unregister categories
    categoryMgr->UnregisterCategory(CLSID_VietnameseIME, GUID_TFCAT_TIP_KEYBOARD, CLSID_VietnameseIME);
    categoryMgr->UnregisterCategory(CLSID_VietnameseIME, GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT, CLSID_VietnameseIME);
    categoryMgr->UnregisterCategory(CLSID_VietnameseIME, GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT, CLSID_VietnameseIME);
    categoryMgr->UnregisterCategory(CLSID_VietnameseIME, GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER, CLSID_VietnameseIME);
    categoryMgr->UnregisterCategory(CLSID_VietnameseIME, GUID_TFCAT_DISPLAYATTRIBUTE, GUID_VietnameseDisplayAttribute);

    return S_OK;
}

} // namespace vn_ime
