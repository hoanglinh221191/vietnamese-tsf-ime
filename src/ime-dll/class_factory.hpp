#pragma once

#include <unknwn.h>
#include <new>
#include "com_ptr.hpp"

namespace vn_ime {

// Forward declaration of the class factory creator helper
HRESULT CreateInstance_VietnameseIME(IUnknown* outer, REFIID riid, void** ppv);

class ClassFactory : public IClassFactory {
public:
    virtual ~ClassFactory() noexcept = default;

    // IUnknown methods
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;

        if (riid == IID_IUnknown || riid == IID_IClassFactory) {
            *ppv = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    STDMETHODIMP_(ULONG) AddRef() override {
        return ++ref_count_;
    }

    STDMETHODIMP_(ULONG) Release() override {
        ULONG count = --ref_count_;
        if (count == 0) {
            delete this;
        }
        return count;
    }

    // IClassFactory methods
    STDMETHODIMP CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppvObj) override {
        if (!ppvObj) return E_POINTER;
        *ppvObj = nullptr;

        if (pUnkOuter) {
            return CLASS_E_NOAGGREGATION;
        }

        return CreateInstance_VietnameseIME(pUnkOuter, riid, ppvObj);
    }

    STDMETHODIMP LockServer(BOOL fLock) override {
        if (fLock) {
            ++server_locks_;
        } else {
            --server_locks_;
        }
        return S_OK;
    }

    // Static helpers to track active objects and locks
    static ULONG GetServerLocks() noexcept { return server_locks_; }
    static ULONG GetActiveObjects() noexcept { return active_objects_; }
    static void IncrementActiveObjects() noexcept { ++active_objects_; }
    static void DecrementActiveObjects() noexcept { --active_objects_; }

private:
    ULONG ref_count_ = 1;
    inline static ULONG server_locks_ = 0;
    inline static ULONG active_objects_ = 0;
};

} // namespace vn_ime
