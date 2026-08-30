#pragma once

#include <unknwn.h>
#include <atomic>
#include <new>
#include "com_ptr.hpp"

namespace vn_ime {

// Forward declaration of the class factory creator helper
HRESULT CreateInstance_VietnameseIME(IUnknown* outer, REFIID riid, void** ppv);

class ClassFactory : public IClassFactory {
public:
    ClassFactory() noexcept {
        IncrementActiveObjects();
    }

    virtual ~ClassFactory() noexcept {
        DecrementActiveObjects();
    }

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
        return ref_count_.fetch_add(1, std::memory_order_relaxed) + 1;
    }

    STDMETHODIMP_(ULONG) Release() override {
        const ULONG count =
            ref_count_.fetch_sub(1, std::memory_order_acq_rel) - 1;
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
            server_locks_.fetch_add(1, std::memory_order_relaxed);
        } else {
            ULONG current = server_locks_.load(std::memory_order_acquire);
            while (current != 0 &&
                   !server_locks_.compare_exchange_weak(
                       current, current - 1,
                       std::memory_order_acq_rel,
                       std::memory_order_acquire)) {
            }
            if (current == 0) {
                return E_UNEXPECTED;
            }
        }
        return S_OK;
    }

    // Static helpers to track active objects and locks
    static ULONG GetServerLocks() noexcept {
        return server_locks_.load(std::memory_order_acquire);
    }
    static ULONG GetActiveObjects() noexcept {
        return active_objects_.load(std::memory_order_acquire);
    }
    static void IncrementActiveObjects() noexcept {
        active_objects_.fetch_add(1, std::memory_order_relaxed);
    }
    static void DecrementActiveObjects() noexcept {
        ULONG current = active_objects_.load(std::memory_order_acquire);
        while (current != 0 &&
               !active_objects_.compare_exchange_weak(
                   current, current - 1,
                   std::memory_order_acq_rel,
                   std::memory_order_acquire)) {
        }
    }

private:
    std::atomic<ULONG> ref_count_{1};
    inline static std::atomic<ULONG> server_locks_{0};
    inline static std::atomic<ULONG> active_objects_{0};
};

} // namespace vn_ime
