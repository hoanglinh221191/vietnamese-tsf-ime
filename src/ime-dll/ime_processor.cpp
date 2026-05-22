#include "ime_processor.hpp"
#include "logger.hpp"

namespace vn_ime {

class VietnameseDisplayAttributeInfo : public ITfDisplayAttributeInfo {
public:
    VietnameseDisplayAttributeInfo() noexcept : ref_count_(1) {}
    virtual ~VietnameseDisplayAttributeInfo() noexcept = default;

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (riid == IID_IUnknown || riid == IID_ITfDisplayAttributeInfo) {
            *ppv = static_cast<ITfDisplayAttributeInfo*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    STDMETHODIMP_(ULONG) AddRef() override { return ++ref_count_; }
    STDMETHODIMP_(ULONG) Release() override {
        ULONG count = --ref_count_;
        if (count == 0) delete this;
        return count;
    }

    STDMETHODIMP GetGUID(GUID* pguid) override {
        if (!pguid) return E_INVALIDARG;
        *pguid = GUID_VietnameseDisplayAttribute;
        return S_OK;
    }

    STDMETHODIMP GetDescription(BSTR* pbstrDesc) override {
        if (!pbstrDesc) return E_INVALIDARG;
        *pbstrDesc = SysAllocString(L"Vietnamese IME Display Attribute");
        return *pbstrDesc ? S_OK : E_OUTOFMEMORY;
    }

    STDMETHODIMP GetAttributeInfo(TF_DISPLAYATTRIBUTE* pda) override {
        if (!pda) return E_INVALIDARG;
        pda->crText.type = TF_CT_NONE;
        pda->crBk.type = TF_CT_NONE;
        pda->lsStyle = TF_LS_DOT;
        pda->fBoldLine = FALSE;
        pda->crLine.type = TF_CT_NONE;
        pda->bAttr = TF_ATTR_INPUT;
        return S_OK;
    }

    STDMETHODIMP SetAttributeInfo(const TF_DISPLAYATTRIBUTE*) override { return E_NOTIMPL; }
    STDMETHODIMP Reset() override { return S_OK; }

private:
    ULONG ref_count_;
};

class VietnameseEnumDisplayAttributeInfo : public IEnumTfDisplayAttributeInfo {
public:
    VietnameseEnumDisplayAttributeInfo() noexcept : ref_count_(1) {}
    virtual ~VietnameseEnumDisplayAttributeInfo() noexcept = default;

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (riid == IID_IUnknown || riid == IID_IEnumTfDisplayAttributeInfo) {
            *ppv = static_cast<IEnumTfDisplayAttributeInfo*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    STDMETHODIMP_(ULONG) AddRef() override { return ++ref_count_; }
    STDMETHODIMP_(ULONG) Release() override {
        ULONG count = --ref_count_;
        if (count == 0) delete this;
        return count;
    }

    STDMETHODIMP Clone(IEnumTfDisplayAttributeInfo** ppEnum) override {
        if (!ppEnum) return E_INVALIDARG;
        *ppEnum = new (std::nothrow) VietnameseEnumDisplayAttributeInfo();
        return *ppEnum ? S_OK : E_OUTOFMEMORY;
    }

    STDMETHODIMP Next(ULONG celt, ITfDisplayAttributeInfo** rgelt, ULONG* pceltFetched) override {
        if (!rgelt || (!pceltFetched && celt != 1)) return E_INVALIDARG;
        if (pceltFetched) *pceltFetched = 0;
        
        if (celt > 0 && index_ == 0) {
            rgelt[0] = new (std::nothrow) VietnameseDisplayAttributeInfo();
            if (!rgelt[0]) return E_OUTOFMEMORY;
            index_ = 1;
            if (pceltFetched) *pceltFetched = 1;
            return S_OK;
        }
        return S_FALSE;
    }

    STDMETHODIMP Reset() override {
        index_ = 0;
        return S_OK;
    }

    STDMETHODIMP Skip(ULONG celt) override {
        index_ += celt;
        if (index_ > 1) {
            index_ = 1;
            return S_FALSE;
        }
        return S_OK;
    }

private:
    ULONG ref_count_;
    ULONG index_ = 0;
};

enum class EditAction {
    ProcessChar,
    Backspace,
    Commit,
};

class EditSession : public ITfEditSession {
public:
    EditSession(VietnameseIME* ime, ITfContext* pic, EditAction action, wchar_t ch = 0) noexcept
        : ime_(ime), pic_(pic), action_(action), ch_(ch), ref_count_(1) {
        if (ime_) ime_->AddRef();
        if (pic_) pic_->AddRef();
    }

