#pragma once
#include "renderer_frontend.hpp"
#include "legacy_copy_ring.hpp"
#include <d3d9.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
namespace udlss::compat {
class D3D9Frontend final : public IRendererFrontend {
public:
    bool initialize(IDirect3DDevice9* device,RuntimeStatus& status);
    RendererRoute route() const noexcept override { return route_; }
    CompatInterop interop() const noexcept override { return route_==RendererRoute::CompatD3D9Ex?CompatInterop::D3D9ExShared:CompatInterop::D3D9ClassicTransfer; }
    std::wstring_view name() const noexcept override { return route_==RendererRoute::CompatD3D9Ex?L"D3D9Ex shared-resource frontend":L"classic D3D9 full-resolution transfer frontend"; }
    ID3D11Device* canonicalDevice() const noexcept override { return d11_.Get(); }
    ID3D11DeviceContext* canonicalContext() const noexcept override { return d11Context_.Get(); }
    bool beginFrame(CompatFrame& frame,RuntimeStatus& status) override;
    bool endFrame(const CompatFrame& frame,RuntimeStatus& status) override;
    void reset() override;
private:
    bool createD3D11Device(RuntimeStatus& status);
    bool tryInitializeExSharing(IDirect3DSurface9* backbuffer,RuntimeStatus& status);
    bool waitD3D9(RuntimeStatus& status);
    Microsoft::WRL::ComPtr<IDirect3DDevice9> d9_;
    Microsoft::WRL::ComPtr<IDirect3DDevice9Ex> d9ex_;
    Microsoft::WRL::ComPtr<ID3D11Device> d11_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> d11Context_;
    Microsoft::WRL::ComPtr<IDirect3DTexture9> sharedTexture9_;
    Microsoft::WRL::ComPtr<IDirect3DSurface9> sharedSurface9_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> sharedTexture11_;
    Microsoft::WRL::ComPtr<IDirect3DQuery9> eventQuery_;
    Microsoft::WRL::ComPtr<IDirect3DSurface9> currentBackbuffer_;
    LegacyCopyRing legacyRing_;
    RendererRoute route_{RendererRoute::Unsupported};
    std::uint32_t width_{},height_{};
    DXGI_FORMAT format_{DXGI_FORMAT_UNKNOWN};
    std::uint64_t generation_{};
};
}
