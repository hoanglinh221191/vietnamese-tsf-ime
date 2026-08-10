#pragma once

#include <cstdint>

namespace vn_ime {

enum class TrayClickEvent : uint8_t {
    LeftButtonDown,
    LeftButtonDoubleClick,
    SingleClickTimerArmFailed,
    SingleClickTimer,
    ForegroundTimer,
};

enum class TrayClickAction : uint8_t {
    None,
    ArmSingleClickTimer,
    CancelSingleClickTimerAndOpenConfig,
    ToggleInputMode,
};

struct TrayClickState {
    bool single_click_pending = false;

    TrayClickAction Advance(TrayClickEvent event) noexcept {
        switch (event) {
            case TrayClickEvent::LeftButtonDown:
                if (single_click_pending) {
                    return TrayClickAction::None;
                }
                single_click_pending = true;
                return TrayClickAction::ArmSingleClickTimer;

            case TrayClickEvent::LeftButtonDoubleClick:
                single_click_pending = false;
                return TrayClickAction::CancelSingleClickTimerAndOpenConfig;

            case TrayClickEvent::SingleClickTimer:
            case TrayClickEvent::SingleClickTimerArmFailed:
                if (!single_click_pending) {
                    return TrayClickAction::None;
                }
                single_click_pending = false;
                return TrayClickAction::ToggleInputMode;

            case TrayClickEvent::ForegroundTimer:
            default:
                return TrayClickAction::None;
        }
    }

    void Reset() noexcept {
        single_click_pending = false;
    }
};

}  // namespace vn_ime
