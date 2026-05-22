#pragma once

#include <windows.h>
#include <msctf.h>
#include "com_ptr.hpp"
#include "class_factory.hpp"
#include "engine.hpp"

// Define ITfTextInputProcessorEx manually as it might be missing in some MinGW headers
#ifndef __ITfTextInputProcessorEx_INTERFACE_DEFINED__
#define __ITfTextInputProcessorEx_INTERFACE_DEFINED__

inline constexpr IID IID_ITfTextInputProcessorEx = {
    0x191d9630, 0xa2a4, 0x11e0, { 0xba, 0xad, 0x00, 0x21, 0x8a, 0x29, 0x6d, 0x22 }
};

MIDL_INTERFACE("191d9630-a2a4-11e0-baad-00218a296d22")
ITfTextInputProcessorEx : public ITfTextInputProcessor
{
public:
    virtual HRESULT STDMETHODCALLTYPE ActivateEx( 
        ITfThreadMgr *ptm,
        TfClientId tid,
        DWORD dwFlags) = 0;
};

#endif

// Define ITfDisplayAttributeProvider manually if missing in MinGW headers
#ifndef __ITfDisplayAttributeProvider_INTERFACE_DEFINED__
#define __ITfDisplayAttributeProvider_INTERFACE_DEFINED__

inline constexpr IID IID_ITfDisplayAttributeProvider = {
    0xfee47777, 0x163c, 0x4769, { 0x99, 0x6a, 0x6e, 0x9c, 0x50, 0xad, 0x8f, 0x54 }
};

MIDL_INTERFACE("fee47777-163c-4769-996a-6e9c50ad8f54")
ITfDisplayAttributeProvider : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE EnumDisplayAttributeInfo( 
        IEnumTfDisplayAttributeInfo **ppEnum) = 0;
        
    virtual HRESULT STDMETHODCALLTYPE GetDisplayAttributeInfo( 
        REFGUID guid,
        ITfDisplayAttributeInfo **ppInfo) = 0;
};

#endif

#ifndef __ITfFunction_INTERFACE_DEFINED__
#define __ITfFunction_INTERFACE_DEFINED__

inline constexpr IID IID_ITfFunction = {
    0xe4b24c9c, 0x09d1, 0x4dbd, { 0x96, 0xe5, 0x35, 0x79, 0x7f, 0x90, 0x4b, 0x61 }
};

MIDL_INTERFACE("e4b24c9c-09d1-4dbd-96e5-35797f904b61")
ITfFunction : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE GetDisplayName(BSTR *pbstrName) = 0;
};

#endif

#ifndef __ITfCandidateList_INTERFACE_DEFINED__
#define __ITfCandidateList_INTERFACE_DEFINED__

inline constexpr IID IID_ITfCandidateList = {
    0xa3ad50fb, 0x9bdb, 0x49e3, { 0xa8, 0x43, 0x6c, 0x76, 0x52, 0x0f, 0xbf, 0x5d }
};

struct ITfCandidateString;
struct IEnumTfCandidates;

MIDL_INTERFACE("a3ad50fb-9bdb-49e3-a843-6c76520fbf5d")
ITfCandidateList : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE EnumCandidates(IEnumTfCandidates **ppEnum) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCandidate(ULONG nIndex, ITfCandidateString **ppCand) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCandidateNum(ULONG *pnCnt) = 0;
    
    typedef enum {
        CAND_FINALIZED = 0x0,
        CAND_SELECTED  = 0x1,
        CAND_CANCELED  = 0x2,
    } TfCandidateResult;

    virtual HRESULT STDMETHODCALLTYPE SetResult(ULONG nIndex, TfCandidateResult imcr) = 0;
};

#endif

#ifndef __ITfFnReconversion_INTERFACE_DEFINED__
#define __ITfFnReconversion_INTERFACE_DEFINED__

inline constexpr IID IID_ITfFnReconversion = {
    0x4ea48a35, 0x6085, 0x4285, { 0xa1, 0x3c, 0x07, 0x02, 0x93, 0x1d, 0x38, 0x0b }
};

