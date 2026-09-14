#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <cstdint>

namespace udlss::compat {
class CanonicalSurface {
public:
    bool ensure(ID3D11Device* device,std::uint32_t width,std::uint32_t height,DXGI_FORMAT format,UINT bindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET|D3D11_BIND_UNORDERED_ACCESS,UINT miscFlags=0);
    void reset();
    ID3D11Texture2D* texture() const noexcept { return texture_.Get(); }
    std::uint32_t width() const noexcept { return width_; }
    std::uint32_t height() const noexcept { return height_; }
    DXGI_FORMAT format() const noexcept { return format_; }
    std::uint64_t generation() const noexcept { return generation_; }
private:
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture_;
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    std::uint32_t width_{},height_{};
    DXGI_FORMAT format_{DXGI_FORMAT_UNKNOWN};
    UINT bindFlags_{},miscFlags_{};
    std::uint64_t generation_{};
};
}
