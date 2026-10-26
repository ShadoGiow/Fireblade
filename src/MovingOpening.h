#pragma once
#include <algorithm>
namespace bf::moving {
inline constexpr float kClip = 2.361111000f;
inline constexpr float kBlendOut = 0.250000000f;
inline constexpr float kLength = kClip + kBlendOut;
inline constexpr float kAnchor = 0.040000000f;
inline constexpr float kContact = 0.666667000f;
inline constexpr float kRamp = 0.350000000f;
inline float Weight(double age) {
    auto smooth = [](float x) { x = std::clamp(x, 0.0f, 1.0f); return x*x*(3.0f-2.0f*x); };
    if (age < 0.0 || age >= kLength) return 0.0f;
    return age < 0.10 ? smooth(static_cast<float>(age / 0.10)) :
        age <= kClip ? 1.0f : 1.0f-smooth(static_cast<float>((age-kClip)/kBlendOut));
}
}