    virtual ~EditSession() noexcept {
        if (ime_) ime_->Release();
        if (pic_) pic_->Release();
    }

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (riid == IID_IUnknown || riid == IID_ITfEditSession) {
            *ppv = static_cast<ITfEditSession*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    STDMETHODIMP_(ULONG) AddRef() override { return ++ref_count_; }
    STDMETHODIMP_(ULONG) Release() override {
        ULONG count = --ref_count_;
        if (count == 0) delete this;
        return count;
    }

    STDMETHODIMP DoEditSession(TfEditCookie ec) override {
        logger::Log(logger::Level::Info, L"EditSession::DoEditSession started");
        if (!ime_ || !pic_) {
            logger::Log(logger::Level::Error, L"EditSession: ime_ or pic_ is null");
            return E_FAIL;
        }
        
        ComPtr<ITfRange> range;
        TF_SELECTION sel;
        ULONG fetched = 0;
        
        HRESULT hr = pic_->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &sel, &fetched);
        logger::LogFormat(logger::Level::Info, L"GetSelection returned hr = 0x%08X, fetched = %u", hr, fetched);
        if (FAILED(hr) || fetched == 0) {
            logger::Log(logger::Level::Error, L"GetSelection failed or returned fetched = 0");
            return E_FAIL;
        }
        range.Attach(sel.range);
        
        if (action_ == EditAction::ProcessChar) {
            logger::LogFormat(logger::Level::Info, L"EditAction::ProcessChar: ch = '%c' (0x%04X)", ch_, ch_);
            if (!ime_->HasActiveComposition()) {
                logger::Log(logger::Level::Info, L"No active composition, starting new one");
                ime_->GetEngine().Clear();
                ime_->GetEngine().ProcessKey(ch_);
                std::wstring disp = ime_->GetEngine().GetDisplayString();
                logger::LogFormat(logger::Level::Info, L"Engine display string: %s", disp.c_str());
                
                HRESULT hrComp = ime_->StartComposition(ec, pic_, range.Get());
                logger::LogFormat(logger::Level::Info, L"StartComposition returned hr = 0x%08X", hrComp);
                if (SUCCEEDED(hrComp)) {
                    HRESULT hrUpdate = ime_->UpdateCompositionText(ec, pic_, range.Get(), disp);
                    logger::LogFormat(logger::Level::Info, L"UpdateCompositionText returned hr = 0x%08X", hrUpdate);
                }
            } else {
                logger::Log(logger::Level::Info, L"Active composition exists, updating");
                ime_->GetEngine().ProcessKey(ch_);
                std::wstring disp = ime_->GetEngine().GetDisplayString();
                logger::LogFormat(logger::Level::Info, L"Engine display string: %s", disp.c_str());
                HRESULT hrUpdate = ime_->UpdateCompositionText(ec, pic_, range.Get(), disp);
                logger::LogFormat(logger::Level::Info, L"UpdateCompositionText returned hr = 0x%08X", hrUpdate);
            }
        }
        else if (action_ == EditAction::Backspace) {
            logger::Log(logger::Level::Info, L"EditAction::Backspace");
            if (ime_->HasActiveComposition()) {
                ime_->GetEngine().Backspace();
                std::wstring disp = ime_->GetEngine().GetDisplayString();
                std::wstring raw = ime_->GetEngine().GetRawString();
                logger::LogFormat(logger::Level::Info, L"Backspace: raw empty? %s, disp: %s", raw.empty() ? L"YES" : L"NO", disp.c_str());
                if (raw.empty()) {
                    ime_->UpdateCompositionText(ec, pic_, range.Get(), L"");
                    ime_->EndComposition(ec);
                } else {
                    ime_->UpdateCompositionText(ec, pic_, range.Get(), disp);
                }
            }
        }
        else if (action_ == EditAction::Commit) {
            logger::LogFormat(logger::Level::Info, L"EditAction::Commit: ch = '%c' (0x%04X)", ch_, ch_);
            if (ime_->HasActiveComposition()) {
                HRESULT hrEnd = ime_->EndComposition(ec);
                logger::LogFormat(logger::Level::Info, L"EndComposition returned hr = 0x%08X", hrEnd);
            }
            if (ch_ != 0) {
                ComPtr<ITfRange> current_range;
                TF_SELECTION current_sel;
                ULONG current_fetched = 0;
                HRESULT hrSel = pic_->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &current_sel, &current_fetched);
                logger::LogFormat(logger::Level::Info, L"Commit GetSelection returned hr = 0x%08X, fetched = %u", hrSel, current_fetched);
                if (SUCCEEDED(hrSel) && current_fetched > 0) {
                    current_range.Attach(current_sel.range);
                    wchar_t delim[2] = { ch_, L'\0' };
                    HRESULT hrText = current_range->SetText(ec, 0, delim, 1);
                    logger::LogFormat(logger::Level::Info, L"Commit SetText returned hr = 0x%08X", hrText);
                    
                    current_range->Collapse(ec, TF_ANCHOR_END);
                    current_sel.range = current_range.Get();
                    current_sel.style.ase = TF_AE_NONE;
                    current_sel.style.fInterimChar = FALSE;
                    HRESULT hrSetSel = pic_->SetSelection(ec, 1, &current_sel);
                    logger::LogFormat(logger::Level::Info, L"Commit SetSelection returned hr = 0x%08X", hrSetSel);
                }
            }
        }
        
