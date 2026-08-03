#pragma once

#include <windows.h>
#include <msctf.h>
#include <atomic>
#include <unordered_map>
#include <string>
#include <vector>
#include "com_ptr.hpp"
#include "class_factory.hpp"
#include "engine.hpp"
#include "commit_undo.hpp"

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

#ifndef __ITfCandidateString_INTERFACE_DEFINED__
#define __ITfCandidateString_INTERFACE_DEFINED__

inline constexpr IID IID_ITfCandidateString = {
    0x581f317e, 0xfd9d, 0x443f, { 0xb9, 0x72, 0xed, 0x00, 0x46, 0x7c, 0x5d, 0x40 }
};

MIDL_INTERFACE("581f317e-fd9d-443f-b972-ed00467c5d40")
ITfCandidateString : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE GetString(BSTR *pbstr) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetIndex(ULONG *pnIndex) = 0;
};

#endif

#ifndef __IEnumTfCandidates_INTERFACE_DEFINED__
#define __IEnumTfCandidates_INTERFACE_DEFINED__

inline constexpr IID IID_IEnumTfCandidates = {
    0xdefb1926, 0x6c80, 0x4ce8, { 0x87, 0xd4, 0xd6, 0xb7, 0x2b, 0x81, 0x2b, 0xde }
};

MIDL_INTERFACE("defb1926-6c80-4ce8-87d4-d6b72b812bde")
IEnumTfCandidates : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE Clone(IEnumTfCandidates **ppEnum) = 0;
    virtual HRESULT STDMETHODCALLTYPE Next(ULONG ulCount, ITfCandidateString **ppCand, ULONG *pcFetched) = 0;
    virtual HRESULT STDMETHODCALLTYPE Reset() = 0;
    virtual HRESULT STDMETHODCALLTYPE Skip(ULONG ulCount) = 0;
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

inline constexpr GUID GUID_PROP_INPUTSCOPE_LOCAL = {
    0x1713dd5a, 0x68e7, 0x4a5b, { 0x9a, 0xf6, 0x59, 0x2a, 0x59, 0x5c, 0x77, 0x8d }
};

