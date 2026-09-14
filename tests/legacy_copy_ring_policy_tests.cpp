#include "udlss/legacy_copy_ring_policy.hpp"
#include <cassert>
using namespace udlss;
int main(){
    static_assert(kLegacyCopyRingSlots==3);
    LegacyCopyRingCursor c{};
    assert(c.current()==0);
    c.advance();assert(c.current()==1);
    c.advance();assert(c.current()==2);
    c.advance();assert(c.current()==0);
    assert(legacyCopyRingAllowed(RendererRoute::CompatD3D9Classic));
    assert(!legacyCopyRingAllowed(RendererRoute::CompatD3D9Ex));
    assert(!legacyCopyRingAllowed(RendererRoute::NativeD3D11));
    return 0;
}
