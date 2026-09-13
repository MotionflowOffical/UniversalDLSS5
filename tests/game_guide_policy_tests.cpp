#include "udlss/game_guide_policy.hpp"
#include <cassert>
using namespace udlss;
int main(){
    assert(guideSourcePriority(GameGuideSource::GameAdapter) > guideSourcePriority(GameGuideSource::Ngx));
    assert(guideSourcePriority(GameGuideSource::Ngx) > guideSourcePriority(GameGuideSource::Streamline));
    assert(guideSourcePriority(GameGuideSource::Streamline) > guideSourcePriority(GameGuideSource::D3D12Tracker));
    assert(guideSourcePriority(GameGuideSource::D3D12Tracker) > guideSourcePriority(GameGuideSource::D3D11Tracker));
    assert(guideCaptureFresh(1000,1450,500));
    assert(!guideCaptureFresh(1000,1501,500));
    assert(!guideCaptureFresh(0,1200,500));
    return 0;
}
