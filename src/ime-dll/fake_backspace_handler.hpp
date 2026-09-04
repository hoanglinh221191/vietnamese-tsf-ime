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
bool IsExcelProcess(std::wstring_view process_name) noexcept;
bool IsOutlookProcess(std::wstring_view process_name) noexcept;
bool IsLibreOfficeProcess(std::wstring_view process_name) noexcept;

bool IsFakeBackspaceTargetApp(
    std::wstring_view host_process,
    std::wstring_view focused_process) noexcept;

bool IsNativeEnterReplayTargetApp(
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

// Upper bound on the INPUT records dispatched by a single SendInput batch.
// Two records (down + up) are emitted per key, so this covers an edit of 128
// keystrokes - far beyond any inline word the engine can hold.
inline constexpr size_t kMaxSyntheticEditInputs = 256;

// Fills `out` with `backspace_count` synthetic Backspace presses followed by
// `chars` typed as Unicode, and returns the number of INPUT records written
// (0 when nothing to send or when `capacity` is too small).
// Exposed so unit tests can assert the exact ordering of a batch.
//
// `prefer_selection_replace` asks for a different shape when the edit both
// deletes and types: Shift+Left over the characters being replaced, then the
// replacement, so the host is never sent a Backspace at all. CorelDRAW loses
// Backspaces it has already accepted - two rewrites in one word can leave
// "thuu" where "thu" was meant - and a replacement made through a selection
// cannot run into that. Pure deletions have no replacement to select over and
// keep using Backspace.
size_t BuildSyntheticEditInputs(
    size_t backspace_count,
    std::wstring_view chars,
    INPUT* out,
    size_t capacity,
    bool prefer_selection_replace = false) noexcept;

// Fills `out` with just the selection half of a selection replacement: Shift
// down, one Left per character being replaced, Shift up. Returns the number of
// INPUT records written (0 when nothing to select or `capacity` is too small).
//
// The two halves exist separately because CorelDRAW loses the deletion when it
// dequeues the caret keys and the replacement text in the same pump iteration.
// Measured over 84 rewrites: every rewrite whose replacement reached the key
// sink 12ms or more after the Left produced correct text, and all four that
// arrived within 6ms produced a doubled vowel ("thu" + horn + hook came out
// "thuu" with both marks). Sending the halves in one SendInput array leaves
// that spacing entirely to the host's scheduling; sending them as two batches a
// timer apart does not.
size_t BuildSelectionPrefixInputs(
    size_t select_count,
    INPUT* out,
    size_t capacity) noexcept;

// Fills `out` with the down/up pair for one virtual key, so a replayed key can
// join the paced queue instead of jumping ahead of it. `extended` sets
// KEYEVENTF_EXTENDEDKEY, which arrows and Home/End need.
size_t BuildSyntheticNativeKeyInputs(
    WORD virtual_key,
    bool extended,
    INPUT* out,
    size_t capacity) noexcept;

// Dispatches `backspace_count` Backspace presses followed by `chars` in ONE
// SendInput call. Windows inserts the events of a single SendInput array
// serially and never intersperses them with the user's real keystrokes, so a
// host such as CorelDRAW can no longer observe a partially applied edit.
void SendSyntheticEditBatch(
    size_t backspace_count,
    std::wstring_view chars,
    HWND target_hwnd = nullptr,
    bool is_direct_post = false,
    bool prefer_selection_replace = false);

// Notified immediately before a synthetic edit reaches the host, so the caller
// can arm its echo guard before any injected key can be routed back into the
// text service. Hosts that hand keys to TSF outside their own message pump make
// GetMessageExtraInfo() unreliable, and without the guard the service eats its
// own Backspace presses.
struct EditDispatchObserver {
    // Return true to take the dispatch over completely - the handler then sends
    // nothing itself. That is how an edit can be paced out over several message
    // pump iterations instead of landing in one burst.
    virtual bool OnBeforeSyntheticEdit(
        size_t backspace_count, std::wstring_view chars) = 0;
    // The edit turned out to be "type this one key", so the key is replayed as
    // itself instead of as a unicode packet. The guard MUST be armed for it:
    // a replay that came back unrecognised would be replayed again forever.
    // Returns true when the observer took the dispatch over, exactly as
    // OnBeforeSyntheticEdit does - a key must join a queue that is still
    // draining rather than overtake it.
    virtual bool OnBeforeSyntheticNativeKey(WORD virtual_key) = 0;

protected:
    ~EditDispatchObserver() = default;
};

// Core execution routines for fake backspace inline typing
// Shape of the edit a key would produce, without its text: counts only, so it
// can be logged without ever writing what the user typed into a log file.
struct PlannedEditShape {
    size_t backspaces = 0;
    size_t appended = 0;
    size_t resulting_length = 0;
    bool valid = false;
};

PlannedEditShape PlanFakeBackspaceEditShape(
    const core::Engine& engine,
    wchar_t ch,
    size_t direct_inline_length);

// Reports whether the edit for `ch` would only append `ch` itself - no
// backspaces, no rewritten characters. Such a keystroke gains nothing from being
// converted into a unicode packet, and losing the original virtual key costs the
// host its single-letter shortcuts.
bool IsIdentityAppendEdit(
    const core::Engine& engine,
    wchar_t ch,
    size_t direct_inline_length);

// `identity_replay_virtual_key`, when non-zero, is the virtual key that produced
// `ch`. If the edit turns out to be a plain append of `ch`, that key is replayed
// as itself so the host still sees a real WM_KEYDOWN and can resolve its own
// accelerators; the character reaches the document exactly as before.
bool ProcessFakeBackspaceChar(
    core::Engine& engine,
    wchar_t ch,
    size_t& direct_inline_length,
    HWND target_hwnd = nullptr,
    bool is_direct_post = false,
    HostInputDispatch dispatch = HostInputDispatch::SendToHost,
    EditDispatchObserver* observer = nullptr,
    WORD identity_replay_virtual_key = 0,
    bool prefer_selection_replace = false);

bool ProcessFakeBackspaceBackspace(
    core::Engine& engine,
    size_t& direct_inline_length,
    HWND target_hwnd = nullptr,
    bool is_direct_post = false,
    HostInputDispatch dispatch = HostInputDispatch::SendToHost,
    EditDispatchObserver* observer = nullptr,
    bool prefer_selection_replace = false);

} // namespace vn_ime::fake_backspace
