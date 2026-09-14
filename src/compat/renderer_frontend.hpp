#pragma once
#include "udlss/renderer_route_policy.hpp"
#include "udlss/shared_control.hpp"
#include "udlss/hdr_policy.hpp"
#include <d3d11.h>
#include <cstdint>
#include <string_view>

namespace udlss::compat {

struct CompatFrame {
    ID3D11Texture2D* color{};
    std::uint32_t width{};
    std::uint32_t height{};
    DXGI_FORMAT format{DXGI_FORMAT_UNKNOWN};
    ColorEncoding colorEncoding{ColorEncoding::SdrLinear};
    std::uint64_t generation{};
    std::uint32_t legacyCopies{};
};

class IRendererFrontend {
public:
    virtual ~IRendererFrontend() = default;
    virtual RendererRoute route() const noexcept = 0;
    virtual CompatInterop interop() const noexcept = 0;
    virtual std::wstring_view name() const noexcept = 0;
    virtual ID3D11Device* canonicalDevice() const noexcept = 0;
    virtual ID3D11DeviceContext* canonicalContext() const noexcept = 0;
    virtual bool beginFrame(CompatFrame& frame, RuntimeStatus& status) = 0;
    virtual bool endFrame(const CompatFrame& frame, RuntimeStatus& status) = 0;
    virtual void reset() = 0;
};

} // namespace udlss::compat
