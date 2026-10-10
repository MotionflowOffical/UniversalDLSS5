#include "udlss/guide_candidate_policy.hpp"
#include <cassert>
using namespace udlss;
int main(){
    assert(guideExtentAspectCompatible(1707,960,2560,1440));
    assert(guideExtentAspectCompatible(1280,720,2560,1440));
    assert(!guideExtentAspectCompatible(512,512,2560,1440));
    // Explicit Streamline/NGX provider guides may be generated at aggressive
    // internal resolutions (for example 1/3 linear scale). Heuristic trackers
    // stay conservative, but provider-tagged guides must not be rejected.
    assert(!guideExtentAspectCompatible(640,360,1920,1080));
    assert(providerGuideExtentCompatible(640,360,1920,1080));
    assert(providerGuideExtentCompatible(960,540,1920,1080));
    assert(!providerGuideExtentCompatible(320,180,1920,1080));

    NativeMotionCandidateMeta mv{};
    mv.width=1707;mv.height=960;mv.format=MotionFormatClass::Rg16Float;
    mv.encoding=NativeMotionEncoding::PixelCurrentToPrevious;
    mv.renderTargetWrites=1;mv.shaderResourceBinds=1;mv.sampledAfterWrite=1;
    assert(temporalMotionAutoUsable(mv,2560,1440,true,6));
    auto weak=mv;weak.encoding=NativeMotionEncoding::Unknown;weak.semanticMotion=false;
    assert(!temporalMotionAutoUsable(weak,2560,1440,true,6));
    assert(temporalMotionAutoUsable(weak,2560,1440,true,20)==(scoreTemporalMotionCandidate(weak,2560,1440,true,20)>=96));

    DepthCandidateMeta depth{1707,960,1,true,true};
    assert(temporalDepthAutoUsable(depth,2560,1440,true,8));
    DepthCandidateMeta shadow{2048,2048,1,true,true};
    assert(!temporalDepthAutoUsable(shadow,2560,1440,true,8));
    return 0;
}