MIDL_INTERFACE("4ea48a35-6085-4285-a13c-0702931d380b")
ITfFnReconversion : public ITfFunction
{
public:
    virtual HRESULT STDMETHODCALLTYPE QueryRange(
        ITfRange *pRange,
        ITfRange **ppNewRange,
        BOOL *pfConvertible) = 0;
        
    virtual HRESULT STDMETHODCALLTYPE GetReconversion(
        ITfRange *pRange,
        ITfCandidateList **ppCandList) = 0;
        
    virtual HRESULT STDMETHODCALLTYPE Reconvert(
        ITfRange *pRange) = 0;
};

#endif

#ifndef __ITfSourceSingle_INTERFACE_DEFINED__
#define __ITfSourceSingle_INTERFACE_DEFINED__

inline constexpr IID IID_ITfSourceSingle = {
    0x4e6350d1, 0xa74b, 0x11d2, { 0x8b, 0x10, 0x00, 0x10, 0x5a, 0x27, 0x99, 0xb5 }
};

MIDL_INTERFACE("4e6350d1-a74b-11d2-8b10-00105a2799b5")
ITfSourceSingle : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE AdviseSingleSink(
        TfClientId tid,
        REFIID riid,
        IUnknown *punk) = 0;
        
    virtual HRESULT STDMETHODCALLTYPE UnadviseSingleSink(
        TfClientId tid,
        REFIID riid) = 0;
};

#endif

#ifndef __ITfFunctionProvider_INTERFACE_DEFINED__
#define __ITfFunctionProvider_INTERFACE_DEFINED__

inline constexpr IID IID_ITfFunctionProvider = {
    0x101d8641, 0x6011, 0x11d2, { 0x83, 0xc0, 0x00, 0x10, 0x5a, 0x27, 0x99, 0xb5 }
};

MIDL_INTERFACE("101d8641-6011-11d2-83c0-00105a2799b5")
ITfFunctionProvider : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE GetType(
        GUID *pguid) = 0;
        
    virtual HRESULT STDMETHODCALLTYPE GetDescription(
        BSTR *pbstrDesc) = 0;
        
    virtual HRESULT STDMETHODCALLTYPE GetFunction(
        REFGUID rguid,
        REFIID riid,
        IUnknown **ppunk) = 0;
};

#endif


