#pragma once
#include <cstdint>
#include <array>
#include <cmath>
namespace udlss {
enum TemporalFlags : std::uint32_t {
 Temporal_SourceFrameKnown=1, Temporal_CameraKnown=2, Temporal_JitterKnown=4,
 Temporal_CameraCut=8, Temporal_MotionJittered=16, Temporal_ExposureKnown=32
};
// Value-only metadata: safe to carry with an owned snapshot and across IPC.
// An unset flag means unknown, never an identity camera or zero game jitter.
struct TemporalFrame {
 std::uint64_t sourceFrame{};
 std::uint32_t flags{},viewport{};
 std::uint32_t renderWidth{},renderHeight{},outputWidth{},outputHeight{};
 std::uint32_t cropX{},cropY{};
 float jitterX{},jitterY{},exposure{1};
 std::array<float,16> viewToClip{},clipToView{},clipToPrevious{},previousToClip{};
 std::array<float,3> cameraPosition{},cameraUp{},cameraRight{},cameraForward{};
 float nearPlane{},farPlane{},fov{},aspect{};
};
inline bool temporalFramesMatch(const TemporalFrame& a,const TemporalFrame& b){
 if(a.viewport!=b.viewport && a.viewport && b.viewport)return false;
 return !(a.flags&b.flags&Temporal_SourceFrameKnown)||a.sourceFrame==b.sourceFrame;
}
enum class TemporalGuideMerge {Merge,KeepLocal,Replace};
inline void invalidateUnmatchedTemporalMetadata(TemporalFrame& frame){
 frame.flags&=Temporal_CameraCut;
}
inline TemporalGuideMerge chooseTemporalGuideMerge(const TemporalFrame& local,const TemporalFrame& incoming,std::uint32_t localPriority,std::uint32_t incomingPriority){
 if(temporalFramesMatch(local,incoming))return TemporalGuideMerge::Merge;
 return incomingPriority>=localPriority?TemporalGuideMerge::Replace:TemporalGuideMerge::KeepLocal;
}
inline bool temporalDiscontinuity(const TemporalFrame& previous,const TemporalFrame& current){
 if(current.flags&Temporal_CameraCut)return true;
 if((previous.flags^current.flags)&Temporal_SourceFrameKnown)return true;
 if(previous.viewport!=current.viewport)return true;
 if(previous.renderWidth!=current.renderWidth||previous.renderHeight!=current.renderHeight)return true;
 if(previous.outputWidth!=current.outputWidth||previous.outputHeight!=current.outputHeight)return true;
 if(previous.cropX!=current.cropX||previous.cropY!=current.cropY)return true;
 if((previous.flags&current.flags&Temporal_SourceFrameKnown) &&
    static_cast<std::uint32_t>(current.sourceFrame-previous.sourceFrame)!=1)return true;
 return false;
}
inline bool canReusePresentedColorHistory(const TemporalFrame& previous,const TemporalFrame& current){
 // This texture records the last processed Present, independently of the
 // provider's game-frame token. Repeated/skipped/unknown tokens can reset NR
 // without deleting source color at the same output coordinates.
 return !(current.flags&Temporal_CameraCut)&&previous.viewport==current.viewport&&
  previous.outputWidth==current.outputWidth&&previous.outputHeight==current.outputHeight&&
  previous.cropX==current.cropX&&previous.cropY==current.cropY;
}
inline std::array<float,2> canonicalJitterDelta(const TemporalFrame& previous,const TemporalFrame& current,std::uint32_t width,std::uint32_t height){
 if(!(current.flags&Temporal_MotionJittered)||!(previous.flags&current.flags&Temporal_JitterKnown)||
    temporalDiscontinuity(previous,current)||!current.renderWidth||!current.renderHeight)return {};
 const float x=(current.jitterX-previous.jitterX)*width/current.renderWidth;
 const float y=(current.jitterY-previous.jitterY)*height/current.renderHeight;
 return std::isfinite(x)&&std::isfinite(y)?std::array<float,2>{x,y}:std::array<float,2>{};
}
}
