#pragma once

#include <algorithm>

namespace vn_ime {

struct DialogVerticalFit {
    int footer_top = 0;
    int max_scroll = 0;
    bool needs_scroll = false;
};

inline constexpr bool ShouldKeepDialogTemplateChildVisible(
    bool footer_control,
    bool has_visible_style) noexcept {
    return footer_control || has_visible_style;
}

inline constexpr DialogVerticalFit ComputeDialogVerticalFit(
    int original_client_height,
    int original_footer_top,
    int fitted_client_height) noexcept {
    const int footer_height =
        (std::max)(0, original_client_height - original_footer_top);
    const int fitted_footer_top =
        (std::max)(0, fitted_client_height - footer_height);
    const int max_scroll =
        (std::max)(0, original_footer_top - fitted_footer_top);
    return {
        .footer_top = fitted_footer_top,
        .max_scroll = max_scroll,
        .needs_scroll = max_scroll > 0,
    };
}

}  // namespace vn_ime
