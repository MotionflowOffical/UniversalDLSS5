#pragma once
#include "udlss/native_motion_policy.hpp"
#include <d3d12.h>
#include <wrl/client.h>
#include <cstdint>
#include <vector>

namespace udlss::gpu {

struct D3D12MotionCandidate {
    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    NativeMotionCandidateMeta meta{};
    D3D12_RESOURCE_STATES state{D3D12_RESOURCE_STATE_COMMON};
    std::uint64_t stableId{};
    std::uint32_t score{};
    std::uint32_t confidence{};
    bool conventionKnown{};
};

struct D3D12CommandListTouchInfo {
    std::vector<ID3D12Resource*> resources;
    bool legacyBarrier{};
    bool enhancedBarrier{};
    bool presentTransition{};
};

struct D3D12DepthCandidate {
    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    DXGI_FORMAT viewFormat{DXGI_FORMAT_UNKNOWN};
    D3D12_RESOURCE_STATES state{D3D12_RESOURCE_STATE_COMMON};
    std::uint64_t stableId{};
    std::uint32_t score{};
    std::uint32_t confidence{};
    bool inverted{true};
    bool conventionKnown{};
};

class D3D12ResourceTracker {
public:
    void onResourceBarriers(ID3D12GraphicsCommandList* list,UINT count,const D3D12_RESOURCE_BARRIER* barriers);
#if defined(__ID3D12GraphicsCommandList7_INTERFACE_DEFINED__)
    void onEnhancedBarriers(ID3D12GraphicsCommandList* list,UINT32 count,const D3D12_BARRIER_GROUP* groups);
#endif
    void finalizeFrame(ID3D12Device* device,UINT targetWidth,UINT targetHeight);
    D3D12MotionCandidate bestMotionCandidate(ID3D12Device* device,UINT targetWidth,UINT targetHeight) const;
    D3D12DepthCandidate bestDepthCandidate(ID3D12Device* device,UINT targetWidth,UINT targetHeight) const;
    D3D12_RESOURCE_STATES currentState(ID3D12Resource* resource,D3D12_RESOURCE_STATES fallback=D3D12_RESOURCE_STATE_COMMON) const;
    void reset();
    std::vector<ID3D12Resource*> takeCommandListTouches(ID3D12CommandList* commandList);
    D3D12CommandListTouchInfo takeCommandListTouchInfo(ID3D12CommandList* commandList);
};

D3D12ResourceTracker& globalD3D12ResourceTracker();
bool installD3D12ResourceTrackingHooks(ID3D12Device* sampleDevice);
void setD3D12TrackingSuppressed(bool value);
bool d3d12TrackingSuppressed();
void setD3D12QueueTouchCaptureEnabled(bool value);
bool d3d12QueueTouchCaptureEnabled();
void setD3D12SemanticResourceTrackingEnabled(bool value);
bool d3d12SemanticResourceTrackingEnabled();
std::vector<ID3D12Resource*> takeD3D12CommandListTouches(ID3D12CommandList* commandList);
D3D12CommandListTouchInfo takeD3D12CommandListTouchInfo(ID3D12CommandList* commandList);
bool d3d12EnhancedBarrierTrackingAvailable();

} // namespace udlss::gpu
