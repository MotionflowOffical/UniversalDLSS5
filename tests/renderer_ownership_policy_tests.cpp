#include "udlss/renderer_ownership_policy.hpp"
#include <cassert>
using namespace udlss;
int main(){
    RendererOwnershipState s{};
    assert(compatibilityRendererAllowed(s, GraphicsApi::D3D9));
    observeNativeRenderer(s, GraphicsApi::D3D11);
    assert(s.nativeApi==GraphicsApi::D3D11);
    assert(!compatibilityRendererAllowed(s, GraphicsApi::D3D9));
    assert(!compatibilityRendererAllowed(s, GraphicsApi::OpenGL));
    assert(!compatibilityRendererAllowed(s, GraphicsApi::Vulkan));
    assert(nativeRendererAllowed(s, GraphicsApi::D3D11));
    observeNativeRenderer(s, GraphicsApi::D3D12);
    assert(s.nativeApi==GraphicsApi::D3D12);
    resetRendererOwnership(s);
    assert(s.nativeApi==GraphicsApi::Unknown);
    assert(compatibilityRendererAllowed(s, GraphicsApi::D3D9));
    return 0;
}