        return S_OK;
    }

private:
    VietnameseIME* ime_;
    ITfContext* pic_;
    EditAction action_;
    wchar_t ch_;
    ULONG ref_count_;
};

// Creator function used by ClassFactory
HRESULT CreateInstance_VietnameseIME(IUnknown* outer, REFIID riid, void** ppv) {
    if (outer) return CLASS_E_NOAGGREGATION;
    
    VietnameseIME* ime = new (std::nothrow) VietnameseIME();
    if (!ime) return E_OUTOFMEMORY;
    
    HRESULT hr = ime->QueryInterface(riid, ppv);
    ime->Release(); // Balance the constructor ref count (starts at 1)
    return hr;
}

VietnameseIME::VietnameseIME() noexcept {
    ClassFactory::IncrementActiveObjects();
}

VietnameseIME::~VietnameseIME() noexcept {
    ClassFactory::DecrementActiveObjects();
}

// IUnknown Implementation
STDMETHODIMP VietnameseIME::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;

    if (riid == IID_IUnknown || riid == IID_ITfTextInputProcessor) {
        *ppv = static_cast<ITfTextInputProcessor*>(this);
    } else if (riid == IID_ITfTextInputProcessorEx) {
        *ppv = static_cast<ITfTextInputProcessorEx*>(this);
    } else if (riid == IID_ITfKeyEventSink) {
        *ppv = static_cast<ITfKeyEventSink*>(this);
    } else if (riid == IID_ITfThreadMgrEventSink) {
        *ppv = static_cast<ITfThreadMgrEventSink*>(this);
    } else if (riid == IID_ITfDisplayAttributeProvider) {
        *ppv = static_cast<ITfDisplayAttributeProvider*>(this);
    } else if (riid == IID_ITfCompositionSink) {
        *ppv = static_cast<ITfCompositionSink*>(this);
    } else {
        return E_NOINTERFACE;
    }

    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) VietnameseIME::AddRef() {
    return ++ref_count_;
}

STDMETHODIMP_(ULONG) VietnameseIME::Release() {
    ULONG count = --ref_count_;
    if (count == 0) {
        delete this;
    }
    return count;
}

// ITfTextInputProcessor Implementation
STDMETHODIMP VietnameseIME::Activate(ITfThreadMgr* ptm, TfClientId tid) {
    return ActivateEx(ptm, tid, 0);
}

STDMETHODIMP VietnameseIME::Deactivate() {
    logger::Log(logger::Level::Info, L"VietnameseIME::Deactivate called.");
    if (!is_active_) return S_OK;
    
    if (active_composition_) {
        active_composition_.Reset();
    }
    engine_.Clear();
    display_attribute_atom_ = 0;

    UninitThreadMgrEventSink();
    UninitKeySink();
    
    thread_mgr_.Reset();
    client_id_ = 0;
    is_active_ = false;
    
    logger::Log(logger::Level::Info, L"VietnameseIME::Deactivate succeeded.");
    return S_OK;
}

// ITfTextInputProcessorEx Implementation
STDMETHODIMP VietnameseIME::ActivateEx(ITfThreadMgr* ptm, TfClientId tid, [[maybe_unused]] DWORD dwFlags) {
    logger::LogFormat(logger::Level::Info, L"VietnameseIME::ActivateEx called. tid = %d, dwFlags = %u", tid, dwFlags);
    if (is_active_) return S_OK;
    if (!ptm) return E_INVALIDARG;

    thread_mgr_ = ComPtr<ITfThreadMgr>(ptm);
    client_id_ = tid;
    is_active_ = true;

    HRESULT hr = InitKeySink();
    if (FAILED(hr)) {
        logger::LogFormat(logger::Level::Error, L"InitKeySink failed. hr = 0x%08X", hr);
        Deactivate();
        return hr;
    }

    hr = InitThreadMgrEventSink();
    if (FAILED(hr)) {
        logger::LogFormat(logger::Level::Error, L"InitThreadMgrEventSink failed. hr = 0x%08X", hr);
        Deactivate();
        return hr;
    }

    // Register Display Attribute GUID to get an atom
    ComPtr<ITfCategoryMgr> category_mgr;
    if (SUCCEEDED(CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER, IID_ITfCategoryMgr, reinterpret_cast<void**>(category_mgr.GetAddressOf())))) {
        category_mgr->RegisterGUID(GUID_VietnameseDisplayAttribute, &display_attribute_atom_);
    }

