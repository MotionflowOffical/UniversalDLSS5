#pragma once
#include "udlss/native_motion_policy.hpp"
#include <d3d11.h>
#include <wrl/client.h>
#include <cstdint>

namespace udlss::gpu {

struct NativeMotionCandidate {
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    NativeMotionCandidateMeta meta{};
    std::uint64_t stableId{};
    std::uint32_t score{};
};

struct TrackedDepthCandidate {
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    DXGI_FORMAT viewFormat{DXGI_FORMAT_UNKNOWN};
    float clearDepth{1.0f};
    bool clearKnown{};
    std::uint32_t drawUses{};
    std::uint64_t stableId{};
    std::uint32_t score{};
};

class D3D11ResourceTracker {
public:
    void onRenderTargets(ID3D11DeviceContext* context,UINT count,ID3D11RenderTargetView* const* rtvs,ID3D11DepthStencilView* dsv);
    enum class ShaderStage : std::uint32_t { Pixel=0, Compute=1 };
    void onShaderCreated(ShaderStage stage,const void* bytecode,SIZE_T length,IUnknown* shader);
    void onShaderBound(ID3D11DeviceContext* context,ShaderStage stage,IUnknown* shader);
    void onShaderResources(ID3D11DeviceContext* context,ShaderStage stage,UINT start,UINT count,ID3D11ShaderResourceView* const* srvs);
    void onDraw(ID3D11DeviceContext* context);
    void onClearRenderTarget(ID3D11RenderTargetView* rtv);
    void onClearDepthStencil(ID3D11DepthStencilView* dsv,UINT clearFlags,float clearDepth);
    void finalizeFrame(ID3D11Device* device,UINT width,UINT height);
    NativeMotionCandidate bestMotionCandidate(ID3D11Device* device,UINT width,UINT height) const;
    TrackedDepthCandidate bestDepthCandidate(ID3D11Device* device,UINT width,UINT height) const;
    void reset();
};

D3D11ResourceTracker& globalD3D11ResourceTracker();
bool installD3D11ResourceTrackingHooks(ID3D11DeviceContext* sampleContext);
void setD3D11TrackingSuppressed(bool value);
bool d3d11TrackingSuppressed();

} // namespace udlss::gpu
