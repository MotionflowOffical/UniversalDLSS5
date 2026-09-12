#pragma once
#include <cstdint>
#include <algorithm>
#include <cmath>

namespace udlss {
struct DepthCandidateMeta {
    std::uint32_t width{}, height{}, sampleCount{1};
    bool depthFormatSupported{};
    bool boundAsDepth{};
};

inline int depthCandidateScore(const DepthCandidateMeta& c, std::uint32_t targetW, std::uint32_t targetH) {
    if (!c.depthFormatSupported || !c.boundAsDepth || c.width==0 || c.height==0) return -100000;
    int score=0;
    if (c.width==targetW && c.height==targetH) score+=10000;
    else {
        const double sx=targetW?double(c.width)/targetW:0.0;
        const double sy=targetH?double(c.height)/targetH:0.0;
        score-=int((std::abs(sx-1.0)+std::abs(sy-1.0))*4000.0);
    }
    if (c.sampleCount==1) score+=1000; else score-=int(c.sampleCount)*500;
    const std::uint64_t area=std::uint64_t(c.width)*c.height;
    const std::uint64_t target=std::uint64_t(targetW)*targetH;
    if (target && area>target*2) score-=2000; // typical shadow-map/atlas penalty
    return score;
}

inline bool depthCandidateUsable(const DepthCandidateMeta& c, std::uint32_t targetW, std::uint32_t targetH) {
    return c.depthFormatSupported && c.boundAsDepth && c.sampleCount==1 && c.width==targetW && c.height==targetH;
}
}
