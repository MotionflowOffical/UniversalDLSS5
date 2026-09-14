#pragma once
#include "renderer_frontend.hpp"
#include "udlss/compat_dispatch_policy.hpp"
#include "udlss/settings.hpp"
#include "../gpu/d3d11_pipeline.hpp"
#include <memory>
#include <string>

namespace udlss::compat {
class CompatDispatch {
public:
    bool process(IRendererFrontend& frontend,const Settings& settings,const std::wstring& moduleDir,const std::wstring& runtime,RuntimeStatus& status);
    void reset();
private:
    std::unique_ptr<gpu::D3D11Pipeline> pipeline_;
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    RendererRoute route_{RendererRoute::Unknown};
    std::uint64_t compatFrames_{};
    std::uint64_t legacyCopies_{};
    std::uint64_t externalHostFrames_{};
};
}
