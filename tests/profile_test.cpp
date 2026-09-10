// Copyright (C) 2026 WideMelon contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "WideMelon.h"
#include <cmath>
#include <iostream>
int main(int argc, char** argv) {
    const int width = std::atoi(argv[1]);
    if (WideMelon::Width() != width) return 1;
    // Projection + wider raster must preserve world scale and reveal extra view.
    constexpr double w = 65536.;
    for (int x : {-50000, -10000, 0, 10000, 50000}) {
        const double actual = (WideMelon::ProjectX(x) / w + 1.) * width / 2.;
        const double expected = (x / w + 1.) * 128. + (width - 256.) / 2.;
        if (std::abs(actual - expected) > 0.01) return 2;
    }
    if (WideMelon::ProjectX(0) != 0) return 3;
    if (width > 256 && WideMelon::ProjectX(70000) >= w) return 4;
    std::cout << "Profile " << width << ": scale and center preserved\n";
}
