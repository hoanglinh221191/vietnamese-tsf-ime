#include "ime_processor.hpp"
#include "logger.hpp"
#include "rules.hpp"
#include "speller.hpp"
#include "config.hpp"
#include <inputscope.h>
#include <vector>


extern HINSTANCE g_hInst;

namespace vn_ime {

namespace {

bool IsLowerChar(wchar_t c) {
    return c >= L'a' && c <= L'z';
}

bool IsUpperChar(wchar_t c) {
    return c >= L'A' && c <= L'Z';
}

bool IsValidCompositionChar(wchar_t ch, core::InputMethod method) {
    if (method == core::InputMethod::Telex || method == core::InputMethod::SimpleTelex) {
        return (ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z');
    } else if (method == core::InputMethod::VNI) {
        return (ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') || (ch >= L'0' && ch <= L'9');
    }
    return false;
}

bool IsReconversionKey(wchar_t ch, core::InputMethod method) {
    if (core::rules::IsToneKey(ch, method)) return true;
    wchar_t lch = core::rules::ToLower(ch);
    if (method == core::InputMethod::Telex || method == core::InputMethod::SimpleTelex) {
        return (lch == L'w');
    } else if (method == core::InputMethod::VNI) {
        return (lch == L'6' || lch == L'7' || lch == L'8' || lch == L'9');
    }
    return false;
}

bool ShouldAutoCapitalizeAtRange(TfEditCookie ec, ITfRange* range) {
    if (!range) return false;

    ComPtr<ITfRange> context_range;
    if (FAILED(range->Clone(context_range.GetAddressOf())) || !context_range) {
        return false;
    }

    context_range->Collapse(ec, TF_ANCHOR_START);
    LONG shifted = 0;
    context_range->ShiftStart(ec, -20, &shifted, nullptr);
    if (shifted == 0) {
        return false;
    }

    wchar_t buf[32] = {0};
    ULONG fetched = 0;
    if (FAILED(context_range->GetText(ec, 0, buf, 31, &fetched)) || fetched == 0) {
        return false;
    }

    std::wstring_view preceding_text(buf, fetched);
    size_t last_non_space = preceding_text.find_last_not_of(L" \t\r\n");
    if (last_non_space == std::wstring_view::npos) {
        return false;
    }

    wchar_t end_char = preceding_text[last_non_space];
    return end_char == L'.' || end_char == L'?' || end_char == L'!';
}

bool ShouldAutoCapitalizeAtFocusedControl() {
    HWND hwnd = ::GetFocus();
    if (!hwnd) return false;

    wchar_t class_name[64] = {0};
    if (::GetClassNameW(hwnd, class_name, 64) == 0) {
        return false;
    }

    // Notepad++ edits text through Scintilla. Some Scintilla/TSF paths do not
    // expose preceding text reliably via ITfRange, so read the byte before caret
    // through Scintilla's native messages. This only checks ASCII punctuation.
    if (_wcsicmp(class_name, L"Scintilla") == 0) {
        constexpr UINT SCI_GETCHARAT = 2007;
        constexpr UINT SCI_GETCURRENTPOS = 2008;

        LRESULT pos = ::SendMessageW(hwnd, SCI_GETCURRENTPOS, 0, 0);
        if (pos <= 0) return false;

        for (LRESULT i = pos - 1; i >= 0 && i >= pos - 32; --i) {
            LRESULT ch = ::SendMessageW(hwnd, SCI_GETCHARAT, static_cast<WPARAM>(i), 0);
            if (ch == 0) break;
            if (ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L'\n') {
                continue;
            }
            return ch == L'.' || ch == L'?' || ch == L'!';
        }
    }

    return false;
}

} // namespace


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
    ReconvertTest,
    Reconvert,
    CheckPassword,
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

    bool is_convertible() const noexcept { return is_convertible_; }
    const std::wstring& get_result_text() const noexcept { return result_text_; }

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

        if (action_ == EditAction::CheckPassword) {
            logger::Log(logger::Level::Info, L"EditSession: executing CheckPassword...");
            ime_->SetPasswordField(false);
            ComPtr<ITfReadOnlyProperty> prop;
            if (SUCCEEDED(pic_->GetAppProperty(GUID_PROP_INPUTSCOPE_LOCAL, prop.GetAddressOf())) && prop) {
                ComPtr<ITfRange> range;
                TF_SELECTION sel;
                ULONG fetched = 0;
                if (SUCCEEDED(pic_->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &sel, &fetched)) && fetched > 0) {
                    range.Attach(sel.range);
                } else {
                    ComPtr<ITfRange> start_range;
                    if (SUCCEEDED(pic_->GetStart(ec, start_range.GetAddressOf()))) {
                        range = start_range;
                    }
                }

                if (range) {
                    VARIANT var;
                    VariantInit(&var);
                    if (SUCCEEDED(prop->GetValue(ec, range.Get(), &var))) {
                        if (var.vt == VT_UNKNOWN && var.punkVal != nullptr) {
                            ComPtr<ITfInputScope> input_scope;
                            if (SUCCEEDED(var.punkVal->QueryInterface(IID_ITfInputScope_LOCAL, reinterpret_cast<void**>(input_scope.GetAddressOf())))) {
                                InputScope* scopes = nullptr;
                                UINT count = 0;
                                if (SUCCEEDED(input_scope->GetInputScopes(&scopes, &count)) && scopes) {
                                    for (UINT i = 0; i < count; ++i) {
                                        logger::LogFormat(logger::Level::Info, L"Found InputScope: %u", static_cast<unsigned int>(scopes[i]));
                                        if (scopes[i] == IS_PASSWORD || 
                                            scopes[i] == IS_NUMERIC_PASSWORD ||
                                            scopes[i] == IS_NUMERIC_PIN ||
                                            scopes[i] == IS_ALPHANUMERIC_PIN ||
                                            scopes[i] == IS_ALPHANUMERIC_PIN_SET) {
                                            logger::Log(logger::Level::Info, L"Password field detected via InputScope!");
                                            ime_->SetPasswordField(true);
                                            break;
                                        }
                                    }
                                    ::CoTaskMemFree(scopes);
                                }
                            }
                        }
                        VariantClear(&var);
                    }
                }
            }
            return S_OK;
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
                IMEConfig config = LoadConfigFromRegistry();
                if (config.enable_auto_capitalize &&
                    (ShouldAutoCapitalizeAtRange(ec, range.Get()) || ShouldAutoCapitalizeAtFocusedControl())) {
                    ch_ = core::rules::ToUpper(ch_);
                    logger::Log(logger::Level::Info, L"Auto-capitalized first composition key");
                }
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
        else if (action_ == EditAction::ReconvertTest || action_ == EditAction::Reconvert) {
            logger::LogFormat(logger::Level::Info, L"Reconvert action: %s", (action_ == EditAction::ReconvertTest) ? L"ReconvertTest" : L"Reconvert");
            
            ComPtr<ITfRange> search_range;
            hr = range->Clone(search_range.GetAddressOf());
            if (FAILED(hr)) return hr;
            
            LONG shifted = 0;
            search_range->ShiftStart(ec, -15, &shifted, nullptr);
            
            wchar_t buf[32] = {0};
            ULONG fetched_chars = 0;
            search_range->GetText(ec, 0, buf, 31, &fetched_chars);
            
            if (fetched_chars > 0) {
                std::wstring_view text(buf, fetched_chars);
                int end_idx = static_cast<int>(text.length()) - 1;
                
                int start_idx = end_idx;
                while (start_idx >= 0 && core::rules::IsWordChar(text[start_idx])) {
                    start_idx--;
                }
                start_idx++;
                
                if (start_idx <= end_idx) {
                    std::wstring target_word(text.substr(start_idx, end_idx - start_idx + 1));
                    logger::LogFormat(logger::Level::Info, L"Reconvert target word: '%s'", target_word.c_str());
                    
                    std::wstring raw_keys = core::rules::ReconstructRawKeys(target_word, ime_->GetEngine().GetInputMethod());
                    raw_keys.push_back(ch_);
                    
                    core::Engine temp_engine(ime_->GetEngine().GetInputMethod());
                    for (wchar_t k : raw_keys) {
                        temp_engine.ProcessKey(k);
                    }
                    std::wstring new_word = temp_engine.GetDisplayString();
                    logger::LogFormat(logger::Level::Info, L"Reconvert raw_keys = '%s', new_word = '%s'", raw_keys.c_str(), new_word.c_str());
                    
                    std::wstring lower_new;
                    for (wchar_t c : new_word) lower_new.push_back(core::rules::ToLower(c));
                    bool is_valid = core::speller::IsInDictionary(lower_new) || core::rules::IsValidVietnamese(new_word, false);
                    
                    if (new_word != target_word && is_valid) {
                        is_convertible_ = true;
                        result_text_ = new_word;
                        
                        if (action_ == EditAction::Reconvert) {
                            ComPtr<ITfRange> word_range;
                            hr = range->Clone(word_range.GetAddressOf());
                            if (SUCCEEDED(hr)) {
                                word_range->Collapse(ec, TF_ANCHOR_END);
                                
                                LONG s_shifted = 0;
                                word_range->ShiftStart(ec, -static_cast<LONG>(fetched_chars - start_idx), &s_shifted, nullptr);
                                
                                HRESULT hrComp = ime_->StartComposition(ec, pic_, word_range.Get());
                                if (SUCCEEDED(hrComp)) {
                                    word_range->SetText(ec, 0, result_text_.c_str(), static_cast<LONG>(result_text_.length()));
                                    ime_->EndComposition(ec);
                                }
                                
                                TF_SELECTION new_sel;
                                new_sel.range = range.Get();
                                new_sel.style.ase = TF_AE_NONE;
                                new_sel.style.fInterimChar = FALSE;
                                pic_->SetSelection(ec, 1, &new_sel);
                            }
                        }
                    }
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
    bool is_convertible_ = false;
    std::wstring result_text_;
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

VietnameseIME::VietnameseIME() noexcept
    : registry_thread_(nullptr),
      registry_shutdown_event_(nullptr),
      registry_watch_event_(nullptr),
      config_changed_(false) {
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
    } else if (riid == IID_ITfFunctionProvider) {
        *ppv = static_cast<ITfFunctionProvider*>(this);
    } else if (riid == IID_ITfFnReconversion || riid == IID_ITfFunction) {
        *ppv = static_cast<ITfFnReconversion*>(this);
    } else if (riid == IID_ITfMouseSink) {
        *ppv = static_cast<ITfMouseSink*>(this);
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
    
    // Shut down registry watcher thread
    if (registry_thread_) {
        if (registry_shutdown_event_) {
            SetEvent(registry_shutdown_event_);
        }
        // Wait up to 2 seconds for the thread to exit cleanly
        WaitForSingleObject(registry_thread_, 2000);
        CloseHandle(registry_thread_);
        registry_thread_ = nullptr;
    }
    if (registry_shutdown_event_) {
        CloseHandle(registry_shutdown_event_);
        registry_shutdown_event_ = nullptr;
    }
    if (registry_watch_event_) {
        CloseHandle(registry_watch_event_);
        registry_watch_event_ = nullptr;
    }
    
    if (mouse_cookie_ != 0 && active_composition_) {
        ComPtr<ITfRange> comp_range;
        if (SUCCEEDED(active_composition_->GetRange(comp_range.GetAddressOf())) && comp_range) {
            ComPtr<ITfContext> context;
            if (SUCCEEDED(comp_range->GetContext(context.GetAddressOf())) && context) {
                ComPtr<ITfMouseTracker> mouse_tracker;
                if (SUCCEEDED(context->QueryInterface(IID_ITfMouseTracker, reinterpret_cast<void**>(mouse_tracker.GetAddressOf())))) {
                    mouse_tracker->UnadviseMouseSink(mouse_cookie_);
                }
            }
        }
        mouse_cookie_ = 0;
    }
    
    if (active_composition_) {
        active_composition_.Reset();
    }
    engine_.Clear();
    display_attribute_atom_ = 0;

    UninitThreadMgrEventSink();
    UninitKeySink();
    
    ComPtr<ITfSourceSingle> source_single;
    if (SUCCEEDED(thread_mgr_.As(source_single))) {
        source_single->UnadviseSingleSink(client_id_, IID_ITfFunctionProvider);
    }

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

    ComPtr<ITfSourceSingle> source_single;
    if (SUCCEEDED(thread_mgr_.As(source_single))) {
        source_single->AdviseSingleSink(client_id_, IID_ITfFunctionProvider, static_cast<ITfFunctionProvider*>(this));
    }

    // Register Display Attribute GUID to get an atom
    ComPtr<ITfCategoryMgr> category_mgr;
    if (SUCCEEDED(CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER, IID_ITfCategoryMgr, reinterpret_cast<void**>(category_mgr.GetAddressOf())))) {
        category_mgr->RegisterGUID(GUID_VietnameseDisplayAttribute, &display_attribute_atom_);
    }

    // Load initial config
    ReloadConfig();

    // Set up registry monitoring (skip on Secure Desktop)
    if (!logger::IsSecureDesktop()) {
        registry_shutdown_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        registry_watch_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (registry_shutdown_event_ && registry_watch_event_) {
            registry_thread_ = CreateThread(nullptr, 0, RegistryWatchThreadProc, this, 0, nullptr);
            if (!registry_thread_) {
                logger::Log(logger::Level::Error, L"ActivateEx: Failed to create Registry watch thread");
            }
        } else {
            logger::Log(logger::Level::Error, L"ActivateEx: Failed to create Registry watch events");
        }
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
                CommitCompositionSync(context.Get());
            }
        }
    }
    return S_OK;
}

STDMETHODIMP VietnameseIME::OnTestKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) {
    if (!pfEaten) return E_INVALIDARG;

    CheckAndReloadConfig();

    KeyDecision decision = MakeKeyDecision(wParam, lParam);
    if (decision.action == KeyAction::Reconvert) {
        decision.eat = TryReconversion(pic, decision.ch, false);
    }

    *pfEaten = decision.eat ? TRUE : FALSE;

    logger::LogFormat(logger::Level::Debug, L"OnTestKeyDown: wParam = 0x%02X, lParam = 0x%08X, action = %d, eaten = %s",
                      wParam, lParam, static_cast<int>(decision.action), decision.eat ? L"TRUE" : L"FALSE");

    return S_OK;
}

STDMETHODIMP VietnameseIME::OnKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) {
    if (!pfEaten) return E_INVALIDARG;

    CheckAndReloadConfig();

    KeyDecision decision = MakeKeyDecision(wParam, lParam);
    if (decision.action == KeyAction::Reconvert) {
        if (TryReconversion(pic, decision.ch, true)) {
            *pfEaten = TRUE;
            return S_OK;
        }
        decision.eat = false;
        decision.action = KeyAction::PassThrough;
    }

    if (decision.commit_existing_before_host && active_composition_) {
        CommitCompositionSync(pic);
    }

    *pfEaten = decision.eat ? TRUE : FALSE;

    if (decision.eat) {
        logger::LogFormat(logger::Level::Info, L"OnKeyDown (EATEN): wParam = 0x%02X, action = %d", wParam, static_cast<int>(decision.action));
        
        if (decision.action == KeyAction::Backspace) {
            ComPtr<ITfEditSession> session;
            session.Attach(new (std::nothrow) EditSession(this, pic, EditAction::Backspace));
            if (session) {
                HRESULT hr = 0;
                HRESULT hrReq = pic->RequestEditSession(client_id_, session.Get(), TF_ES_SYNC | TF_ES_READWRITE, &hr);
                logger::LogFormat(logger::Level::Info, L"RequestEditSession (Backspace) returned hrReq = 0x%08X, hr = 0x%08X", hrReq, hr);
            }
        } else if (decision.action == KeyAction::CommitSpace) {
            ComPtr<ITfEditSession> session;
            session.Attach(new (std::nothrow) EditSession(this, pic, EditAction::Commit, L' '));
            if (session) {
                HRESULT hr = 0;
                HRESULT hrReq = pic->RequestEditSession(client_id_, session.Get(), TF_ES_SYNC | TF_ES_READWRITE, &hr);
                logger::LogFormat(logger::Level::Info, L"RequestEditSession (Commit Space) returned hrReq = 0x%08X, hr = 0x%08X", hrReq, hr);
            }
        } else if (decision.action == KeyAction::CommitChar) {
            ComPtr<ITfEditSession> session;
            session.Attach(new (std::nothrow) EditSession(this, pic, EditAction::Commit, decision.ch));
            if (session) {
                HRESULT hr = 0;
                HRESULT hrReq = pic->RequestEditSession(client_id_, session.Get(), TF_ES_SYNC | TF_ES_READWRITE, &hr);
                logger::LogFormat(logger::Level::Info, L"RequestEditSession (Commit Char) returned hrReq = 0x%08X, hr = 0x%08X", hrReq, hr);
            }
        } else if (decision.action == KeyAction::ProcessChar) {
            logger::LogFormat(logger::Level::Info, L"ProcessChar: wParam = 0x%02X, char = '%c' (0x%04X)", wParam, decision.ch ? decision.ch : L'?', decision.ch);
            if (decision.ch != 0) {
                ComPtr<ITfEditSession> session;
                session.Attach(new (std::nothrow) EditSession(this, pic, EditAction::ProcessChar, decision.ch));
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
        logger::LogFormat(logger::Level::Debug, L"OnKeyDown (PASSED): wParam = 0x%02X, commit_existing = %s",
                          wParam, decision.commit_existing_before_host ? L"TRUE" : L"FALSE");
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

bool VietnameseIME::IsModifierKey(WPARAM wParam) const noexcept {
    return (wParam == VK_SHIFT || wParam == VK_CONTROL || wParam == VK_MENU ||
            wParam == VK_LWIN || wParam == VK_RWIN || wParam == VK_CAPITAL ||
            wParam == VK_NUMLOCK || wParam == VK_SCROLL ||
            wParam == VK_LSHIFT || wParam == VK_RSHIFT ||
            wParam == VK_LCONTROL || wParam == VK_RCONTROL ||
            wParam == VK_LMENU || wParam == VK_RMENU);
}

VietnameseIME::KeyDecision VietnameseIME::MakeKeyDecision(WPARAM wParam, LPARAM lParam) const {
    KeyDecision decision;
    decision.is_modifier = IsModifierKey(wParam);
    if (decision.is_modifier) {
        return decision;
    }

    const bool has_composition = HasActiveComposition();

    // Enter and Tab should keep native host behavior. If a composition exists,
    // finalize it on OnKeyDown before returning the key to the host.
    if (has_composition && (wParam == VK_RETURN || wParam == VK_TAB)) {
        decision.commit_existing_before_host = true;
        return decision;
    }

    if (IsCurrentAppBlocked()) {
        if (has_composition) {
            decision.commit_existing_before_host = true;
        }
        return decision;
    }

    const bool filtered = IsKeyFiltered(wParam, lParam);
    if (!filtered) {
        if (has_composition) {
            if ((GetKeyState(VK_CONTROL) & 0x8000) == 0 &&
                (GetKeyState(VK_MENU) & 0x8000) == 0 &&
                (GetKeyState(VK_LWIN) & 0x8000) == 0 &&
                (GetKeyState(VK_RWIN) & 0x8000) == 0) {
                decision.ch = TranslateKey(wParam, lParam);
                if (decision.ch != 0 && decision.ch >= L' ') {
                    decision.eat = true;
                    decision.action = KeyAction::CommitChar;
                    return decision;
                }
            }
            decision.commit_existing_before_host = true;
        }
        return decision;
    }

    if (has_composition) {
        decision.eat = true;
        if (wParam == VK_BACK) {
            decision.action = KeyAction::Backspace;
        } else if (wParam == VK_SPACE) {
            decision.action = KeyAction::CommitSpace;
        } else {
            decision.ch = TranslateKey(wParam, lParam);
            if (decision.ch != 0) {
                decision.action = KeyAction::ProcessChar;
            } else {
                decision.eat = false;
                decision.action = KeyAction::PassThrough;
                decision.commit_existing_before_host = true;
            }
        }
        return decision;
    }

    decision.ch = TranslateKey(wParam, lParam);
    if (decision.ch == 0) {
        return decision;
    }

    if (IsReconversionKey(decision.ch, engine_.GetInputMethod())) {
        decision.action = KeyAction::Reconvert;
        return decision;
    }

    decision.eat = true;
    decision.action = KeyAction::ProcessChar;
    return decision;
}

bool VietnameseIME::TryReconversion(ITfContext* pic, wchar_t ch, bool apply) {
    if (!pic || ch == 0) {
        return false;
    }

    ComPtr<EditSession> session;
    session.Attach(new (std::nothrow) EditSession(this, pic, apply ? EditAction::Reconvert : EditAction::ReconvertTest, ch));
    if (!session) {
        return false;
    }

    HRESULT hr = S_OK;
    HRESULT hrReq = pic->RequestEditSession(
        client_id_,
        session.Get(),
        apply ? (TF_ES_SYNC | TF_ES_READWRITE) : (TF_ES_SYNC | TF_ES_READ),
        &hr
    );

    logger::LogFormat(logger::Level::Info,
                      L"TryReconversion(apply=%s) returned hrReq = 0x%08X, hr = 0x%08X, convertible = %s",
                      apply ? L"TRUE" : L"FALSE",
                      hrReq,
                      hr,
                      session->is_convertible() ? L"TRUE" : L"FALSE");

    return SUCCEEDED(hrReq) && SUCCEEDED(hr) && session->is_convertible();
}

bool VietnameseIME::IsValidCompositionKey(WPARAM wParam, core::InputMethod method) const {
    if (wParam >= 0x41 && wParam <= 0x5A) {
        return true;
    }
    if (method == core::InputMethod::VNI) {
        if (wParam >= 0x30 && wParam <= 0x39) {
            if ((GetKeyState(VK_SHIFT) & 0x8000) == 0) {
                return true;
            }
        }
    }
    return false;
}

std::wstring VietnameseIME::GetFocusedProcessName() const {
    HWND hwnd = ::GetFocus();
    if (!hwnd) {
        hwnd = ::GetForegroundWindow();
    }
    if (!hwnd) {
        return L"";
    }

    DWORD process_id = 0;
    ::GetWindowThreadProcessId(hwnd, &process_id);
    if (process_id == 0) {
        return L"";
    }

    if (process_id == cached_process_id_) {
        return cached_process_name_;
    }

    std::wstring process_name;
    HANDLE hProcess = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id);
    if (hProcess) {
        std::wstring path(32768, L'\0');
        DWORD size = static_cast<DWORD>(path.size());
        if (::QueryFullProcessImageNameW(hProcess, 0, &path[0], &size) && size > 0) {
            path.resize(size);
            process_name = NormalizeProcessName(std::move(path));
        }
        ::CloseHandle(hProcess);
    }

    cached_process_id_ = process_id;
    cached_process_name_ = std::move(process_name);
    return cached_process_name_;
}

bool VietnameseIME::IsCurrentAppBlocked() const {
    if (!enable_app_blocklist_ || blocked_apps_.empty()) {
        return false;
    }

    std::wstring process_name = GetFocusedProcessName();
    if (process_name.empty()) {
        return false;
    }

    for (const auto& blocked_app : blocked_apps_) {
        if (blocked_app == process_name) {
            return true;
        }
    }
    return false;
}

bool VietnameseIME::IsKeyFiltered(WPARAM wParam, [[maybe_unused]] LPARAM lParam) const noexcept {
    if (is_password_field_) {
        return false;
    }

    // Direct Win32 focused HWND style fallback
    HWND hwnd = ::GetFocus();
    if (hwnd) {
        LONG_PTR lStyle = ::GetWindowLongPtrW(hwnd, GWL_STYLE);
        if ((lStyle & ES_PASSWORD) != 0 || ::SendMessageW(hwnd, EM_GETPASSWORDCHAR, 0, 0) != 0) {
            return false;
        }
    }

    if ((GetKeyState(VK_CONTROL) & 0x8000) != 0 ||
        (GetKeyState(VK_MENU) & 0x8000) != 0 ||
        (GetKeyState(VK_LWIN) & 0x8000) != 0 ||
        (GetKeyState(VK_RWIN) & 0x8000) != 0) {
        return false;
    }

    if (active_composition_) {
        if (wParam == VK_BACK || wParam == VK_SPACE || wParam == VK_RETURN) {
            return true;
        }
    }

    if (IsValidCompositionKey(wParam, engine_.GetInputMethod())) {
        return true;
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

STDMETHODIMP VietnameseIME::OnSetFocus(ITfDocumentMgr* pdmFocus, ITfDocumentMgr* pdmPrevFocus) {
    logger::Log(logger::Level::Info, L"OnSetFocus (ITfDocumentMgr) called.");
    is_password_field_ = false;

    if (pdmPrevFocus) {
        ComPtr<ITfContext> context;
        if (SUCCEEDED(pdmPrevFocus->GetTop(context.GetAddressOf())) && context) {
            CommitCompositionSync(context.Get());
        }
    }

    if (pdmFocus) {
        ComPtr<ITfContext> context;
        if (SUCCEEDED(pdmFocus->GetTop(context.GetAddressOf())) && context) {
            ComPtr<EditSession> session;
            session.Attach(new (std::nothrow) EditSession(this, context.Get(), EditAction::CheckPassword));
            if (session) {
                HRESULT hr = S_OK;
                HRESULT hrReq = context->RequestEditSession(client_id_, session.Get(), TF_ES_SYNC | TF_ES_READ, &hr);
                if (FAILED(hrReq) || FAILED(hr)) {
                    context->RequestEditSession(client_id_, session.Get(), TF_ES_READ, &hr);
                }
            }
        }
    }
    return S_OK;
}

STDMETHODIMP VietnameseIME::OnPushContext(ITfContext* pic) {
    logger::Log(logger::Level::Info, L"OnPushContext called.");
    is_password_field_ = false;
    if (pic) {
        ComPtr<EditSession> session;
        session.Attach(new (std::nothrow) EditSession(this, pic, EditAction::CheckPassword));
        if (session) {
            HRESULT hr = S_OK;
            HRESULT hrReq = pic->RequestEditSession(client_id_, session.Get(), TF_ES_SYNC | TF_ES_READ, &hr);
            if (FAILED(hrReq) || FAILED(hr)) {
                pic->RequestEditSession(client_id_, session.Get(), TF_ES_READ, &hr);
            }
        }
    }
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
    
    // Unadvise Mouse Sink
    if (mouse_cookie_ != 0 && pComposition) {
        ComPtr<ITfRange> comp_range;
        if (SUCCEEDED(pComposition->GetRange(comp_range.GetAddressOf())) && comp_range) {
            ComPtr<ITfContext> context;
            if (SUCCEEDED(comp_range->GetContext(context.GetAddressOf())) && context) {
                ComPtr<ITfMouseTracker> mouse_tracker;
                if (SUCCEEDED(context->QueryInterface(IID_ITfMouseTracker, reinterpret_cast<void**>(mouse_tracker.GetAddressOf())))) {
                    HRESULT hrMouse = mouse_tracker->UnadviseMouseSink(mouse_cookie_);
                    logger::LogFormat(logger::Level::Info, L"OnCompositionTerminated: UnadviseMouseSink returned hr = 0x%08X", hrMouse);
                }
            }
        }
        mouse_cookie_ = 0;
    }

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
    
    if (SUCCEEDED(hr)) {
        ComPtr<ITfMouseTracker> mouse_tracker;
        if (SUCCEEDED(pic->QueryInterface(IID_ITfMouseTracker, reinterpret_cast<void**>(mouse_tracker.GetAddressOf())))) {
            HRESULT hrMouse = mouse_tracker->AdviseMouseSink(cloned_range.Get(), static_cast<ITfMouseSink*>(this), &mouse_cookie_);
            logger::LogFormat(logger::Level::Info, L"StartComposition: AdviseMouseSink returned hr = 0x%08X, cookie = %u", hrMouse, mouse_cookie_);
        } else {
            logger::Log(logger::Level::Warning, L"StartComposition: Context does not support ITfMouseTracker");
        }
    }
    
    return hr;
}

HRESULT VietnameseIME::EndComposition(TfEditCookie ec) {
    if (!active_composition_) return S_OK;

    // Apply shorthand expansion if enabled
    IMEConfig config = LoadConfigFromRegistry();
    if (config.enable_shorthand && !shorthand_map_.empty()) {
        ComPtr<ITfRange> comp_range;
        if (SUCCEEDED(active_composition_->GetRange(comp_range.GetAddressOf())) && comp_range) {
            wchar_t buf[256] = {0};
            ULONG fetched_chars = 0;
            comp_range->GetText(ec, 0, buf, 255, &fetched_chars);
            if (fetched_chars > 0) {
                std::wstring comp_text(buf, fetched_chars);
                std::wstring expanded = LookUpShorthand(comp_text);
                if (expanded != comp_text) {
                    comp_range->SetText(ec, 0, expanded.c_str(), static_cast<LONG>(expanded.length()));
                }
            }
        }
    }

    // Apply auto-capitalization if enabled
    if (config.enable_auto_capitalize) {
        ComPtr<ITfRange> comp_range;
        if (SUCCEEDED(active_composition_->GetRange(comp_range.GetAddressOf())) && comp_range) {
            ComPtr<ITfRange> context_range;
            if (SUCCEEDED(comp_range->Clone(context_range.GetAddressOf())) && context_range) {
                context_range->Collapse(ec, TF_ANCHOR_START);
                LONG shifted = 0;
                context_range->ShiftStart(ec, -20, &shifted, nullptr);
                
                wchar_t buf[32] = {0};
                ULONG fetched = 0;
                if (SUCCEEDED(context_range->GetText(ec, 0, buf, 31, &fetched)) && fetched > 0) {
                    std::wstring preceding_text(buf, fetched);
                    size_t last_non_space = preceding_text.find_last_not_of(L" \t\r\n");
                    if (last_non_space != std::wstring::npos) {
                        wchar_t end_char = preceding_text[last_non_space];
                        if (end_char == L'.' || end_char == L'?' || end_char == L'!') {
                            wchar_t comp_buf[256] = {0};
                            ULONG comp_fetched = 0;
                            comp_range->GetText(ec, 0, comp_buf, 255, &comp_fetched);
                            if (comp_fetched > 0) {
                                std::wstring comp_text(comp_buf, comp_fetched);
                                comp_text[0] = core::rules::ToUpper(comp_text[0]);
                                comp_range->SetText(ec, 0, comp_text.c_str(), static_cast<LONG>(comp_text.length()));
                            }
                        }
                    }
                }
            }
        }
    }
    
    // Unadvise Mouse Sink
    if (mouse_cookie_ != 0) {
        ComPtr<ITfRange> comp_range;
        if (SUCCEEDED(active_composition_->GetRange(comp_range.GetAddressOf())) && comp_range) {
            ComPtr<ITfContext> context;
            if (SUCCEEDED(comp_range->GetContext(context.GetAddressOf())) && context) {
                ComPtr<ITfMouseTracker> mouse_tracker;
                if (SUCCEEDED(context->QueryInterface(IID_ITfMouseTracker, reinterpret_cast<void**>(mouse_tracker.GetAddressOf())))) {
                    HRESULT hrMouse = mouse_tracker->UnadviseMouseSink(mouse_cookie_);
                    logger::LogFormat(logger::Level::Info, L"EndComposition: UnadviseMouseSink returned hr = 0x%08X", hrMouse);
                }
            }
        }
        mouse_cookie_ = 0;
    }

    // Keep the insertion point after the finalized composition text. Some hosts
    // leave the transformed range selected after SetText(), so the next delimiter
    // can replace the word instead of being inserted after it.
    ComPtr<ITfRange> final_comp_range;
    if (SUCCEEDED(active_composition_->GetRange(final_comp_range.GetAddressOf())) && final_comp_range) {
        ComPtr<ITfContext> context;
        if (SUCCEEDED(final_comp_range->GetContext(context.GetAddressOf())) && context) {
            ComPtr<ITfRange> caret_range;
            if (SUCCEEDED(final_comp_range->Clone(caret_range.GetAddressOf())) && caret_range) {
                caret_range->Collapse(ec, TF_ANCHOR_END);

                TF_SELECTION sel;
                sel.range = caret_range.Get();
                sel.style.ase = TF_AE_NONE;
                sel.style.fInterimChar = FALSE;
                HRESULT hrSel = context->SetSelection(ec, 1, &sel);
                logger::LogFormat(logger::Level::Info, L"EndComposition: SetSelection to composition end returned hr = 0x%08X", hrSel);
            }
        }
    }

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
        HRESULT hrReq = pic->RequestEditSession(client_id_, session.Get(), TF_ES_SYNC | TF_ES_READWRITE, &hr);
        if (FAILED(hrReq) || FAILED(hr)) {
            logger::LogFormat(logger::Level::Warning, L"CommitCompositionSync: Sync request failed (hrReq = 0x%08X, hr = 0x%08X), falling back to async", hrReq, hr);
            pic->RequestEditSession(client_id_, session.Get(), TF_ES_ASYNCDONTCARE | TF_ES_READWRITE, &hr);
        }
    }
}

// ITfFunctionProvider methods
STDMETHODIMP VietnameseIME::GetType(GUID* pguid) {
    if (!pguid) return E_INVALIDARG;
    *pguid = CLSID_VietnameseIME;
    return S_OK;
}

STDMETHODIMP VietnameseIME::GetDescription(BSTR* pbstrDesc) {
    if (!pbstrDesc) return E_INVALIDARG;
    *pbstrDesc = SysAllocString(L"Vietnamese IME Function Provider");
    return *pbstrDesc ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP VietnameseIME::GetFunction(REFGUID rguid, REFIID riid, IUnknown** ppunk) {
    if (!ppunk) return E_INVALIDARG;
    *ppunk = nullptr;
    if (rguid == IID_ITfFnReconversion) {
        return QueryInterface(riid, reinterpret_cast<void**>(ppunk));
    }
    return E_INVALIDARG;
}

// ITfFunction methods (base of ITfFnReconversion)
STDMETHODIMP VietnameseIME::GetDisplayName(BSTR* pbstrName) {
    if (!pbstrName) return E_INVALIDARG;
    *pbstrName = SysAllocString(L"Reconversion");
    return *pbstrName ? S_OK : E_OUTOFMEMORY;
}

// ITfFnReconversion methods
STDMETHODIMP VietnameseIME::QueryRange(ITfRange* pRange, ITfRange** ppNewRange, BOOL* pfConvertible) {
    if (ppNewRange) *ppNewRange = nullptr;
    if (pfConvertible) *pfConvertible = FALSE;
    return E_NOTIMPL;
}

STDMETHODIMP VietnameseIME::GetReconversion(ITfRange* pRange, ITfCandidateList** ppCandList) {
    if (ppCandList) *ppCandList = nullptr;
    return E_NOTIMPL;
}

STDMETHODIMP VietnameseIME::Reconvert(ITfRange* pRange) {
    return E_NOTIMPL;
}

// ITfMouseSink methods
STDMETHODIMP VietnameseIME::OnMouseEvent(ULONG uEdge, ULONG uQuadrant, DWORD dwBtnStatus, BOOL* pfEaten) {
    if (!pfEaten) return E_INVALIDARG;
    
    logger::LogFormat(logger::Level::Info, L"OnMouseEvent: uEdge = %u, uQuadrant = %u, dwBtnStatus = 0x%X", uEdge, uQuadrant, dwBtnStatus);
    
    // If a button click occurred (left, right, or middle mouse button)
    // We should commit the active composition.
    if ((dwBtnStatus & (MK_LBUTTON | MK_RBUTTON | MK_MBUTTON)) != 0) {
        logger::Log(logger::Level::Info, L"OnMouseEvent: Mouse click detected, committing composition asynchronously");
        
        // We need an ITfContext to commit the composition.
        // We can get the context from the active composition's range.
        if (active_composition_) {
            ComPtr<ITfRange> comp_range;
            if (SUCCEEDED(active_composition_->GetRange(comp_range.GetAddressOf())) && comp_range) {
                ComPtr<ITfContext> context;
                if (SUCCEEDED(comp_range->GetContext(context.GetAddressOf())) && context) {
                    CommitCompositionAsync(context.Get());
                }
            }
        }
    }
    
    // Always set pfEaten to FALSE so the application handles the click (e.g. moves the caret)
    *pfEaten = FALSE;
    return S_OK;
}

// Key translation helper
wchar_t VietnameseIME::TranslateKey(WPARAM wParam, LPARAM lParam) const {
    wchar_t ch = 0;
    BYTE keyboardState[256];
    if (GetKeyboardState(keyboardState)) {
        wchar_t buf[4] = {0};
        UINT scanCode = (lParam >> 16) & 0xFF;
        int count = ToUnicode(static_cast<UINT>(wParam), scanCode, keyboardState, buf, 4, 0);
        if (count > 0) {
            ch = buf[0];
        }
    }
    return ch;
}

void VietnameseIME::ReloadConfig() {
    IMEConfig config = LoadConfigFromRegistry();
    logger::SetEnabled(config.enable_log);
    logger::Log(logger::Level::Info, L"VietnameseIME::ReloadConfig loading configuration...");
    engine_.SetInputMethod(config.input_method);
    engine_.SetAutoCorrect(config.enable_auto_correct);
    enable_app_blocklist_ = config.enable_app_blocklist;
    blocked_apps_ = NormalizeProcessList(config.blocked_apps);
    cached_process_id_ = 0;
    cached_process_name_.clear();

    // Load shorthand rules
    LoadShorthandRules();

    logger::LogFormat(logger::Level::Info, L"Config loaded: input_method = %d, enable_auto_correct = %s, enable_log = %s, enable_shorthand = %s, enable_app_blocklist = %s, blocked_apps = %zu",
                      static_cast<int>(config.input_method), config.enable_auto_correct ? L"true" : L"false",
                      config.enable_log ? L"true" : L"false", config.enable_shorthand ? L"true" : L"false",
                      config.enable_app_blocklist ? L"true" : L"false", blocked_apps_.size());
}

std::wstring VietnameseIME::LookUpShorthand(const std::wstring& shortcut) {
    if (shorthand_map_.empty() || shortcut.empty()) {
        return shortcut;
    }

    // Normalize shortcut to lower case for map lookup
    std::wstring lower_shortcut;
    lower_shortcut.reserve(shortcut.length());
    for (wchar_t c : shortcut) {
        lower_shortcut.push_back(core::rules::ToLower(c));
    }

    auto it = shorthand_map_.find(lower_shortcut);
    if (it == shorthand_map_.end()) {
        return shortcut;
    }

    const std::wstring& expansion = it->second;
    if (expansion.empty()) {
        return shortcut;
    }

    // Casing checks
    bool all_upper = true;
    for (wchar_t c : shortcut) {
        if (IsLowerChar(c)) all_upper = false;
    }

    // Case preservation logic
    if (all_upper) {
        std::wstring result;
        result.reserve(expansion.length());
        for (wchar_t c : expansion) {
            result.push_back(core::rules::ToUpper(c));
        }
        return result;
    }

    // Capitalized (first character uppercase, rest lowercase)
    bool first_upper_rest_lower = false;
    if (IsUpperChar(shortcut[0])) {
        bool rest_lower = true;
        for (size_t i = 1; i < shortcut.length(); ++i) {
            if (IsUpperChar(shortcut[i])) {
                rest_lower = false;
                break;
            }
        }
        if (rest_lower) {
            first_upper_rest_lower = true;
        }
    }

    if (first_upper_rest_lower) {
        std::wstring result = expansion;
        result[0] = core::rules::ToUpper(result[0]);
        return result;
    }

    // Otherwise, return expansion exactly as defined in rules file
    return expansion;
}

void VietnameseIME::LoadShorthandRules() {
    shorthand_map_.clear();

    IMEConfig config = LoadConfigFromRegistry();
    if (!config.enable_shorthand) {
        return;
    }

    std::wstring filePath = GetShorthandFilePath(g_hInst);
    if (filePath.empty()) return;

    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        logger::LogFormat(logger::Level::Warning, L"Shorthand file not found or cannot be opened: %s", filePath.c_str());
        return;
    }

    DWORD fileSize = GetFileSize(hFile, nullptr);
    if (fileSize == INVALID_FILE_SIZE || fileSize == 0) {
        CloseHandle(hFile);
        return;
    }

    std::string utf8Content;
    utf8Content.resize(fileSize);
    DWORD bytesRead = 0;
    if (ReadFile(hFile, &utf8Content[0], fileSize, &bytesRead, nullptr) && bytesRead > 0) {
        utf8Content.resize(bytesRead);
    } else {
        CloseHandle(hFile);
        return;
    }
    CloseHandle(hFile);

    // Convert UTF-8 to UTF-16
    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8Content.data(), static_cast<int>(utf8Content.length()), nullptr, 0);
    if (wlen <= 0) return;

    std::wstring utf16Content;
    utf16Content.resize(wlen);
    MultiByteToWideChar(CP_UTF8, 0, utf8Content.data(), static_cast<int>(utf8Content.length()), &utf16Content[0], wlen);

    // Parse line by line
    size_t start = 0;
    // Skip UTF-16 BOM if present
    if (!utf16Content.empty() && utf16Content[0] == L'\xFEFF') {
        start = 1;
    }

    while (start < utf16Content.length()) {
        size_t end = utf16Content.find(L'\n', start);
        if (end == std::wstring::npos) {
            end = utf16Content.length();
        }

        std::wstring_view line(utf16Content.data() + start, end - start);
        if (!line.empty() && line.back() == L'\r') {
            line.remove_suffix(1);
        }

        size_t eq_pos = line.find(L'=');
        if (eq_pos != std::wstring_view::npos) {
            std::wstring_view key_view = line.substr(0, eq_pos);
            std::wstring_view val_view = line.substr(eq_pos + 1);

            // Trim spaces
            while (!key_view.empty() && (key_view.front() == L' ' || key_view.front() == L'\t')) key_view.remove_prefix(1);
            while (!key_view.empty() && (key_view.back() == L' ' || key_view.back() == L'\t')) key_view.remove_suffix(1);
            while (!val_view.empty() && (val_view.front() == L' ' || val_view.front() == L'\t')) val_view.remove_prefix(1);
            while (!val_view.empty() && (val_view.back() == L' ' || val_view.back() == L'\t')) val_view.remove_suffix(1);

            if (!key_view.empty() && !val_view.empty()) {
                std::wstring key(key_view);
                for (wchar_t& c : key) c = core::rules::ToLower(c);
                shorthand_map_[key] = std::wstring(val_view);
            }
        }

        start = (end < utf16Content.length()) ? end + 1 : utf16Content.length();
    }

    logger::LogFormat(logger::Level::Info, L"Loaded %zu shorthand rules from %s", shorthand_map_.size(), filePath.c_str());
}

void VietnameseIME::CheckAndReloadConfig() {
    if (config_changed_.exchange(false)) {
        ReloadConfig();
    }
}

DWORD WINAPI VietnameseIME::RegistryWatchThreadProc(LPVOID lpParam) {
    logger::Log(logger::Level::Info, L"RegistryWatchThreadProc started");
    VietnameseIME* pThis = reinterpret_cast<VietnameseIME*>(lpParam);
    if (!pThis) return 0;

    HKEY hKey = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, REG_KEY_PATH, 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_READ, nullptr, &hKey, nullptr) != ERROR_SUCCESS) {
        logger::Log(logger::Level::Error, L"RegistryWatchThreadProc: Failed to open/create Registry key for watching");
        return 0;
    }

    HANDLE waitHandles[2] = { pThis->registry_shutdown_event_, pThis->registry_watch_event_ };

    while (true) {
        LONG status = RegNotifyChangeKeyValue(hKey, FALSE, REG_NOTIFY_CHANGE_LAST_SET, pThis->registry_watch_event_, TRUE);
        if (status != ERROR_SUCCESS) {
            logger::LogFormat(logger::Level::Error, L"RegNotifyChangeKeyValue failed: %d", status);
            break;
        }

        DWORD waitResult = WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE);
        if (waitResult == WAIT_OBJECT_0) {
            logger::Log(logger::Level::Info, L"RegistryWatchThreadProc: Shutdown event signaled");
            break;
        } else if (waitResult == WAIT_OBJECT_0 + 1) {
            logger::Log(logger::Level::Info, L"RegistryWatchThreadProc: Registry change detected");
            pThis->config_changed_ = true;
        } else {
            logger::LogFormat(logger::Level::Error, L"WaitForMultipleObjects failed or returned unexpected result: %u", waitResult);
            break;
        }
    }

    if (hKey) {
        RegCloseKey(hKey);
    }
    logger::Log(logger::Level::Info, L"RegistryWatchThreadProc exiting");
    return 0;
}

} // namespace vn_ime

