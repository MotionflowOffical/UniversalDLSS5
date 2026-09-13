#pragma once
#include "udlss/game_guide_policy.hpp"
#include "udlss/native_motion_policy.hpp"
#include <d3d11.h>
#include <d3d12.h>
#include <dxgiformat.h>
#include <wrl/client.h>
#include <cstdint>
#include <string>

namespace udlss::gpu {

struct CapturedGameGuides {
    Microsoft::WRL::ComPtr<ID3D11Texture2D> depth11;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> motion11;
    Microsoft::WRL::ComPtr<ID3D12Resource> depth12;
    Microsoft::WRL::ComPtr<ID3D12Resource> motion12;
    DXGI_FORMAT depthFormat{DXGI_FORMAT_UNKNOWN};
    DXGI_FORMAT motionFormat{DXGI_FORMAT_UNKNOWN};
    D3D12_RESOURCE_STATES depthState{D3D12_RESOURCE_STATE_COMMON};
    D3D12_RESOURCE_STATES motionState{D3D12_RESOURCE_STATE_COMMON};
    std::uint32_t depthWidth{},depthHeight{};
    std::uint32_t motionWidth{},motionHeight{};
    float motionToPixelScaleX{1.0f},motionToPixelScaleY{1.0f};
    bool depthInverted{true};
    bool depthConventionKnown{};
    bool motionConventionKnown{};
    bool cameraCut{};
    bool validUntilPresent{};
    std::uint32_t confidence{};
    GameGuideSource source{GameGuideSource::None};
    std::wstring provider;
    std::uint64_t capturedTickMs{};

    explicit operator bool() const { return depth11||motion11||depth12||motion12; }
};

bool installGameTemporalGuideHooks();
void resetGameTemporalGuides();
CapturedGameGuides snapshotGameTemporalGuides(ID3D11Device* device,std::uint64_t nowMs);
CapturedGameGuides snapshotGameTemporalGuides(ID3D12Device* device,std::uint64_t nowMs);
void setGameGuideCaptureSuppressed(bool value);
bool gameGuideCaptureSuppressed();

class GameGuideCaptureGuard {
public:
    GameGuideCaptureGuard(){ setGameGuideCaptureSuppressed(true); }
    ~GameGuideCaptureGuard(){ setGameGuideCaptureSuppressed(false); }
    GameGuideCaptureGuard(const GameGuideCaptureGuard&)=delete;
    GameGuideCaptureGuard& operator=(const GameGuideCaptureGuard&)=delete;
};

} // namespace udlss::gpu
