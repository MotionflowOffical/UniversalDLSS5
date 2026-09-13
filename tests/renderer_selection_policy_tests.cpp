#include "udlss/renderer_selection_policy.hpp"
#include <array>
#include <iostream>
using namespace udlss;
int main(){
    const std::uint64_t now=10'000;
    std::array<RendererCandidate,2> c{{
        {1784,1076,560,11,353.8f,9'990,false,false,false},
        {26472,1920,1080,12,60.0f,9'940,true,true,true}
    }};
    if(electPrimaryRenderer(c,0,now)!=26472){std::cerr<<"helper swapchain won election\n";return 1;}
    // A slightly better/newer challenger cannot cause frame-to-frame ownership flicker.
    std::array<RendererCandidate,2> h{{
        {10,1920,1080,12,60,9'980,true,true,true},
        {11,2048,1152,12,61,9'999,false,true,true}
    }};
    if(electPrimaryRenderer(h,10,now)!=10){std::cerr<<"hysteresis did not retain incumbent\n";return 2;}
    h[0].lastTickMs=7'000;
    if(electPrimaryRenderer(h,10,now)!=11){std::cerr<<"stale incumbent was not replaced\n";return 3;}
    return 0;
}
