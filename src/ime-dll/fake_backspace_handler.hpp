#pragma once

#include <windows.h>
#include <string>
#include <string_view>
#include "engine.hpp"

namespace vn_ime::fake_backspace {

enum class HostInputDispatch {
    SendToHost,
    SuppressForTesting,
};

// Process identification helpers
bool IsCorelDrawProcess(std::wstring_view process_name) noexcept;
bool IsTerminalProcess(std::wstring_view process_name) noexcept;
bool IsVisualStudioProcess(std::wstring_view process_name) noexcept;
bool IsConsoleProcess(std::wstring_view process_name) noexcept;

bool IsFakeBackspaceTargetApp(
    std::wstring_view host_process,
    std::wstring_view focused_process) noexcept;

// Synthetic keystroke and character dispatchers (using 0xDEADC0DE marker)
void SendSyntheticNativeKey(
    WORD vk,
    HWND target_hwnd = nullptr,
    bool is_direct_post = false);

void SendSyntheticUnicodeChar(
    wchar_t ch,
    HWND target_hwnd = nullptr,
    bool is_direct_post = false);

// Core execution routines for fake backspace inline typing
bool ProcessFakeBackspaceChar(
    core::Engine& engine,
    wchar_t ch,
    size_t& direct_inline_length,
    HWND target_hwnd = nullptr,
    bool is_direct_post = false,
    HostInputDispatch dispatch = HostInputDispatch::SendToHost);

bool ProcessFakeBackspaceBackspace(
    core::Engine& engine,
    size_t& direct_inline_length,
    HWND target_hwnd = nullptr,
    bool is_direct_post = false,
    HostInputDispatch dispatch = HostInputDispatch::SendToHost);

} // namespace vn_ime::fake_backspace
