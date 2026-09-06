#pragma once

#include <cstdint>

namespace vn_ime {

enum class HotkeyMode : uint8_t {
    CtrlShift = 0,
    AltZ = 1,
};

enum class HotkeyKey : uint8_t {
    Control,
    Shift,
    Z,
    Other,
};

struct HotkeyModifiers {
    bool alt_down = false;
    bool control_down = false;
    bool shift_down = false;
    // A mouse button went down since this chord began. The key sinks never see
    // the mouse, so without this Ctrl+Shift+click - open a link in a new tab,
    // add to a selection - released as an innocent Ctrl+Shift and toggled.
    bool pointer_pressed = false;
};

struct HotkeyToggleState {
    bool control_down = false;
    bool shift_down = false;
    bool unrelated_key_pressed = false;

    void Reset() noexcept {
        control_down = false;
        shift_down = false;
        unrelated_key_pressed = false;
    }

    [[nodiscard]] bool ShouldClaimTestEvent(
        HotkeyMode mode,
        HotkeyKey key,
        bool is_key_down,
        HotkeyModifiers modifiers = {}) const noexcept {
        if (mode == HotkeyMode::AltZ) {
            return is_key_down && key == HotkeyKey::Z &&
                   modifiers.alt_down && !modifiers.control_down &&
                   !modifiers.shift_down;
        }

        if (key == HotkeyKey::Control || key == HotkeyKey::Shift) {
            return true;
        }
        return false;
    }

    void ObservePassThroughEvent(
        HotkeyMode mode,
        HotkeyKey key,
        bool is_key_down) noexcept {
        if (mode == HotkeyMode::CtrlShift && is_key_down &&
            key != HotkeyKey::Control && key != HotkeyKey::Shift &&
            (control_down || shift_down)) {
            unrelated_key_pressed = true;
        }
    }

    [[nodiscard]] bool DispatchEvent(
        HotkeyMode mode,
        HotkeyKey key,
        bool is_key_down,
        bool was_key_down,
        HotkeyModifiers modifiers = {}) noexcept {
        if (mode == HotkeyMode::AltZ) {
            return is_key_down && !was_key_down && key == HotkeyKey::Z &&
                   modifiers.alt_down && !modifiers.control_down &&
                   !modifiers.shift_down;
        }

        if (is_key_down) {
            if (key == HotkeyKey::Control || key == HotkeyKey::Shift) {
                const bool believed_held = key == HotkeyKey::Control
                    ? control_down
                    : shift_down;
                // A fresh press of a key we still believe is held means its
                // release never reached us: the system took the whole chord
                // (Ctrl+Shift+Esc opens Task Manager) or focus moved while it
                // was down. Everything tracked is then fiction, and leaving it
                // in place used to wedge the hotkey for the rest of the session.
                // was_key_down separates that from plain autorepeat.
                if (believed_held && !was_key_down) {
                    Reset();
                }
                if (!control_down && !shift_down) {
                    unrelated_key_pressed = false;
                }
                if (key == HotkeyKey::Control) {
                    control_down = true;
                } else {
                    shift_down = true;
                }
            } else if (control_down || shift_down) {
                unrelated_key_pressed = true;
            }
            if (modifiers.pointer_pressed &&
                (control_down || shift_down)) {
                unrelated_key_pressed = true;
            }
            return false;
        }

        if (modifiers.pointer_pressed && (control_down || shift_down)) {
            unrelated_key_pressed = true;
        }

        bool should_toggle = false;
        if (key == HotkeyKey::Control) {
            should_toggle = control_down && shift_down &&
                            !unrelated_key_pressed;
            control_down = false;
        } else if (key == HotkeyKey::Shift) {
            should_toggle = control_down && shift_down &&
                            !unrelated_key_pressed;
            shift_down = false;
        }

        if (!control_down && !shift_down) {
            unrelated_key_pressed = false;
        }
        return should_toggle;
    }
};

} // namespace vn_ime
