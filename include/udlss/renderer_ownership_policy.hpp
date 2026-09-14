#pragma once
#include "shared_control.hpp"
#include <atomic>
#include <cstdint>

namespace udlss {

struct RendererOwnershipState {
    GraphicsApi nativeApi{GraphicsApi::Unknown};
};

inline bool isNativeOwnershipApi(GraphicsApi api) noexcept {
    return api==GraphicsApi::D3D11 || api==GraphicsApi::D3D12;
}

inline void observeNativeRenderer(RendererOwnershipState& state,GraphicsApi api) noexcept {
    if(isNativeOwnershipApi(api)) state.nativeApi=api;
}

inline void resetRendererOwnership(RendererOwnershipState& state) noexcept {
    state.nativeApi=GraphicsApi::Unknown;
}

inline bool nativeRendererAllowed(const RendererOwnershipState& state,GraphicsApi api) noexcept {
    return state.nativeApi==GraphicsApi::Unknown || state.nativeApi==api;
}

inline bool compatibilityRendererAllowed(const RendererOwnershipState& state,GraphicsApi) noexcept {
    return state.nativeApi==GraphicsApi::Unknown;
}

namespace bridge_renderer_ownership {
inline std::atomic<std::uint32_t> nativeApi{static_cast<std::uint32_t>(GraphicsApi::Unknown)};
}

inline void resetProcessRendererOwnership() noexcept {
    bridge_renderer_ownership::nativeApi.store(static_cast<std::uint32_t>(GraphicsApi::Unknown),std::memory_order_release);
}

inline void observeProcessNativeRenderer(GraphicsApi api) noexcept {
    if(isNativeOwnershipApi(api))
        bridge_renderer_ownership::nativeApi.store(static_cast<std::uint32_t>(api),std::memory_order_release);
}

inline GraphicsApi processNativeRenderer() noexcept {
    return static_cast<GraphicsApi>(bridge_renderer_ownership::nativeApi.load(std::memory_order_acquire));
}

inline bool processCompatibilityRendererAllowed(GraphicsApi api) noexcept {
    RendererOwnershipState state{};state.nativeApi=processNativeRenderer();
    return compatibilityRendererAllowed(state,api);
}

} // namespace udlss