    logger::Log(logger::Level::Info, L"VietnameseIME::ActivateEx succeeded.");
    return S_OK;
}

// ITfKeyEventSink Implementation
STDMETHODIMP VietnameseIME::OnSetFocus(BOOL fForeground) {
    logger::LogFormat(logger::Level::Info, L"OnSetFocus called: fForeground = %s", fForeground ? L"TRUE" : L"FALSE");
    if (!fForeground && thread_mgr_) {
        ComPtr<ITfDocumentMgr> doc_mgr;
        if (SUCCEEDED(thread_mgr_->GetFocus(doc_mgr.GetAddressOf())) && doc_mgr) {
            ComPtr<ITfContext> context;
            if (SUCCEEDED(doc_mgr->GetTop(context.GetAddressOf())) && context) {
                CommitCompositionAsync(context.Get());
            }
        }
    }
    return S_OK;
}

STDMETHODIMP VietnameseIME::OnTestKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) {
    if (!pfEaten) return E_INVALIDARG;

    bool eat = IsKeyFiltered(wParam, lParam);
    
    if (!eat && active_composition_) {
        // If the key is not filtered but we have an active composition,
        // we should commit the composition for any key that is not a modifier key.
        // This ensures punctuation (like commas, periods) or editor navigation keys
        // commit the current word, so subsequent operations (like Backspace) are handled correctly.
        bool is_modifier = (wParam == VK_SHIFT || wParam == VK_CONTROL || wParam == VK_MENU || 
                            wParam == VK_LWIN || wParam == VK_RWIN || wParam == VK_CAPITAL || 
                            wParam == VK_NUMLOCK || wParam == VK_SCROLL ||
                            wParam == VK_LSHIFT || wParam == VK_RSHIFT || 
                            wParam == VK_LCONTROL || wParam == VK_RCONTROL || 
                            wParam == VK_LMENU || wParam == VK_RMENU);
        if (!is_modifier) {
            CommitCompositionSync(pic);
            eat = false;
        }
    }

    *pfEaten = eat ? TRUE : FALSE;

    logger::LogFormat(logger::Level::Debug, L"OnTestKeyDown: wParam = 0x%02X, lParam = 0x%08X, eaten = %s",
                      wParam, lParam, eat ? L"TRUE" : L"FALSE");

    return S_OK;
}

