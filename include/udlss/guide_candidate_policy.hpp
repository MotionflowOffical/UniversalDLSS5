#pragma once
#include "native_motion_policy.hpp"
#include "resource_extraction_policy.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace udlss {

inline bool guideExtentAspectCompatible(std::uint32_t width,std::uint32_t height,
                                        std::uint32_t targetWidth,std::uint32_t targetHeight) {
    if(!width||!height||!targetWidth||!targetHeight) return false;
    const double aspect=double(width)/double(height);
    const double targetAspect=double(targetWidth)/double(targetHeight);
    const double aspectError=std::abs(aspect-targetAspect)/std::max(0.001,targetAspect);
    if(aspectError>0.035) return false;
    const double sx=double(width)/double(targetWidth);
    const double sy=double(height)/double(targetHeight);
    // Dynamic-resolution engines commonly run between ~50% and 100% linear
    // resolution. Half-resolution guide buffers are also common.
    return sx>=0.45 && sx<=1.05 && sy>=0.45 && sy<=1.05;
}

inline std::uint32_t scoreTemporalMotionCandidate(const NativeMotionCandidateMeta& m,
                                                  std::uint32_t targetWidth,
                                                  std::uint32_t targetHeight,
                                                  bool transitionWriteRead,
                                                  std::uint32_t consecutiveFrames) {
    if(m.msaa || m.depthBound || !nativeMotionFormatPlausible(m.format) ||
       !guideExtentAspectCompatible(m.width,m.height,targetWidth,targetHeight)) return 0;
    std::uint32_t score=0;
    const bool exact=m.width==targetWidth && m.height==targetHeight;
    score += exact ? 30u : 18u;
    score += m.format==MotionFormatClass::Rg16Float ? 24u : (m.format==MotionFormatClass::Rg16Snorm ? 20u : 18u);
    if(m.semanticMotion) score+=24u;
    if(m.encoding!=NativeMotionEncoding::Unknown) score+=10u;
    if(m.renderTargetWrites) score+=6u;
    if(m.shaderResourceBinds) score+=6u;
    if(m.sampledAfterWrite) score+=8u;
    if(transitionWriteRead) score+=16u;
    if(consecutiveFrames>=3) score+=5u;
    if(consecutiveFrames>=12) score+=5u;
    return std::min<std::uint32_t>(100u,score);
}

inline bool temporalMotionAutoUsable(const NativeMotionCandidateMeta& m,
                                     std::uint32_t targetWidth,
                                     std::uint32_t targetHeight,
                                     bool transitionWriteRead,
                                     std::uint32_t consecutiveFrames) {
    const auto score=scoreTemporalMotionCandidate(m,targetWidth,targetHeight,transitionWriteRead,consecutiveFrames);
    // Heuristic D3D12 tracking may not know the exact encoding. Only auto-use
    // unknown encodings at very high confidence; game-supplied integrations
    // should provide the convention explicitly.
    if(m.encoding==NativeMotionEncoding::Unknown)
        return score>=96 && transitionWriteRead && consecutiveFrames>=12;
    return score>=82 && transitionWriteRead && consecutiveFrames>=3;
}

inline int scoreTemporalDepthCandidate(const DepthCandidateMeta& c,
                                       std::uint32_t targetWidth,
                                       std::uint32_t targetHeight,
                                       bool transitionedDepthToRead,
                                       std::uint32_t consecutiveFrames) {
    if(!c.depthFormatSupported || !c.boundAsDepth || c.sampleCount!=1 ||
       !guideExtentAspectCompatible(c.width,c.height,targetWidth,targetHeight)) return -100000;
    int score=7000;
    if(c.width==targetWidth && c.height==targetHeight) score+=1800;
    else score+=900;
    if(transitionedDepthToRead) score+=1400;
    score+=static_cast<int>(std::min<std::uint32_t>(consecutiveFrames,20u))*30;
    return score;
}

inline bool temporalDepthAutoUsable(const DepthCandidateMeta& c,
                                    std::uint32_t targetWidth,
                                    std::uint32_t targetHeight,
                                    bool transitionedDepthToRead,
                                    std::uint32_t consecutiveFrames) {
    return scoreTemporalDepthCandidate(c,targetWidth,targetHeight,transitionedDepthToRead,consecutiveFrames)>=9000;
}

} // namespace udlss
