#pragma once
#include "renderer_frontend.hpp"
#include <d3d10_1.h>
#include <dxgi.h>
#include <wrl/client.h>

namespace udlss::compat {
class D3D10Frontend final : public IRendererFrontend {
public:
    bool initialize(IDXGISwapChain* swapchain,RuntimeStatus& status);
    RendererRoute route() const noexcept override { return RendererRoute::CompatD3D10; }
    CompatInterop interop() const noexcept override { return CompatInterop::D3D10Shared; }
    std::wstring_view name() const noexcept override { return L"D3D10 shared-resource frontend"; }
    ID3D11Device* canonicalDevice() const noexcept override { return d11_.Get(); }
    ID3D11DeviceContext* canonicalContext() const noexcept override { return d11Context_.Get(); }
    bool beginFrame(CompatFrame& frame,RuntimeStatus& status) override;
    bool endFrame(const CompatFrame& frame,RuntimeStatus& status) override;
    void reset() override;
private:
    bool ensureResources(ID3D10Texture2D* backbuffer,RuntimeStatus& status);
    Microsoft::WRL::ComPtr<IDXGISwapChain> swapchain_;
    Microsoft::WRL::ComPtr<ID3D10Device> d10_;
    Microsoft::WRL::ComPtr<ID3D11Device> d11_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> d11Context_;
    Microsoft::WRL::ComPtr<ID3D10Texture2D> shared10_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> shared11_;
    Microsoft::WRL::ComPtr<IDXGIKeyedMutex> mutex10_;
    Microsoft::WRL::ComPtr<IDXGIKeyedMutex> mutex11_;
    Microsoft::WRL::ComPtr<ID3D10Texture2D> currentBackbuffer_;
    std::uint32_t width_{},height_{};
    DXGI_FORMAT format_{DXGI_FORMAT_UNKNOWN};
    std::uint64_t generation_{};
    bool d11Locked_{};
};
}