STDMETHODIMP VietnameseIME::OnKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) {
    if (!pfEaten) return E_INVALIDARG;

    bool eat = IsKeyFiltered(wParam, lParam);
    *pfEaten = eat ? TRUE : FALSE;

    if (eat) {
        logger::LogFormat(logger::Level::Info, L"OnKeyDown (EATEN): wParam = 0x%02X", wParam);
        
        if (wParam == VK_BACK) {
            ComPtr<ITfEditSession> session;
            session.Attach(new (std::nothrow) EditSession(this, pic, EditAction::Backspace));
            if (session) {
                HRESULT hr = 0;
                HRESULT hrReq = pic->RequestEditSession(client_id_, session.Get(), TF_ES_SYNC | TF_ES_READWRITE, &hr);
                logger::LogFormat(logger::Level::Info, L"RequestEditSession (Backspace) returned hrReq = 0x%08X, hr = 0x%08X", hrReq, hr);
            }
        } else if (wParam == VK_SPACE || wParam == VK_RETURN) {
            wchar_t ch = (wParam == VK_SPACE) ? L' ' : L'\n';
            ComPtr<ITfEditSession> session;
            session.Attach(new (std::nothrow) EditSession(this, pic, EditAction::Commit, ch));
            if (session) {
                HRESULT hr = 0;
                HRESULT hrReq = pic->RequestEditSession(client_id_, session.Get(), TF_ES_SYNC | TF_ES_READWRITE, &hr);
                logger::LogFormat(logger::Level::Info, L"RequestEditSession (Commit) returned hrReq = 0x%08X, hr = 0x%08X", hrReq, hr);
            }
        } else {
            wchar_t ch = 0;
            BYTE keyboardState[256];
            if (GetKeyboardState(keyboardState)) {
                wchar_t buf[4] = {0};
                UINT scanCode = (lParam >> 16) & 0xFF;
                int count = ToUnicode(static_cast<UINT>(wParam), scanCode, keyboardState, buf, 4, 0);
                if (count > 0) {
                    ch = buf[0];
                }
                logger::LogFormat(logger::Level::Info, L"ToUnicode translated wParam 0x%02X to char '%c' (0x%04X), count = %d", wParam, ch ? ch : L'?', ch, count);
            } else {
                logger::Log(logger::Level::Warning, L"GetKeyboardState failed in OnKeyDown");
            }
            if (ch != 0) {
                ComPtr<ITfEditSession> session;
                session.Attach(new (std::nothrow) EditSession(this, pic, EditAction::ProcessChar, ch));
                if (session) {
                    HRESULT hr = 0;
                    HRESULT hrReq = pic->RequestEditSession(client_id_, session.Get(), TF_ES_SYNC | TF_ES_READWRITE, &hr);
                    logger::LogFormat(logger::Level::Info, L"RequestEditSession (ProcessChar) returned hrReq = 0x%08X, hr = 0x%08X", hrReq, hr);
                }
            } else {
                logger::Log(logger::Level::Warning, L"Char is 0, skipping EditSession request");
            }
        }
    } else {
        logger::LogFormat(logger::Level::Debug, L"OnKeyDown (PASSED): wParam = 0x%02X", wParam);
    }

    return S_OK;
}

STDMETHODIMP VietnameseIME::OnTestKeyUp([[maybe_unused]] ITfContext* pic, [[maybe_unused]] WPARAM wParam, [[maybe_unused]] LPARAM lParam, BOOL* pfEaten) {
    if (!pfEaten) return E_INVALIDARG;
    *pfEaten = FALSE;
    return S_OK;
}

