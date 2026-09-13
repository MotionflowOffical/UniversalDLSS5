#pragma once
#include "settings.hpp"
#include <cstdint>

namespace udlss {

enum class SwapchainPixelClass : std::uint32_t {
    Unknown = 0,
    Unorm8 = 1,
    Srgb8 = 2,
    Rgb10A2 = 3,
    Float16 = 4,
};

enum class SwapchainColorSpaceClass : std::uint32_t {
    Unknown = 0,
    Sdr709 = 1,
    Linear709 = 2,
    Pq2020 = 3,
    Hlg2020 = 4,
};

enum class ColorEncoding : std::uint32_t {
    SdrLinear = 0,
    SdrSrgb = 1,
    Hdr10Pq = 2,
    ScRgb = 3,
    UnsupportedHdr = 4,
};

struct HdrDecision {
    bool hdrActive{};
    bool supported{true};
    ColorEncoding encoding{ColorEncoding::SdrLinear};
};

inline HdrDecision resolveHdrDecision(HdrMode requested,
                                      SwapchainPixelClass pixel,
                                      SwapchainColorSpaceClass colorSpace) {
    const bool pq = colorSpace == SwapchainColorSpaceClass::Pq2020;
    const bool scrgb = colorSpace == SwapchainColorSpaceClass::Linear709;
    const bool hlg = colorSpace == SwapchainColorSpaceClass::Hlg2020;

    if (requested == HdrMode::SDR) {
        return {false, true, pixel == SwapchainPixelClass::Srgb8 ? ColorEncoding::SdrSrgb : ColorEncoding::SdrLinear};
    }

    if (requested == HdrMode::HDR) {
        if (pixel == SwapchainPixelClass::Rgb10A2) return {true, true, ColorEncoding::Hdr10Pq};
        if (pixel == SwapchainPixelClass::Float16) return {true, true, ColorEncoding::ScRgb};
        return {true, false, ColorEncoding::UnsupportedHdr};
    }

    // Auto follows the swapchain's actual color-space contract.  Merely using
    // a 10-bit surface does not imply HDR; many SDR applications use RGB10A2.
    if (pq) {
        if (pixel == SwapchainPixelClass::Rgb10A2) return {true, true, ColorEncoding::Hdr10Pq};
        return {true, false, ColorEncoding::UnsupportedHdr};
    }
    if (scrgb) {
        if (pixel == SwapchainPixelClass::Float16) return {true, true, ColorEncoding::ScRgb};
        return {true, false, ColorEncoding::UnsupportedHdr};
    }
    if (hlg) return {true, false, ColorEncoding::UnsupportedHdr};
    return {false, true, pixel == SwapchainPixelClass::Srgb8 ? ColorEncoding::SdrSrgb : ColorEncoding::SdrLinear};
}

struct ColorTransitionState {
    std::uint64_t signature{};
    std::uint32_t stablePresents{};
    std::uint32_t transitionCount{};
    bool initialized{};
};

inline bool observeColorSignature(ColorTransitionState& state, std::uint64_t signature) {
    if (!state.initialized) {
        state.initialized = true;
        state.signature = signature;
        state.stablePresents = 1;
        return false;
    }
    if (state.signature != signature) {
        state.signature = signature;
        state.stablePresents = 1;
        ++state.transitionCount;
        return true;
    }
    if (state.stablePresents < 0xffffffffu) ++state.stablePresents;
    return false;
}

inline bool colorPipelineStable(const ColorTransitionState& state, std::uint32_t requiredPresents = 4) {
    return state.initialized && state.stablePresents >= requiredPresents;
}

} // namespace udlss
