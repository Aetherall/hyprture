#include "Fit.hpp"

#include <cstdio>
#include <cstdlib>

static int failures = 0;

static void expect(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

int main() {
    using Fit::SBox;

    // A wide monitor in a 16:9 box: full width, bars above and below.
    const auto WIDE = Fit::contain({0, 0, 320, 180}, 3440, 1440);
    expect(WIDE.w == 320 && WIDE.x == 0, "wide source fills the width");
    expect(WIDE.h < 180 && WIDE.y > 0 && WIDE.y + WIDE.h / 2 == 90, "wide source centered vertically");

    // A tall window: full height, centered horizontally.
    const auto TALL = Fit::contain({10, 20, 200, 100}, 500, 1000);
    expect(TALL.h == 100 && TALL.w == 50 && TALL.x == 85 && TALL.y == 20, "tall source fits the height");

    // Same aspect: the box itself.
    expect(Fit::contain({5, 5, 192, 108}, 1920, 1080) == SBox{5, 5, 192, 108}, "same aspect fills exactly");

    // Nothing to fit, or nowhere: empty.
    expect(Fit::contain({0, 0, 0, 100}, 10, 10).empty(), "empty destination");
    expect(Fit::contain({0, 0, 100, 100}, 0, 10).empty(), "empty source");

    expect(Fit::offset({1, 2, 3, 4}, 10, 20) == SBox{11, 22, 3, 4}, "offset");
    expect(Fit::intersect({0, 0, 10, 10}, {5, 5, 10, 10}) == SBox{5, 5, 5, 5}, "overlap");
    expect(Fit::intersect({0, 0, 10, 10}, {20, 0, 5, 5}).empty(), "disjoint");

    if (failures == 0)
        std::printf("fit: ok\n");
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
