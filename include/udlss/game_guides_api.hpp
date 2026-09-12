#pragma once
#include <cstdint>
#include <cstddef>

namespace udlss {
constexpr std::uint32_t kGameGuidesAbiV1=1;
constexpr const char kGameGuidesExportV1[]="UdlssGameGuides_GetFrameV1";

enum GameGuideFlagsV1 : std::uint32_t {
    GameGuide_CameraCut=1u<<0,
    GameGuide_DepthInverted=1u<<1,
    GameGuide_DepthConventionKnown=1u<<2,
    GameGuide_MotionPixelCurrentToPrevious=1u<<3,
    GameGuide_ControlMaskIsNrApplication=1u<<4,
};

// Returned COM resource pointers must be AddRef'd by the provider. The bridge
// consumes one reference for the current frame and releases it automatically.
struct GameGuideFrameV1 {
    std::uint32_t size{sizeof(GameGuideFrameV1)};
    std::uint32_t abi{kGameGuidesAbiV1};
    std::uint32_t flags{};
    std::uint32_t reserved{};
    void* depthTexture{};
    void* motionTexture{};
    void* controlMaskTexture{};
    void* normalsTexture{};
    void* albedoTexture{};
    std::uint32_t depthFormat{};
    std::uint32_t motionFormat{};
    std::uint32_t controlMaskFormat{};
    float motionScaleX{1.0f};
    float motionScaleY{1.0f};
    wchar_t providerName[64]{};
};

using GameGuidesGetFrameV1 = bool (*)(void* d3d11Device, void* d3d11Context,
                                      void* backbuffer, GameGuideFrameV1* outFrame);
}
