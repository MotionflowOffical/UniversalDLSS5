#include <cassert>
#include "udlss/swapchain_policy.hpp"
using namespace udlss;
int main(){
    assert(shouldPromotePrimary(false,0,100,1920ull*1080,0));
    assert(!shouldPromotePrimary(true,100,150,1920ull*1080,1920ull*1080));
    assert(shouldPromotePrimary(true,100,2501,1280ull*720,1920ull*1080));
    assert(shouldPromotePrimary(true,100,150,1920ull*1080,640ull*480));
    assert(!shouldPromotePrimary(true,100,150,800ull*600,1920ull*1080));
    return 0;
}
