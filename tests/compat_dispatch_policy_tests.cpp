#include "udlss/compat_dispatch_policy.hpp"
#include <cassert>
using namespace udlss;
int main(){
    assert(!routeNeedsCompatDispatch(RendererRoute::NativeD3D11));
    assert(!routeNeedsCompatDispatch(RendererRoute::NativeD3D12));
    assert(!routeNeedsCompatDispatch(RendererRoute::ModernD3D12Recovery));
    assert(routeNeedsCompatDispatch(RendererRoute::CompatD3D10));
    assert(routeNeedsCompatDispatch(RendererRoute::CompatD3D9Ex));
    assert(routeNeedsCompatDispatch(RendererRoute::CompatD3D9Classic));
    assert(routeNeedsCompatDispatch(RendererRoute::CompatOpenGL));
    assert(routeNeedsCompatDispatch(RendererRoute::CompatVulkan));
    assert(!routeNeedsCompatDispatch(RendererRoute::Unsupported));
    return 0;
}
