#pragma once
// Reconciles the *observed* NGX NR keys in the research decompilation with
// UniversalDLSS5's guide preconditions. No vendor weights or private entry
// points are used. DXGI values are kept numeric for portable unit tests.
#include <cstdint>
#include <cmath>
namespace udlss::ngxnr {
inline constexpr bool isProviderDepthFormat(std::uint32_t fmt) noexcept {
    switch(fmt) { case 39: case 40: case 41: // R32_TYPELESS / D32_FLOAT / R32_FLOAT
      case 44: case 45: case 46: // R24G8_TYPELESS / D24S8 / R24_UNORM_X8_TYPELESS
      case 53: case 55: case 56: // R16_TYPELESS / D16_UNORM / R16_UNORM
      case 19: case 20: case 21: return true;
      default: return false; }
}
inline constexpr bool isProviderMotionFormat(std::uint32_t fmt) noexcept {
    switch(fmt) { case 10: // RGBA16F - observed Cyberpunk guide
       case 13: // RGBA16 SNORM
       case 16: // RG32F
       case 34: // RG16F
       case 37: // RG16 SNORM
       case 2: return true; // RGBA32F
       default: return false; }
}
inline bool finiteMotionScale(float x, float y) noexcept {
    return std::isfinite(x) && std::isfinite(y) && std::abs(x) <= 65536.f && std::abs(y) <= 65536.f;
}
// Observed decomp contract: Color, Output, MVec, Depth have independent
// subrectangle coordinates, while Reset/DepthInverted/MVecScale[X,Y] are
// scalar frame controls. Presence of a key does not prove requiredness.
struct Rect { std::uint32_t x{},y{},width{},height{}; };
inline constexpr bool rectInside(Rect r,std::uint32_t w,std::uint32_t h) noexcept {
    return r.width>0 && r.height>0 && r.x<=w && r.y<=h &&
        r.width<=w-r.x && r.height<=h-r.y;
}
}
