#include "udlss/temporal_frame.hpp"
#include "udlss/host_feature_policy.hpp"
#include <iostream>
#include "udlss/snapshot_lease.hpp"
using namespace udlss;
#define CHECK(x) do { if(!(x)){std::cerr<<"failed: " #x "\n";return 1;} } while(0)
int main(){
 auto lease=std::make_shared<int>();CHECK(snapshotLeaseAvailable(lease));
 auto reader=lease;CHECK(!snapshotLeaseAvailable(lease));reader.reset();CHECK(snapshotLeaseAvailable(lease));
 TemporalFrame a{};a.flags=Temporal_SourceFrameKnown;a.sourceFrame=4;a.viewport=1;a.renderWidth=640;a.renderHeight=360;
 auto b=a;b.sourceFrame=5;
 CHECK(!temporalFramesMatch(a,b)); CHECK(temporalFramesMatch(a,a));
 CHECK(chooseTemporalGuideMerge(a,b,3,2)==TemporalGuideMerge::KeepLocal);
 CHECK(chooseTemporalGuideMerge(a,b,2,3)==TemporalGuideMerge::Replace);
 CHECK(chooseTemporalGuideMerge(a,a,2,3)==TemporalGuideMerge::Merge);
 CHECK(!temporalDiscontinuity(a,b));
 b.sourceFrame=6;CHECK(temporalDiscontinuity(a,b));
 b=a;b.renderWidth=800;CHECK(temporalDiscontinuity(a,b));
 b=a;b.viewport=2;CHECK(!temporalFramesMatch(a,b));CHECK(temporalDiscontinuity(a,b));
 b=a;b.flags|=Temporal_CameraCut;CHECK(temporalDiscontinuity(a,b));
 TemporalFrame unknown{};CHECK(temporalFramesMatch(unknown,a));
 HostFeatureKey k{1920,1080,28,0,1};CHECK(canReuseHostFeature(true,k,k));
 auto changed=k;changed.width=1280;CHECK(!canReuseHostFeature(true,k,changed));
 changed=k;changed.preset=1;CHECK(!canReuseHostFeature(true,k,changed));
 CHECK(!canReuseHostFeature(false,k,k));
 a.flags=Temporal_SourceFrameKnown|Temporal_JitterKnown;a.sourceFrame=9;a.jitterX=-.25f;a.jitterY=.25f;
 b=a;b.sourceFrame=10;b.flags|=Temporal_MotionJittered;b.jitterX=.25f;b.jitterY=-.25f;
 auto delta=canonicalJitterDelta(a,b,1280,720);CHECK(delta[0]==1&&delta[1]==-1);
 b.sourceFrame=12;delta=canonicalJitterDelta(a,b,1280,720);CHECK(delta[0]==0&&delta[1]==0);
 // NR must reset for a repeated game-frame token, but the previous presented
 // source color still exists. Do not pulse post/UI masking by discarding it.
 a.outputWidth=1280;a.outputHeight=720;b=a;
 CHECK(temporalDiscontinuity(a,b));CHECK(canReusePresentedColorHistory(a,b));
 b.sourceFrame=12;CHECK(temporalDiscontinuity(a,b));CHECK(canReusePresentedColorHistory(a,b));
 b=a;b.renderWidth=800;CHECK(canReusePresentedColorHistory(a,b));
 b=a;b.flags=0;CHECK(canReusePresentedColorHistory(a,b));
 b=a;b.flags|=Temporal_CameraCut;CHECK(!canReusePresentedColorHistory(a,b));
 b=a;b.viewport=2;CHECK(!canReusePresentedColorHistory(a,b));
 b=a;b.outputWidth=1920;CHECK(!canReusePresentedColorHistory(a,b));
 b=a;b.outputHeight=1080;CHECK(!canReusePresentedColorHistory(a,b));
 b=a;b.cropX=1;CHECK(!canReusePresentedColorHistory(a,b));
 b.flags=Temporal_SourceFrameKnown|Temporal_CameraKnown|Temporal_JitterKnown|Temporal_MotionJittered|Temporal_ExposureKnown;
 invalidateUnmatchedTemporalMetadata(b);CHECK(b.flags==0);
 return 0;
}