namespace vn_ime {

// Define GUID_TFCAT_DISPLAYATTRIBUTE if missing
#ifndef GUID_TFCAT_DISPLAYATTRIBUTE
inline constexpr GUID GUID_TFCAT_DISPLAYATTRIBUTE = {
    0x191d9630, 0xa2a4, 0x11d0, { 0xb1, 0x18, 0x00, 0xaa, 0x00, 0xba, 0x76, 0x61 }
};
#endif

// Main CLSID of our Vietnamese IME
// {A85F2C8C-7DE6-4F7F-9B67-4EBEA54D4A4B}
inline constexpr CLSID CLSID_VietnameseIME = { 
    0xa85f2c8c, 0x7de6, 0x4f7f, { 0x9b, 0x67, 0x4e, 0xbe, 0xa5, 0x4d, 0x4a, 0x4b } 
};

// Profile GUID for the Vietnamese layout
// {4B6925B4-1E4E-40BC-BDD3-C26BA333CD12}
inline constexpr GUID GUID_VietnameseProfile = {
    0x4b6925b4, 0x1e4e, 0x40bc, { 0xbd, 0xd3, 0xc2, 0x6b, 0xa3, 0x33, 0xcd, 0x12 }
};

// Display Attribute GUID for Vietnamese text composition styling
// {C5D6C58B-E20C-4BEF-903D-94D93C0C4623}
inline constexpr GUID GUID_VietnameseDisplayAttribute = {
    0xc5d6c58b, 0xe20c, 0x4bef, { 0x90, 0x3d, 0x94, 0xd9, 0x3c, 0x0c, 0x46, 0x23 }
};

class VietnameseIME : public ITfTextInputProcessorEx,
                      public ITfKeyEventSink,
                      public ITfThreadMgrEventSink,
                      public ITfDisplayAttributeProvider,
                      public ITfCompositionSink,
                      public ITfFunctionProvider,
                      public ITfFnReconversion,
                      public ITfMouseSink {
public:
    VietnameseIME() noexcept;
    virtual ~VietnameseIME() noexcept;

    // IUnknown methods
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // ITfTextInputProcessor methods
    STDMETHODIMP Activate(ITfThreadMgr* ptm, TfClientId tid) override;
    STDMETHODIMP Deactivate() override;

    // ITfTextInputProcessorEx methods
    STDMETHODIMP ActivateEx(ITfThreadMgr* ptm, TfClientId tid, DWORD dwFlags) override;

    // ITfKeyEventSink methods
    STDMETHODIMP OnSetFocus(BOOL fForeground) override;
    STDMETHODIMP OnTestKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    STDMETHODIMP OnKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    STDMETHODIMP OnTestKeyUp(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    STDMETHODIMP OnKeyUp(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    STDMETHODIMP OnPreservedKey(ITfContext* pic, REFGUID rguid, BOOL* pfEaten) override;

    // ITfThreadMgrEventSink methods
    STDMETHODIMP OnInitDocumentMgr(ITfDocumentMgr* pdm) override;
    STDMETHODIMP OnUninitDocumentMgr(ITfDocumentMgr* pdm) override;
    STDMETHODIMP OnSetFocus(ITfDocumentMgr* pdmFocus, ITfDocumentMgr* pdmPrevFocus) override;
    STDMETHODIMP OnPushContext(ITfContext* pic) override;
    STDMETHODIMP OnPopContext(ITfContext* pic) override;

    // ITfDisplayAttributeProvider methods
    STDMETHODIMP EnumDisplayAttributeInfo(IEnumTfDisplayAttributeInfo** ppEnum) override;
    STDMETHODIMP GetDisplayAttributeInfo(REFGUID guid, ITfDisplayAttributeInfo** ppInfo) override;

    // ITfCompositionSink methods
    STDMETHODIMP OnCompositionTerminated(TfEditCookie ecWrite, ITfComposition *pComposition) override;

    // ITfFunctionProvider methods
    STDMETHODIMP GetType(GUID* pguid) override;
    STDMETHODIMP GetDescription(BSTR* pbstrDesc) override;
    STDMETHODIMP GetFunction(REFGUID rguid, REFIID riid, IUnknown** ppunk) override;

    // ITfFunction methods (base of ITfFnReconversion)
    STDMETHODIMP GetDisplayName(BSTR* pbstrName) override;

    // ITfFnReconversion methods
    STDMETHODIMP QueryRange(ITfRange* pRange, ITfRange** ppNewRange, BOOL* pfConvertible) override;
    STDMETHODIMP GetReconversion(ITfRange* pRange, ITfCandidateList** ppCandList) override;
    STDMETHODIMP Reconvert(ITfRange* pRange) override;

    // ITfMouseSink methods
    STDMETHODIMP OnMouseEvent(ULONG uEdge, ULONG uQuadrant, DWORD dwBtnStatus, WINBOOL* pfEaten) override;

    // Composition management helpers (public so EditSession can access them)
    HRESULT StartComposition(TfEditCookie ec, ITfContext* pic, ITfRange* range);
    HRESULT EndComposition(TfEditCookie ec);
    HRESULT UpdateCompositionText(TfEditCookie ec, ITfContext* pic, ITfRange* range, const std::wstring& text);
    void CommitCompositionAsync(ITfContext* pic);
    void CommitCompositionSync(ITfContext* pic);

    // Get current engine reference
    core::Engine& GetEngine() noexcept { return engine_; }
    
    // Check if composition is active
    bool HasActiveComposition() const noexcept { return active_composition_.Get() != nullptr; }

    // Client ID getter
    TfClientId GetClientId() const noexcept { return client_id_; }

private:
    HRESULT InitKeySink();
    void UninitKeySink();
    HRESULT InitThreadMgrEventSink();
    void UninitThreadMgrEventSink();
    bool IsKeyFiltered(WPARAM wParam, LPARAM lParam) const noexcept;
    wchar_t TranslateKey(WPARAM wParam, LPARAM lParam) const;

    ULONG ref_count_ = 1;
    
    ComPtr<ITfThreadMgr> thread_mgr_;
    TfClientId client_id_ = 0;
    DWORD thread_mgr_cookie_ = 0;
    
    bool is_active_ = false;

    // Core Vietnamese IME state
    core::Engine engine_;
    ComPtr<ITfComposition> active_composition_;
    TfGuidAtom display_attribute_atom_ = 0;
    DWORD mouse_cookie_ = 0;
};

// Registration helper functions (defined in register.cpp)
HRESULT RegisterCOMServer(HINSTANCE hInst);
HRESULT UnregisterCOMServer();
HRESULT RegisterTSFProfile();
HRESULT UnregisterTSFProfile();

} // namespace vn_ime