STDMETHODIMP VietnameseIME::OnKeyUp([[maybe_unused]] ITfContext* pic, [[maybe_unused]] WPARAM wParam, [[maybe_unused]] LPARAM lParam, BOOL* pfEaten) {
    if (!pfEaten) return E_INVALIDARG;
    *pfEaten = FALSE;
    return S_OK;
}

STDMETHODIMP VietnameseIME::OnPreservedKey([[maybe_unused]] ITfContext* pic, [[maybe_unused]] REFGUID rguid, BOOL* pfEaten) {
    if (!pfEaten) return E_INVALIDARG;
    *pfEaten = FALSE;
    return S_OK;
}

bool VietnameseIME::IsKeyFiltered(WPARAM wParam, [[maybe_unused]] LPARAM lParam) const noexcept {
    if ((GetKeyState(VK_CONTROL) & 0x8000) != 0 ||
        (GetKeyState(VK_MENU) & 0x8000) != 0 ||
        (GetKeyState(VK_LWIN) & 0x8000) != 0 ||
        (GetKeyState(VK_RWIN) & 0x8000) != 0) {
        return false;
    }

    if ((wParam >= 0x41 && wParam <= 0x5A) ||
        (wParam >= 0x30 && wParam <= 0x39)) {
        return true;
    }

    if (active_composition_) {
        if (wParam == VK_BACK) {
            return true;
        }
    }

    return false;
}

// ITfThreadMgrEventSink Implementation
STDMETHODIMP VietnameseIME::OnInitDocumentMgr([[maybe_unused]] ITfDocumentMgr* pdm) {
    return S_OK;
}

STDMETHODIMP VietnameseIME::OnUninitDocumentMgr([[maybe_unused]] ITfDocumentMgr* pdm) {
    return S_OK;
}

STDMETHODIMP VietnameseIME::OnSetFocus([[maybe_unused]] ITfDocumentMgr* pdmFocus, ITfDocumentMgr* pdmPrevFocus) {
    logger::Log(logger::Level::Info, L"OnSetFocus (ITfDocumentMgr) called.");
    if (pdmPrevFocus) {
        ComPtr<ITfContext> context;
        if (SUCCEEDED(pdmPrevFocus->GetTop(context.GetAddressOf())) && context) {
            CommitCompositionAsync(context.Get());
        }
    }
    return S_OK;
}

STDMETHODIMP VietnameseIME::OnPushContext([[maybe_unused]] ITfContext* pic) {
    return S_OK;
}

STDMETHODIMP VietnameseIME::OnPopContext([[maybe_unused]] ITfContext* pic) {
    return S_OK;
}

// Helper methods for event sinks
HRESULT VietnameseIME::InitKeySink() {
    logger::Log(logger::Level::Info, L"VietnameseIME::InitKeySink called.");
    ComPtr<ITfKeystrokeMgr> keystroke_mgr;
    HRESULT hr = thread_mgr_.As(keystroke_mgr);
    if (FAILED(hr)) {
        logger::LogFormat(logger::Level::Error, L"Failed to query ITfKeystrokeMgr. hr = 0x%08X", hr);
        return hr;
    }

    hr = keystroke_mgr->AdviseKeyEventSink(client_id_, this, TRUE);
    if (FAILED(hr)) {
        logger::LogFormat(logger::Level::Error, L"AdviseKeyEventSink failed. hr = 0x%08X", hr);
        return hr;
    }
    logger::Log(logger::Level::Info, L"VietnameseIME::InitKeySink succeeded.");
    return S_OK;
}

void VietnameseIME::UninitKeySink() {
    logger::Log(logger::Level::Info, L"VietnameseIME::UninitKeySink called.");
    ComPtr<ITfKeystrokeMgr> keystroke_mgr;
    if (SUCCEEDED(thread_mgr_.As(keystroke_mgr))) {
        HRESULT hr = keystroke_mgr->UnadviseKeyEventSink(client_id_);
        logger::LogFormat(logger::Level::Info, L"UnadviseKeyEventSink returned. hr = 0x%08X", hr);
    }
}

