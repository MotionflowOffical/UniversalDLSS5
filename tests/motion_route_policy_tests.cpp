#include "udlss/motion_route_policy.hpp"
#include <cassert>
using namespace udlss;
int main(){
    MotionRouteAvailability a{};
    a.adapterNative=true;a.trackedNative=true;a.cameraDepth=true;a.nvof=true;a.hlsl=true;
    assert(chooseMotionRoute(a)==MotionRoute::AdapterNative);
    a.adapterNative=false;assert(chooseMotionRoute(a)==MotionRoute::TrackedNative);
    a.trackedNative=false;assert(chooseMotionRoute(a)==MotionRoute::CameraDepth);
    a.cameraDepth=false;assert(chooseMotionRoute(a)==MotionRoute::Nvof);
    a.nvof=false;assert(chooseMotionRoute(a)==MotionRoute::Hlsl);
    a.hlsl=false;assert(chooseMotionRoute(a)==MotionRoute::Zero);
    assert(std::wstring_view(motionRouteLabel(MotionRoute::CameraDepth))==L"Camera + game depth motion");
    return 0;
}
