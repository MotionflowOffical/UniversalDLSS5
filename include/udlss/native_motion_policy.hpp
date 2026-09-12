#pragma once
#include <algorithm>
#include <cstdint>

namespace udlss {

enum class NativeMotionEncoding : std::uint32_t { Unknown=0, PixelCurrentToPrevious=1, UnityUvPreviousToCurrent=2 };

enum class MotionFormatClass : std::uint32_t {
    Unknown=0,
    Rg16Float=1,
    Rg16Snorm=2,
    Rg32Float=3,
    ColorLike=4,
    DepthLike=5,
};

struct NativeMotionCandidateMeta {
    std::uint32_t width{};
    std::uint32_t height{};
    MotionFormatClass format{MotionFormatClass::Unknown};
    std::uint32_t renderTargetWrites{};
    std::uint32_t shaderResourceBinds{};
    std::uint32_t sampledAfterWrite{};
    std::uint32_t framesObserved{};
    std::uint32_t clears{};
    bool depthBound{};
    bool msaa{};
    bool semanticMotion{};
    NativeMotionEncoding encoding{NativeMotionEncoding::Unknown};
};

inline constexpr bool nativeMotionFormatPlausible(MotionFormatClass f) {
    return f==MotionFormatClass::Rg16Float || f==MotionFormatClass::Rg16Snorm || f==MotionFormatClass::Rg32Float;
}

inline std::uint32_t scoreNativeMotionCandidate(const NativeMotionCandidateMeta& m,
                                                std::uint32_t targetWidth,
                                                std::uint32_t targetHeight) {
    if(!targetWidth || !targetHeight || m.msaa || m.depthBound || !nativeMotionFormatPlausible(m.format)) return 0;
    std::uint32_t score=0;
    if(m.width==targetWidth && m.height==targetHeight) score+=45;
    else {
        const auto dw=m.width>targetWidth?m.width-targetWidth:targetWidth-m.width;
        const auto dh=m.height>targetHeight?m.height-targetHeight:targetHeight-m.height;
        if(dw<=targetWidth/16 && dh<=targetHeight/16) score+=20;
        else return 0;
    }
    score += m.format==MotionFormatClass::Rg16Float ? 25u : (m.format==MotionFormatClass::Rg16Snorm ? 22u : 18u);
    if(m.semanticMotion) score+=25;
    if(m.encoding!=NativeMotionEncoding::Unknown) score+=10;
    if(m.renderTargetWrites) score+=10;
    if(m.shaderResourceBinds) score+=8;
    if(m.sampledAfterWrite) score+=12;
    if(m.framesObserved>=3) score+=5;
    if(m.clears) score+=std::min<std::uint32_t>(3,m.clears);
    return std::min<std::uint32_t>(score,100u);
}

inline bool nativeMotionCandidateAutoUsable(const NativeMotionCandidateMeta& m,
                                            std::uint32_t targetWidth,
                                            std::uint32_t targetHeight) {
    const auto score=scoreNativeMotionCandidate(m,targetWidth,targetHeight);
    return score>=80 && m.renderTargetWrites>0 && m.shaderResourceBinds>0 && m.sampledAfterWrite>0 &&
           m.encoding!=NativeMotionEncoding::Unknown &&
           (m.semanticMotion || score>=95);
}

} // namespace udlss
