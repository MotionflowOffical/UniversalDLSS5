#pragma once
#include "udlss/game_guide_policy.hpp"
#include "udlss/native_motion_policy.hpp"
#include <d3d11.h>
#include <d3d12.h>
#include <dxgiformat.h>
#include <wrl/client.h>
#include <cstdint>
#include <string>
#include "udlss/temporal_frame.hpp"
#include "udlss/snapshot_lease.hpp"

namespace udlss::gpu {

enum class GameTemporalGuideCaptureMode : std::uint32_t { Disabled=0, PresentSafeOnly=1, SnapshotOnly=2, Full=3 };

struct CapturedGameGuides {
    std::shared_ptr<void> snapshotLease;
    TemporalFrame temporal{};
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
    bool snapshotOwned{};
    bool depthSnapshotOwned{};
    bool motionSnapshotOwned{};
    std::uint32_t confidence{};
    GameGuideSource source{GameGuideSource::None};
    std::wstring provider;
    std::uint64_t capturedTickMs{};
    std::uint64_t captureSequence{};
    std::uintptr_t providerCommandListId{};
    std::uintptr_t producerQueueId{};
    std::uint32_t providerViewportId{};

    explicit operator bool() const { return depth11||motion11||depth12||motion12; }
};

bool installGameTemporalGuideHooks();
std::wstring gameTemporalGuideHookDiagnostics();
void releaseCapturedGameTemporalGuides();
void consumeCapturedGameTemporalGuides(std::uint64_t captureSequence);
using D3D12ExecuteCommandListsFn = void (STDMETHODCALLTYPE*)(ID3D12CommandQueue*,UINT,ID3D12CommandList* const*);
bool pendingGameTemporalGuideSnapshotActive();
bool snapshotPendingGameTemporalGuidesAfterExecute(ID3D12CommandQueue* queue,ID3D12CommandList* commandList,D3D12ExecuteCommandListsFn originalExecute);
void noteGameTemporalGuideCommandListExecuted(ID3D12CommandList* commandList,std::uintptr_t queueId);
void resetGameTemporalGuides();
CapturedGameGuides snapshotGameTemporalGuides(ID3D11Device* device,std::uint64_t nowMs);
CapturedGameGuides snapshotGameTemporalGuides(ID3D12Device* device,std::uint64_t nowMs);
void setGameGuideCaptureSuppressed(bool value);
bool gameGuideCaptureSuppressed();
void setGameTemporalGuideCaptureEnabled(bool value);
bool gameTemporalGuideCaptureEnabled();
void setGameTemporalGuideCaptureMode(GameTemporalGuideCaptureMode mode);
GameTemporalGuideCaptureMode gameTemporalGuideCaptureMode();

class GameGuideCaptureGuard {
public:
    GameGuideCaptureGuard(){ setGameGuideCaptureSuppressed(true); }
    ~GameGuideCaptureGuard(){ setGameGuideCaptureSuppressed(false); }
    GameGuideCaptureGuard(const GameGuideCaptureGuard&)=delete;
    GameGuideCaptureGuard& operator=(const GameGuideCaptureGuard&)=delete;
};

} // namespace udlss::gpu
