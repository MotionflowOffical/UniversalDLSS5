#pragma once
#include "renderer_route_policy.hpp"
#include <cstdint>
namespace udlss {
inline constexpr std::uint32_t kLegacyCopyRingSlots=3;
struct LegacyCopyRingCursor {
    std::uint32_t index{};
    constexpr std::uint32_t current() const noexcept { return index; }
    constexpr void advance() noexcept { index=(index+1u)%kLegacyCopyRingSlots; }
};
inline constexpr bool legacyCopyRingAllowed(RendererRoute route) noexcept {
    return route==RendererRoute::CompatD3D9Classic;
}
}
