#pragma once
#include "renderer_route_policy.hpp"
namespace udlss {
inline constexpr bool routeNeedsCompatDispatch(RendererRoute route) noexcept {
    return isCompatibilityRendererRoute(route);
}
}
