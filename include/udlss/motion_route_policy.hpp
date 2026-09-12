#pragma once
#include <cstdint>
#include <string_view>
namespace udlss {
enum class MotionRoute : std::uint32_t { AdapterNative=0, TrackedNative=1, CameraDepth=2, Nvof=3, Hlsl=4, Zero=5 };
struct MotionRouteAvailability { bool adapterNative{}; bool trackedNative{}; bool cameraDepth{}; bool nvof{}; bool hlsl{true}; };
inline constexpr MotionRoute chooseMotionRoute(const MotionRouteAvailability& a){
    if(a.adapterNative) return MotionRoute::AdapterNative;
    if(a.trackedNative) return MotionRoute::TrackedNative;
    if(a.cameraDepth) return MotionRoute::CameraDepth;
    if(a.nvof) return MotionRoute::Nvof;
    if(a.hlsl) return MotionRoute::Hlsl;
    return MotionRoute::Zero;
}
inline constexpr const wchar_t* motionRouteLabel(MotionRoute r){
    switch(r){
    case MotionRoute::AdapterNative:return L"GameGuides native motion";
    case MotionRoute::TrackedNative:return L"Tracked game-native velocity";
    case MotionRoute::CameraDepth:return L"Camera + game depth motion";
    case MotionRoute::Nvof:return L"NVIDIA Optical Flow Accelerator";
    case MotionRoute::Hlsl:return L"HLSL optical flow fallback";
    default:return L"Zero motion vectors";
    }
}
}
