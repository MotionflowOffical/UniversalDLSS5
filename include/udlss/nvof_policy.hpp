#pragma once
#include "settings.hpp"
#include <cstdint>
#include <ranges>

namespace udlss {
enum class NvofPerfClass : std::uint32_t { Fast, Medium, Slow };

template <std::ranges::input_range R>
std::uint32_t chooseNvofGrid(std::uint32_t desired, const R& supported) {
    std::uint32_t best = 0;
    std::uint32_t bestDistance = 0xffffffffu;
    for (auto raw : supported) {
        const auto v = static_cast<std::uint32_t>(raw);
        if (v != 1 && v != 2 && v != 4) continue;
        const auto d = v > desired ? v - desired : desired - v;
        if (d < bestDistance || (d == bestDistance && v < best)) {
            best = v;
            bestDistance = d;
        }
    }
    return best;
}

inline NvofPerfClass nvofPerfClass(LatencyMode mode) {
    switch (mode) {
    case LatencyMode::UltraLow: return NvofPerfClass::Fast;
    case LatencyMode::Quality: return NvofPerfClass::Slow;
    default: return NvofPerfClass::Medium;
    }
}
} // namespace udlss
