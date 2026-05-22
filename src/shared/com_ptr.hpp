#pragma once

#include <utility>

namespace vn_ime {

template <typename T>
class ComPtr {
public:
    ComPtr() noexcept : ptr_(nullptr) {}
    ComPtr(std::nullptr_t) noexcept : ptr_(nullptr) {}
    
    explicit ComPtr(T* ptr) noexcept : ptr_(ptr) {
        if (ptr_) {
            ptr_->AddRef();
        }
    }

    // Copy constructor
    ComPtr(const ComPtr& other) noexcept : ptr_(other.ptr_) {
        if (ptr_) {
            ptr_->AddRef();
        }
    }

    // Move constructor
    ComPtr(ComPtr&& other) noexcept : ptr_(other.ptr_) {
        other.ptr_ = nullptr;
    }

    ~ComPtr() noexcept {
        Reset();
    }

    // Copy assignment
    ComPtr& operator=(const ComPtr& other) noexcept {
        if (this != &other) {
            ComPtr temp(other);
            Swap(temp);
        }
        return *this;
    }

    // Move assignment
    ComPtr& operator=(ComPtr&& other) noexcept {
        if (this != &other) {
            Reset();
            ptr_ = other.ptr_;
            other.ptr_ = nullptr;
        }
        return *this;
    }

    T* Get() const noexcept {
        return ptr_;
    }

    T& operator*() const noexcept {
        return *ptr_;
    }

    T* operator->() const noexcept {
        return ptr_;
    }

    explicit operator bool() const noexcept {
        return ptr_ != nullptr;
    }

    T** GetAddressOf() noexcept {
        return &ptr_;
    }

    T** ReleaseAndGetAddressOf() noexcept {
        Reset();
        return &ptr_;
    }

    void Attach(T* ptr) noexcept {
        Reset();
        ptr_ = ptr;
    }

    T* Detach() noexcept {
        T* temp = ptr_;
        ptr_ = nullptr;
        return temp;
    }

    void Reset() noexcept {
        if (ptr_) {
            ptr_->Release();
            ptr_ = nullptr;
        }
    }

    void Swap(ComPtr& other) noexcept {
        std::swap(ptr_, other.ptr_);
    }

    // Helper to query another interface from this COM object
    template <typename U>
    HRESULT As(ComPtr<U>& other) const noexcept {
        if (!ptr_) return E_POINTER;
        return ptr_->QueryInterface(__uuidof(U), reinterpret_cast<void**>(other.ReleaseAndGetAddressOf()));
    }

private:
    T* ptr_ = nullptr;
};

} // namespace vn_ime
