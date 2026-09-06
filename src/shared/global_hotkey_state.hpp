#pragma once

#include <cstdint>

namespace vn_ime {

// Owns the decision of when the tray process should claim Alt+Z system-wide
// with RegisterHotKey.
//
// Why the tray process and not the IME DLL: the DLL only ever sees a key when
// a TSF text input context has focus, so the on/off hotkey did nothing on a
// canvas, a toolbar, the desktop or inside a game. A hotkey registered here is
// claimed by the window manager before the key reaches the foreground window,
// so it works everywhere - and because the system swallows the key, the DLL's
// own Alt+Z path can never fire at the same time and toggle twice.
//
// Ctrl+Shift cannot be registered this way (Windows refuses combinations made
// only of modifiers), so that mode keeps the DLL-only behaviour and this state
// simply stays idle.
struct GlobalHotkeyPlan {
    bool register_now = false;
    bool unregister_now = false;
};

class GlobalHotkeyState {
public:
    // Another program may already own Alt+Z (NVIDIA's overlay is the common
    // one). Retry on a slow cadence so the hotkey starts working on its own
    // once that program exits, instead of needing Neokey restarted.
    static constexpr unsigned kRetryIntervalMs = 5000;

    // alt_z_selected mirrors the saved hotkey mode. now_ms is any monotonic
    // millisecond tick (GetTickCount is fine - the subtraction below wraps
    // correctly).
    GlobalHotkeyPlan Evaluate(bool alt_z_selected, unsigned now_ms) noexcept {
        GlobalHotkeyPlan plan;
        if (!alt_z_selected) {
            plan.unregister_now = registered_;
            registered_ = false;
            attempted_ = false;
            conflict_reported_ = false;
            return plan;
        }
        if (registered_) {
            return plan;
        }
        if (attempted_ && (now_ms - last_attempt_ms_) < kRetryIntervalMs) {
            return plan;
        }
        plan.register_now = true;
        return plan;
    }

    // Returns true exactly once per run of failures, so the user is told that
    // Alt+Z is taken without a balloon every five seconds.
    bool OnRegisterResult(bool succeeded, unsigned now_ms) noexcept {
        attempted_ = true;
        last_attempt_ms_ = now_ms;
        registered_ = succeeded;
        if (succeeded) {
            conflict_reported_ = false;
            return false;
        }
        if (conflict_reported_) {
            return false;
        }
        conflict_reported_ = true;
        return true;
    }

    bool registered() const noexcept { return registered_; }

private:
    bool registered_ = false;
    bool attempted_ = false;
    bool conflict_reported_ = false;
    unsigned last_attempt_ms_ = 0;
};

}  // namespace vn_ime