inline constexpr IID IID_ITfInputScope_LOCAL = {
    0xfde1eaee, 0x6924, 0x4cdf, { 0x91, 0xe7, 0xda, 0x38, 0xcf, 0xf5, 0x55, 0x9d }
};

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
                      public ITfMouseSink,
                      public ITfTextEditSink {
    friend class EditSession;
public:
    enum class VisualStudioFocusKind {
        NotVisualStudio,
        ShellNativeSurface,
        TsfTextInput,
    };

    enum class NativeKeyReplayKind {
        CommitOnly,
        ReplayNativeKey,
    };

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
    STDMETHODIMP OnMouseEvent(ULONG uEdge, ULONG uQuadrant, DWORD dwBtnStatus, BOOL* pfEaten) override;

    // ITfTextEditSink methods
    STDMETHODIMP OnEndEdit(ITfContext* pic, TfEditCookie ecReadOnly, ITfEditRecord* pEditRecord) override;

    // Composition management helpers (public so EditSession can access them)
    HRESULT StartComposition(TfEditCookie ec, ITfContext* pic, ITfRange* range);
    HRESULT EndComposition(TfEditCookie ec);
    HRESULT UpdateCompositionText(TfEditCookie ec, ITfContext* pic, ITfRange* range, const std::wstring& text);
    void CommitCompositionAsync(ITfContext* pic, WORD replay_vk = 0);
    void CommitCompositionSync(ITfContext* pic, WORD replay_vk = 0);
    bool TryCommitCompositionSync(ITfContext* pic);
    void CommitActiveCompositionFromHook();
    void CommitAndReplayBrowserClick(UINT uMsg);
    void ReplayPendingMouseClick();
    void ClearSensitiveState(bool reset_composition) noexcept;
    HRESULT ReplaceDirectInlineText(TfEditCookie ec, ITfContext* pic, ITfRange* caret_range, const std::wstring& text, const std::wstring& old_text = L"", wchar_t ch = 0);
    void ResetDirectInlineState() noexcept;

    // Get current engine reference
    core::Engine& GetEngine() noexcept { return engine_; }
    
    // Check if composition is active
    bool HasActiveComposition() const noexcept { return active_composition_.Get() != nullptr; }

    // Client ID getter
    TfClientId GetClientId() const noexcept { return client_id_; }

    // Password field getter/setter
    void SetPasswordField(bool is_password) noexcept { is_password_field_ = is_password; }
    bool IsPasswordField() const noexcept { return is_password_field_; }
    bool IsSecureInputContext() const noexcept;
    bool HasDirectInlineState() const noexcept { return direct_inline_display_length_ > 0 || scintilla_direct_inline_byte_length_ > 0 || engine_.HasPendingRaw(); }
    bool IsInkscapeKeySuppressed(WPARAM wParam) const;
    bool IsBrowserProcess() const;
    bool replay_mouse_up_swallow_pending_ = false;

private:
    enum class KeyAction {
        PassThrough,
        ProcessChar,
        Backspace,
        CommitSpace,
        CommitChar,
        DirectProcessChar,
        DirectBackspace,
        DirectCommitSpace,
        DirectCommitChar,
        Reconvert,
        ExplorerEditReconvert,
        InkscapePostKey,
    };

    enum class CommitCaretPolicy {
        MoveToCompositionEnd,
        PreserveHostSelection,
    };

    enum class ExplorerFocusKind {
        NotExplorer,
        NativeSurface,
        Win32Edit,
        TsfTextInput,
    };


    struct KeyDecision {
        bool eat = false;
        bool is_modifier = false;
        bool commit_existing_before_host = false;
        bool clear_sensitive_before_host = false;
        bool replay_native_after_commit = false;
        bool fallback_to_direct_process_char = false;
        bool fallback_to_process_char = false;
        bool observe_excel_char_after_commit = false;
        KeyAction action = KeyAction::PassThrough;
        wchar_t ch = 0;
        wchar_t excel_observed_char = 0;
        WORD replay_vk = 0;
    };

    HRESULT InitKeySink();
    void UninitKeySink();
    HRESULT InitThreadMgrEventSink();
    void UninitThreadMgrEventSink();
    bool IsModifierKey(WPARAM wParam) const noexcept;
    void MarkExternalCaretMoved(const wchar_t* source) noexcept;
    KeyDecision MakeKeyDecision(ITfContext* pic, WPARAM wParam, LPARAM lParam);
    bool IsActiveCompositionSelectionAtEnd(ITfContext* pic, bool* known);
    bool FlushStaleCompositionBeforeKey(ITfContext* pic, const wchar_t* source);
    bool TryReconversion(ITfContext* pic, wchar_t ch, bool apply);
    bool IsKeyFiltered(WPARAM wParam, LPARAM lParam) const noexcept;
    bool IsCurrentAppBlocked(ITfContext* pic = nullptr) const;
    bool IsDirectCommitApp() const;
    bool IsNativeEnterReplayApp() const;
    NativeKeyReplayKind GetNativeKeyReplayKind(ITfContext* pic, WPARAM wParam);
    bool ContextHasNativeKeyReplayInputScope(ITfContext* pic);
    bool IsExcelApp() const;
    std::optional<core::ExcelFormulaInputKind> GetExcelFormulaInputKind(ITfContext* pic);
    core::ExcelFormulaSessionState GetExcelFormulaSessionState(ITfContext* pic) const;
    void PrepareExcelFormulaSession(ITfContext* pic, WPARAM wParam, LPARAM lParam);
    bool TryAdoptPendingExcelFormulaContext(ITfContext* pic);
    void ObserveExcelNativeChar(ITfContext* pic, WPARAM wParam, LPARAM lParam, const wchar_t* source);
    void ObserveExcelNativeChar(ITfContext* pic, wchar_t ch, const wchar_t* source);
    void SetExcelFormulaSessionState(ITfContext* pic, core::ExcelFormulaSessionState state, const wchar_t* source);
    void ResetExcelFormulaSession(const wchar_t* reason) noexcept;
    bool IsWordTsfInlineApp() const;
    bool IsWordTsfInlineActive() const;
    bool IsTelegramProcess() const;
    bool IsConsoleProcess() const;
    bool IsVisualStudioProcess() const;
    bool IsVisualStudioShellNativeSurfaceFocused(ITfContext* pic) const;
    VisualStudioFocusKind GetVisualStudioFocusKind(ITfContext* pic);
    bool IsExplorerProcess() const;
    bool IsExplorerWin32EditFocused() const;
    bool IsExplorerNativeSurfaceFocused(ITfContext* pic) const;
    bool ExplorerFocusedThreadHasCaret() const;
    bool ExplorerContextHasTextInputScope(ITfContext* pic);
    ExplorerFocusKind GetExplorerFocusKind(ITfContext* pic);
    bool IsNotepadPlusPlusDirectInlineFocused() const;
    bool HasNotepadPlusPlusNativeSelection() const;
    bool ProcessNotepadPlusPlusDirectChar(wchar_t ch);
    bool ProcessNotepadPlusPlusDirectBackspace();
    bool ProcessNotepadPlusPlusDirectCommitChar(wchar_t ch);
    bool ProcessWin32EditDirectChar(HWND hwnd, wchar_t ch);
    bool ProcessWin32EditDirectBackspace(HWND hwnd);
    bool ProcessWin32EditDirectCommitChar(HWND hwnd, wchar_t ch);
    bool ProcessScintillaDirectChar(HWND hwnd, wchar_t ch);
    bool ProcessScintillaDirectBackspace(HWND hwnd);
    bool ProcessScintillaDirectCommitChar(HWND hwnd, wchar_t ch);
    bool ProcessExplorerEditChar(wchar_t ch);
    bool ProcessExplorerEditBackspace();
    bool TryExplorerEditReconversion(wchar_t ch, bool apply);
    std::wstring GetFocusedProcessName() const;
    wchar_t TranslateKey(WPARAM wParam, LPARAM lParam) const;
    bool IsValidCompositionKey(WPARAM wParam, core::InputMethod method) const;
    void SendSyntheticNativeKey(WORD vk);
    bool IsTerminalApp() const;
    bool IsInkscapeApp() const;
    bool IsFakeBackspaceApp() const;
    bool ProcessFakeBackspaceEditChar(wchar_t ch);
    bool ProcessFakeBackspaceEditBackspace();
    bool ProcessInkscapeNonCompositionKey(WPARAM wParam, LPARAM lParam);
    void SendSyntheticUnicodeChar(wchar_t ch);
    void EnsureInkscapeSubclassed();


    ULONG ref_count_ = 1;
    
    ComPtr<ITfThreadMgr> thread_mgr_;
    TfClientId client_id_ = 0;
    DWORD thread_mgr_cookie_ = 0;
    
    bool is_active_ = false;
    bool is_password_field_ = false;

    // Core Vietnamese IME state
    core::Engine engine_;
    ComPtr<ITfComposition> active_composition_;
    TfGuidAtom display_attribute_atom_ = 0;
    DWORD mouse_cookie_ = 0;
    bool replay_mouse_click_pending_ = false;
    DWORD replay_mouse_down_flag_ = 0;
    DWORD replay_mouse_up_flag_ = 0;

    // Registry watching
    HANDLE registry_thread_ = nullptr;
    HANDLE registry_shutdown_event_ = nullptr;
    HANDLE registry_watch_event_ = nullptr;
    std::atomic<bool> config_changed_;
    bool enable_app_blocklist_ = false;
    std::vector<std::wstring> blocked_apps_;
    bool enable_auto_exclude_ = true;
    std::vector<std::wstring> auto_blocked_apps_;
    struct DirectAppConfig {
        std::wstring process_name;
        bool is_commit = false;
    };
    std::vector<DirectAppConfig> direct_apps_;
    bool IsCustomDirectApp(bool* is_commit = nullptr) const;
    bool activation_ready_for_auto_exclude_ = false;
    std::wstring host_process_name_;
    mutable DWORD cached_process_id_ = 0;
    mutable std::wstring cached_process_name_;
    DWORD typing_mode_ = 0;
    DWORD hotkey_mode_ = 0;
    bool ctrl_pressed_ = false;
    bool shift_pressed_ = false;
    bool other_key_pressed_ = false;
    bool config_loaded_once_ = false;
    size_t direct_inline_display_length_ = 0;
    size_t scintilla_direct_inline_byte_length_ = 0;
    size_t scintilla_direct_inline_start_ = 0;
    core::ExcelFormulaSessionState excel_formula_state_ = core::ExcelFormulaSessionState::Idle;
    ComPtr<IUnknown> excel_formula_context_identity_;
    bool excel_formula_observation_latched_ = false;
    WPARAM excel_formula_observation_vk_ = 0;
    CommitCaretPolicy pending_commit_caret_policy_ = CommitCaretPolicy::MoveToCompositionEnd;
    bool mouse_commit_pending_ = false;
    bool external_caret_moved_ = false;
    unsigned long long selection_generation_ = 0;
    unsigned long long composition_selection_generation_ = 0;
    std::vector<HWND> subclassed_hwnds_;
    HWND active_subclassed_hwnd_ = nullptr;
    HWND active_subclassed_root_hwnd_ = nullptr;
    WPARAM last_inkscape_commit_vk_ = 0;
    DWORD last_inkscape_commit_time_ = 0;
    bool is_updating_selection_ = false;
    bool composition_commit_pending_ = false;
    DWORD text_edit_cookie_ = 0;
    ComPtr<ITfContext> selection_context_;
    void UnadviseSelectionSink();

    // Commit undo support for Esc restore raw
    void CaptureCommitUndo(TfEditCookie ec, ITfContext* pic);
    void CaptureCommitUndoDirectInline(HWND hwnd, bool is_scintilla);
    bool TryRestoreLastCommittedRaw(TfEditCookie ec, ITfContext* pic);
    bool TryRestoreLastCommittedRawDirectInline(HWND hwnd, bool resume_after_boundary);
    bool TryProcessDirectCommitEsc(ITfContext* pic);
    void ClearLastCommitUndo() noexcept;

    std::optional<CommitUndoEntry> last_commit_undo_;


    static DWORD WINAPI RegistryWatchThreadProc(LPVOID lpParam);
    static LRESULT CALLBACK MouseHookSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
    void CheckAndReloadConfig();
    void ReloadConfig();

    void TrackHotkey(WPARAM wParam, LPARAM lParam, bool is_key_down, BOOL* pfEaten);
    void ToggleTypingMode();

    // Shorthand typing support
    std::unordered_map<std::wstring, std::wstring> shorthand_map_;
    void LoadShorthandRules();
    std::wstring LookUpShorthand(const std::wstring& shortcut);
};

// Registration helper functions (defined in register.cpp)
HRESULT RegisterCOMServer(HINSTANCE hInst);
HRESULT UnregisterCOMServer();
HRESULT RegisterTSFProfile();
HRESULT UnregisterTSFProfile();

} // namespace vn_ime
