#include "udlss/native_motion_policy.hpp"
#include <cassert>
using namespace udlss;
int main(){
    NativeMotionCandidateMeta good{};
    good.width=1920; good.height=1080; good.format=MotionFormatClass::Rg16Float; good.encoding=NativeMotionEncoding::PixelCurrentToPrevious;
    good.renderTargetWrites=120; good.shaderResourceBinds=4; good.sampledAfterWrite=4; good.framesObserved=8;
    const auto goodScore=scoreNativeMotionCandidate(good,1920,1080);
    assert(goodScore>=80);
    assert(nativeMotionCandidateAutoUsable(good,1920,1080));

    auto color=good; color.format=MotionFormatClass::ColorLike;
    assert(scoreNativeMotionCandidate(color,1920,1080)<50);
    assert(!nativeMotionCandidateAutoUsable(color,1920,1080));

    auto small=good; small.width=512; small.height=512;
    assert(!nativeMotionCandidateAutoUsable(small,1920,1080));

    auto unsampled=good; unsampled.sampledAfterWrite=0; unsampled.shaderResourceBinds=0;
    assert(!nativeMotionCandidateAutoUsable(unsampled,1920,1080));


    auto unity=good; unity.semanticMotion=true; unity.encoding=NativeMotionEncoding::UnityUvPreviousToCurrent;
    assert(nativeMotionCandidateAutoUsable(unity,1920,1080));
    auto unknownSemantic=good; unknownSemantic.semanticMotion=true; unknownSemantic.encoding=NativeMotionEncoding::Unknown;
    unknownSemantic.renderTargetWrites=1; unknownSemantic.shaderResourceBinds=1; unknownSemantic.sampledAfterWrite=1; unknownSemantic.framesObserved=1;
    assert(!nativeMotionCandidateAutoUsable(unknownSemantic,1920,1080));

    auto rg32=good; rg32.format=MotionFormatClass::Rg32Float;
    assert(nativeMotionCandidateAutoUsable(rg32,1920,1080));
    return 0;
}
