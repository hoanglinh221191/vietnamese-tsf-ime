#include <windows.h>
#include "ime_processor.hpp"
#include "class_factory.hpp"
#include "logger.hpp"

// Global DLL Module instance handle
HINSTANCE g_hInst = nullptr;

extern "C" BOOL WINAPI DllMain(HINSTANCE hInst, DWORD dwReason, [[maybe_unused]] LPVOID pvReserved) {
    switch (dwReason) {
        case DLL_PROCESS_ATTACH:
            g_hInst = hInst;
            // Disable thread library calls for performance (we don't need DLL_THREAD_ATTACH/DETACH)
            DisableThreadLibraryCalls(hInst);
            break;
        case DLL_PROCESS_DETACH:
            vn_ime::logger::Shutdown();
            break;
    }
    return TRUE;
}

// Returns a class factory to create an object of the requested type
STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;

    if (rclsid != vn_ime::CLSID_VietnameseIME) {
        return CLASS_E_CLASSNOTAVAILABLE;
    }

    vn_ime::ClassFactory* factory = new (std::nothrow) vn_ime::ClassFactory();
    if (!factory) return E_OUTOFMEMORY;

    HRESULT hr = factory->QueryInterface(riid, ppv);
    factory->Release(); // Balance the factory ref count
    return hr;
}

// Determines whether the DLL can be unloaded by COM
STDAPI DllCanUnloadNow() {
    if (vn_ime::ClassFactory::GetActiveObjects() == 0 && 
        vn_ime::ClassFactory::GetServerLocks() == 0) {
        return S_OK;
    }
    return S_FALSE;
}

// Registers the COM server and TSF profiles
STDAPI DllRegisterServer() {
    HRESULT hr = vn_ime::RegisterCOMServer(g_hInst);
    if (FAILED(hr)) {
        return hr;
    }

    hr = vn_ime::RegisterTSFProfile();
    if (FAILED(hr)) {
        vn_ime::UnregisterCOMServer();
        return hr;
    }

    return S_OK;
}

// Unregisters the COM server and TSF profiles
STDAPI DllUnregisterServer() {
    HRESULT hr1 = vn_ime::UnregisterTSFProfile();
    HRESULT hr2 = vn_ime::UnregisterCOMServer();

    return FAILED(hr1) ? hr1 : hr2;
}
