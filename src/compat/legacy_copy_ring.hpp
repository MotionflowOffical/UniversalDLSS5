#pragma once
#include "udlss/legacy_copy_ring_policy.hpp"
#include "udlss/shared_control.hpp"
#include <d3d9.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <array>
namespace udlss::compat {
class LegacyCopyRing {
public:
    bool initialize(IDirect3DDevice9* d3d9,ID3D11Device* d3d11,ID3D11DeviceContext* context,RuntimeStatus& status);
    bool capture(IDirect3DSurface9* backbuffer,ID3D11Texture2D*& canonical,std::uint32_t& width,std::uint32_t& height,DXGI_FORMAT& format,RuntimeStatus& status);
    bool present(IDirect3DSurface9* backbuffer,RuntimeStatus& status);
    void reset();
    std::uint64_t generation() const noexcept { return generation_; }
private:
    struct Slot {
        Microsoft::WRL::ComPtr<IDirect3DSurface9> systemMemory;
        Microsoft::WRL::ComPtr<IDirect3DSurface9> gpuUpload;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> readback;
    };
    bool ensure(IDirect3DSurface9* backbuffer,RuntimeStatus& status);
    Microsoft::WRL::ComPtr<IDirect3DDevice9> d9_;
    Microsoft::WRL::ComPtr<ID3D11Device> d11_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<IDirect3DSurface9> resolve9_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> canonical_;
    std::array<Slot,kLegacyCopyRingSlots> slots_{};
    LegacyCopyRingCursor cursor_{};
    D3DSURFACE_DESC desc_{};
    DXGI_FORMAT dxgiFormat_{DXGI_FORMAT_UNKNOWN};
    std::uint64_t generation_{};
};
}
