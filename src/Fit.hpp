#pragma once

// Geometry with no Hyprland in it (tests/fit.cpp builds it alone).

#include <algorithm>

namespace Fit {
    struct SBox {
        double x = 0, y = 0, w = 0, h = 0;

        bool   empty() const {
            return w <= 0 || h <= 0;
        }
        bool operator==(const SBox&) const = default;
    };

    // A source of size (sw, sh) scaled to fit inside `dest`, centered: the
    // whole source shows, with bars along the axis it doesn't fill.
    inline SBox contain(const SBox& dest, double sw, double sh) {
        if (dest.empty() || sw <= 0 || sh <= 0)
            return {};
        const double K = std::min(dest.w / sw, dest.h / sh);
        const double W = sw * K, H = sh * K;
        return {dest.x + (dest.w - W) / 2, dest.y + (dest.h - H) / 2, W, H};
    }

    // `box` moved by (dx, dy).
    inline SBox offset(const SBox& box, double dx, double dy) {
        return {box.x + dx, box.y + dy, box.w, box.h};
    }

    // The part of `a` inside `b` (empty if none).
    inline SBox intersect(const SBox& a, const SBox& b) {
        const double X1 = std::max(a.x, b.x), Y1 = std::max(a.y, b.y);
        const double X2 = std::min(a.x + a.w, b.x + b.w), Y2 = std::min(a.y + a.h, b.y + b.h);
        if (X2 <= X1 || Y2 <= Y1)
            return {};
        return {X1, Y1, X2 - X1, Y2 - Y1};
    }
}