HRESULT VietnameseIME::InitThreadMgrEventSink() {
    logger::Log(logger::Level::Info, L"VietnameseIME::InitThreadMgrEventSink called.");
    ComPtr<ITfSource> source;
    HRESULT hr = thread_mgr_.As(source);
    if (FAILED(hr)) {
        logger::LogFormat(logger::Level::Error, L"Failed to query ITfSource for ThreadMgrEventSink. hr = 0x%08X", hr);
        return hr;
    }

    hr = source->AdviseSink(IID_ITfThreadMgrEventSink, static_cast<ITfThreadMgrEventSink*>(this), &thread_mgr_cookie_);
    if (FAILED(hr)) {
        logger::LogFormat(logger::Level::Error, L"AdviseSink failed for ThreadMgrEventSink. hr = 0x%08X", hr);
        return hr;
    }
    logger::LogFormat(logger::Level::Info, L"VietnameseIME::InitThreadMgrEventSink succeeded. cookie = %u", thread_mgr_cookie_);
    return S_OK;
}

void VietnameseIME::UninitThreadMgrEventSink() {
    logger::Log(logger::Level::Info, L"VietnameseIME::UninitThreadMgrEventSink called.");
    ComPtr<ITfSource> source;
    if (thread_mgr_cookie_ != 0 && SUCCEEDED(thread_mgr_.As(source))) {
        HRESULT hr = source->UnadviseSink(thread_mgr_cookie_);
        logger::LogFormat(logger::Level::Info, L"UnadviseSink returned. hr = 0x%08X", hr);
        thread_mgr_cookie_ = 0;
    }
}

