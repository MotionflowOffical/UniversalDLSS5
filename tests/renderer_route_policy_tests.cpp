#include "udlss/renderer_route_policy.hpp"
#include <cassert>
#include <string_view>
using namespace udlss;
int main(){
    static_assert(isNativeRendererRoute(RendererRoute::NativeD3D11));
    static_assert(isNativeRendererRoute(RendererRoute::NativeD3D12));
    static_assert(!isNativeRendererRoute(RendererRoute::ModernD3D12Recovery));
    static_assert(isCompatibilityRendererRoute(RendererRoute::CompatD3D10));
    static_assert(isCompatibilityRendererRoute(RendererRoute::CompatD3D9Ex));
    static_assert(isCompatibilityRendererRoute(RendererRoute::CompatD3D9Classic));
    static_assert(isCompatibilityRendererRoute(RendererRoute::CompatOpenGL));
    static_assert(isCompatibilityRendererRoute(RendererRoute::CompatVulkan));
    static_assert(!routeAllowsLegacyCopies(RendererRoute::NativeD3D11));
    static_assert(!routeAllowsLegacyCopies(RendererRoute::NativeD3D12));
    static_assert(routeAllowsLegacyCopies(RendererRoute::CompatD3D9Classic));
    assert(rendererRouteName(RendererRoute::ModernD3D12Recovery)==std::wstring_view(L"Modern D3D12 recovery"));
    assert(compatInteropName(CompatInterop::D3D9ClassicTransfer)==std::wstring_view(L"full-resolution legacy transfer ring"));
    assert(detachModeName(DetachMode::DetachedResident)==std::wstring_view(L"detached-resident"));
    return 0;
}
