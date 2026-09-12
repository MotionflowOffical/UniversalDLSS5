#include <cassert>
#include "udlss/resource_extraction_policy.hpp"
#include "udlss/game_guides_api.hpp"

using namespace udlss;

int main(){
    DepthCandidateMeta mainDepth{1920,1080,1,true,true};
    DepthCandidateMeta shadow{2048,2048,1,true,true};
    DepthCandidateMeta msaa{1920,1080,4,true,true};
    DepthCandidateMeta color{1920,1080,1,false,true};
    assert(depthCandidateScore(mainDepth,1920,1080) > depthCandidateScore(shadow,1920,1080));
    assert(depthCandidateScore(mainDepth,1920,1080) > depthCandidateScore(msaa,1920,1080));
    assert(depthCandidateScore(mainDepth,1920,1080) > depthCandidateScore(color,1920,1080));
    assert(depthCandidateUsable(mainDepth,1920,1080));
    assert(!depthCandidateUsable(msaa,1920,1080));
    assert(!depthCandidateUsable(shadow,1920,1080));

    GameGuideFrameV1 frame{};
    assert(frame.abi == kGameGuidesAbiV1);
    assert(frame.size == sizeof(GameGuideFrameV1));
    return 0;
}
