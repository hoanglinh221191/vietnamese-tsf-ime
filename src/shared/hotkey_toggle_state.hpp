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
        return is_key_down && (control_down || shift_down);
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
            if (key == HotkeyKey::Control) {
                if (!control_down && !shift_down) {
                    unrelated_key_pressed = false;
                }
                control_down = true;
            } else if (key == HotkeyKey::Shift) {
                if (!control_down && !shift_down) {
                    unrelated_key_pressed = false;
                }
                shift_down = true;
            } else if (control_down || shift_down) {
                unrelated_key_pressed = true;
            }
            return false;
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
