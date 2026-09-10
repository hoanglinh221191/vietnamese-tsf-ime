#pragma once

// The V and E marks in the notification area, drawn rather than loaded.
//
// They used to be two baked .ico files, one dark red and one dark blue. Against
// the default Windows 11 taskbar those measured 2.50:1 and 2.29:1 - below the
// 3:1 floor for a user interface graphic, and low enough that the V read as a
// smudge and the E dissolved at 16px. A baked icon can only be right for one
// theme and one DPI, and the taskbar has two of the first and many of the
// second.
//
// Drawing them makes the ink a variable and the size exact. Nothing here talks
// to Windows: this produces coverage values, and the caller turns them into an
// icon.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace vn_ime::tray {

enum class Glyph {
    Vietnamese,  // V
    English,     // E
};

struct Rgb {
    unsigned char r = 0;
    unsigned char g = 0;
    unsigned char b = 0;

    bool operator==(const Rgb&) const = default;
};

// The two taskbars these have to be legible on. Windows 11 tints the taskbar
// with the wallpaper behind it when transparency is on, so the darker and
// lighter ends of that tint are what the ink is checked against, not one value.
inline constexpr Rgb kDarkTaskbar{0x20, 0x20, 0x20};
inline constexpr Rgb kDarkTaskbarTinted{0x3A, 0x3A, 0x3A};
inline constexpr Rgb kLightTaskbar{0xF3, 0xF3, 0xF3};
inline constexpr Rgb kLightTaskbarTinted{0xE6, 0xE6, 0xE6};

// Chosen so the two marks carry the same contrast as each other, not just
// enough of it. A pair where one letter clears the bar and the other scrapes it
// is what makes one of them look thin next to the other.
inline constexpr Rgb kDarkThemeVietnameseInk{0xFF, 0x95, 0x90};
inline constexpr Rgb kDarkThemeEnglishInk{0x8A, 0xB4, 0xF8};
inline constexpr Rgb kLightThemeVietnameseInk{0xB3, 0x26, 0x1E};
inline constexpr Rgb kLightThemeEnglishInk{0x1A, 0x5F, 0xB4};

inline constexpr Rgb GlyphInk(Glyph glyph, bool light_taskbar) noexcept {
    if (light_taskbar) {
        return glyph == Glyph::Vietnamese ? kLightThemeVietnameseInk
                                          : kLightThemeEnglishInk;
    }
    return glyph == Glyph::Vietnamese ? kDarkThemeVietnameseInk
                                      : kDarkThemeEnglishInk;
}

