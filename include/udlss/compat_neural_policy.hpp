#pragma once
#include "renderer_route_policy.hpp"
#include "runtime_diagnostics.hpp"

namespace udlss {

inline constexpr NeuralExecutionLocation selectCompatibilityNeuralLocation(
    bool processIs64Bit, RendererRoute route, bool externalHostAvailable) noexcept {
    if (route == RendererRoute::Unknown || route == RendererRoute::Unsupported) return NeuralExecutionLocation::Unknown;
    if (!processIs64Bit) return externalHostAvailable ? NeuralExecutionLocation::ExternalHost : NeuralExecutionLocation::Unknown;
    if (route == RendererRoute::ModernD3D12Recovery)
        return externalHostAvailable ? NeuralExecutionLocation::ExternalHost : NeuralExecutionLocation::Unknown;
    return NeuralExecutionLocation::InGame;
}

} // namespace udlss
