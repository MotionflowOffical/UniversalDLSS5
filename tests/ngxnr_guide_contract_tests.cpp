#include "udlss/ngxnr_guide_contract.hpp"
#include <cassert>
#include <limits>
int main() {
 using namespace udlss::ngxnr;
 static_assert(isProviderDepthFormat(39));
 static_assert(isProviderMotionFormat(10));
 static_assert(rectInside({0,0,1114,627},1114,627));
 static_assert(!rectInside({0,0,1115,627},1114,627));
 static_assert(!rectInside({4294967290u,0,20,2},1920,1080));
 assert(finiteMotionScale(1920.f,-1080.f));
 assert(!finiteMotionScale(std::numeric_limits<float>::quiet_NaN(),1.f));
 assert(!finiteMotionScale(1e30f,1.f));
}