// ITfDisplayAttributeProvider implementation
STDMETHODIMP VietnameseIME::EnumDisplayAttributeInfo(IEnumTfDisplayAttributeInfo** ppEnum) {
    if (!ppEnum) return E_INVALIDARG;
    *ppEnum = new (std::nothrow) VietnameseEnumDisplayAttributeInfo();
    return *ppEnum ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP VietnameseIME::GetDisplayAttributeInfo(REFGUID guid, ITfDisplayAttributeInfo** ppInfo) {
    if (!ppInfo) return E_INVALIDARG;
    *ppInfo = nullptr;
    if (guid == GUID_VietnameseDisplayAttribute) {
        *ppInfo = new (std::nothrow) VietnameseDisplayAttributeInfo();
        return *ppInfo ? S_OK : E_OUTOFMEMORY;
    }
    return E_INVALIDARG;
}

STDMETHODIMP VietnameseIME::OnCompositionTerminated([[maybe_unused]] TfEditCookie ecWrite, [[maybe_unused]] ITfComposition *pComposition) {
    logger::Log(logger::Level::Info, L"OnCompositionTerminated called");
    active_composition_.Reset();
    engine_.Clear();
    return S_OK;
}

// Composition management helper methods
HRESULT VietnameseIME::StartComposition(TfEditCookie ec, ITfContext* pic, ITfRange* range) {
    if (!range) return E_INVALIDARG;

    ComPtr<ITfContextComposition> context_comp;
    HRESULT hr = pic->QueryInterface(IID_ITfContextComposition, reinterpret_cast<void**>(context_comp.GetAddressOf()));
    logger::LogFormat(logger::Level::Info, L"StartComposition: QueryInterface(IID_ITfContextComposition) returned hr = 0x%08X", hr);
    if (FAILED(hr)) return hr;
    
    ComPtr<ITfRange> cloned_range;
    hr = range->Clone(cloned_range.GetAddressOf());
    logger::LogFormat(logger::Level::Info, L"StartComposition: range->Clone returned hr = 0x%08X", hr);
    if (FAILED(hr)) return hr;

    hr = context_comp->StartComposition(ec, cloned_range.Get(), static_cast<ITfCompositionSink*>(this), active_composition_.ReleaseAndGetAddressOf());
    logger::LogFormat(logger::Level::Info, L"StartComposition: context_comp->StartComposition returned hr = 0x%08X", hr);
    return hr;
}

HRESULT VietnameseIME::EndComposition(TfEditCookie ec) {
    if (!active_composition_) return S_OK;
    HRESULT hr = active_composition_->EndComposition(ec);
    active_composition_.Reset();
    engine_.Clear();
    return hr;
}

HRESULT VietnameseIME::UpdateCompositionText(TfEditCookie ec, ITfContext* pic, ITfRange* range, const std::wstring& text) {
    logger::LogFormat(logger::Level::Info, L"UpdateCompositionText called: text = '%s'", text.c_str());
    if (!active_composition_) {
        logger::Log(logger::Level::Error, L"UpdateCompositionText: active_composition_ is null");
        return E_FAIL;
    }
    
    ComPtr<ITfRange> comp_range;
    HRESULT hr = active_composition_->GetRange(comp_range.GetAddressOf());
    logger::LogFormat(logger::Level::Info, L"UpdateCompositionText: active_composition_->GetRange returned hr = 0x%08X", hr);
    if (FAILED(hr)) return hr;
    
    hr = comp_range->SetText(ec, 0, text.c_str(), static_cast<LONG>(text.length()));
    logger::LogFormat(logger::Level::Info, L"UpdateCompositionText: comp_range->SetText returned hr = 0x%08X", hr);
    if (FAILED(hr)) return hr;
    
    if (display_attribute_atom_ != 0) {
        ComPtr<ITfProperty> prop;
        if (SUCCEEDED(pic->GetProperty(GUID_PROP_ATTRIBUTE, prop.GetAddressOf()))) {
            VARIANT var;
            var.vt = VT_I4;
            var.lVal = static_cast<LONG>(display_attribute_atom_);
            HRESULT hrProp = prop->SetValue(ec, comp_range.Get(), &var);
            logger::LogFormat(logger::Level::Info, L"UpdateCompositionText: prop->SetValue returned hr = 0x%08X", hrProp);
        } else {
            logger::Log(logger::Level::Warning, L"UpdateCompositionText: GetProperty(GUID_PROP_ATTRIBUTE) failed");
        }
    } else {
        logger::Log(logger::Level::Warning, L"UpdateCompositionText: display_attribute_atom_ is 0");
    }
    
    ComPtr<ITfRange> end_range;
    hr = comp_range->Clone(end_range.GetAddressOf());
    if (SUCCEEDED(hr)) {
        end_range->Collapse(ec, TF_ANCHOR_END);
        TF_SELECTION sel;
        sel.range = end_range.Get();
        sel.style.ase = TF_AE_NONE;
        sel.style.fInterimChar = FALSE;
        HRESULT hrSetSel = pic->SetSelection(ec, 1, &sel);
        logger::LogFormat(logger::Level::Info, L"UpdateCompositionText: pic->SetSelection (caret update) returned hr = 0x%08X", hrSetSel);
    } else {
        logger::LogFormat(logger::Level::Warning, L"UpdateCompositionText: Clone returned hr = 0x%08X", hr);
    }
    
    return S_OK;
}

void VietnameseIME::CommitCompositionAsync(ITfContext* pic) {
    if (!active_composition_) return;
    
    ComPtr<ITfEditSession> session;
    session.Attach(new (std::nothrow) EditSession(this, pic, EditAction::Commit));
    if (session) {
        HRESULT hr = 0;
        pic->RequestEditSession(client_id_, session.Get(), TF_ES_ASYNCDONTCARE | TF_ES_READWRITE, &hr);
    }
}

void VietnameseIME::CommitCompositionSync(ITfContext* pic) {
    if (!active_composition_) return;
    
    ComPtr<ITfEditSession> session;
    session.Attach(new (std::nothrow) EditSession(this, pic, EditAction::Commit));
    if (session) {
        HRESULT hr = 0;
        pic->RequestEditSession(client_id_, session.Get(), TF_ES_SYNC | TF_ES_READWRITE, &hr);
    }
}

} // namespace vn_ime