// WCAG relative luminance and contrast ratio. Here so the inks above can be
// checked by a test rather than by eye - the fault being fixed is exactly the
// one an eye signs off on and a number does not.
inline double RelativeLuminance(Rgb colour) noexcept {
    const auto channel = [](unsigned char value) {
        const double v = value / 255.0;
        return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * channel(colour.r) + 0.7152 * channel(colour.g) +
        0.0722 * channel(colour.b);
}

inline double ContrastRatio(Rgb a, Rgb b) noexcept {
    const double la = RelativeLuminance(a);
    const double lb = RelativeLuminance(b);
    const double high = (std::max)(la, lb);
    const double low = (std::min)(la, lb);
    return (high + 0.05) / (low + 0.05);
}

// How thick the strokes are, in whole pixels. Whole, because a 2.4px stroke at
// 16px lands across three pixel columns and reads as a blur rather than a line.
inline int StrokeWidthForSize(int size) noexcept {
    return (std::max)(2, static_cast<int>(std::lround(size * 0.13)));
}

// Puts a centreline where a whole-pixel stroke covers whole pixels: on an
// integer for an even stroke, on a half for an odd one.
inline double SnapCentreline(double value, int stroke) noexcept {
    if (stroke % 2 == 0) {
        return std::round(value);
    }
    return std::floor(value) + 0.5;
}

struct Segment {
    double x0 = 0.0;
    double y0 = 0.0;
    double x1 = 0.0;
    double y1 = 0.0;
};

struct GlyphOutline {
    std::array<Segment, 4> segments{};
    std::size_t segment_count = 0;
    double half_stroke = 0.0;
};

inline GlyphOutline BuildGlyphOutline(Glyph glyph, int size) noexcept {
    GlyphOutline outline;
    if (size <= 0) {
        return outline;
    }

    const int stroke = StrokeWidthForSize(size);
    const double half = stroke / 2.0;
    outline.half_stroke = half;

    const double pad = size * 0.07;
    const double left = SnapCentreline(pad + half, stroke);
    const double right = SnapCentreline(size - pad - half, stroke);
    const double top = SnapCentreline(pad + half, stroke);
    const double bottom = SnapCentreline(size - pad - half, stroke);

    if (glyph == Glyph::Vietnamese) {
        const double apex = SnapCentreline((left + right) / 2.0, stroke);
        outline.segments[0] = {left, top, apex, bottom};
        outline.segments[1] = {right, top, apex, bottom};
        outline.segment_count = 2;
        return outline;
    }

    // The E is set narrower than the V, the way the two are drawn in any
    // typeface. An E filling a V's width stops reading as a letter and starts
    // reading as a stack of bars. It is still far wider than the mark it
    // replaces, which came in at two thirds of the V's width and is why one of
    // them looked thin beside the other.
    constexpr double kEnglishWidthRatio = 0.85;
    const double inset = (right - left) * (1.0 - kEnglishWidthRatio) / 2.0;
    const double e_left = SnapCentreline(left + inset, stroke);
    const double e_right = SnapCentreline(right - inset, stroke);
    const double middle = SnapCentreline((top + bottom) / 2.0, stroke);
    const double middle_right =
        SnapCentreline(e_right - (e_right - e_left) * 0.16, stroke);

    outline.segments[0] = {e_left, top, e_left, bottom};
    outline.segments[1] = {e_left, top, e_right, top};
    outline.segments[2] = {e_left, middle, middle_right, middle};
    outline.segments[3] = {e_left, bottom, e_right, bottom};
    outline.segment_count = 4;
    return outline;
}

inline double DistanceToSegment(
    double px, double py, const Segment& segment) noexcept {
    const double dx = segment.x1 - segment.x0;
    const double dy = segment.y1 - segment.y0;
    const double length_squared = dx * dx + dy * dy;
    if (length_squared == 0.0) {
        return std::hypot(px - segment.x0, py - segment.y0);
    }
    double t = ((px - segment.x0) * dx + (py - segment.y0) * dy) /
        length_squared;
    t = std::clamp(t, 0.0, 1.0);
    return std::hypot(px - (segment.x0 + t * dx), py - (segment.y0 + t * dy));
}

// Coverage per pixel, row-major, 0 to 255. GDI will not antialias a polygon, so
// the edges come from sampling each pixel on a 4x4 grid.
inline constexpr int kSupersample = 4;

inline std::vector<unsigned char> RenderGlyphCoverage(Glyph glyph, int size) {
    if (size <= 0) {
        return {};
    }
    const GlyphOutline outline = BuildGlyphOutline(glyph, size);
    std::vector<unsigned char> coverage(
        static_cast<std::size_t>(size) * static_cast<std::size_t>(size), 0);

    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            int hits = 0;
            for (int sy = 0; sy < kSupersample; ++sy) {
                for (int sx = 0; sx < kSupersample; ++sx) {
                    const double fx = x + (sx + 0.5) / kSupersample;
                    const double fy = y + (sy + 0.5) / kSupersample;
                    for (std::size_t i = 0; i < outline.segment_count; ++i) {
                        if (DistanceToSegment(fx, fy, outline.segments[i]) <=
                            outline.half_stroke) {
                            ++hits;
                            break;
                        }
                    }
                }
            }
            constexpr int kSamples = kSupersample * kSupersample;
            coverage[static_cast<std::size_t>(y) *
                         static_cast<std::size_t>(size) +
                     static_cast<std::size_t>(x)] =
                static_cast<unsigned char>(
                    std::lround(255.0 * hits / kSamples));
        }
    }
    return coverage;
}

}  // namespace vn_ime::tray
