#pragma once
#include <algorithm>
#include <cstdint>
#include <span>

namespace udlss {

struct RendererCandidate {
    std::uint32_t pid{};
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint32_t api{}; // GraphicsApi numeric value
    float fps{};
    std::uint64_t lastTickMs{};
    bool root{};
    bool neuralActive{};
    bool processing{};
};

inline bool rendererCandidateFresh(const RendererCandidate& c,std::uint64_t nowMs,std::uint64_t graceMs=2500) {
    return c.pid && c.width && c.height && c.lastTickMs && nowMs>=c.lastTickMs && nowMs-c.lastTickMs<=graceMs;
}

inline std::uint64_t rendererScore(const RendererCandidate& c) {
    if(!c.pid || !c.width || !c.height) return 0;
    const std::uint64_t pixels=std::min<std::uint64_t>(static_cast<std::uint64_t>(c.width)*c.height,16'777'216ull);
    std::uint64_t score=pixels;
    if(c.api==12) score+=1'500'000ull;
    else if(c.api==11) score+=500'000ull;
    else if(c.api==10) score+=300'000ull;
    else if(c.api==9) score+=200'000ull;
    else if(c.api==0x1001u) score+=350'000ull;
    else if(c.api==0x1000u) score+=250'000ull;
    if(c.root) score+=600'000ull;
    if(c.processing) score+=250'000ull;
    if(c.neuralActive) score+=250'000ull;
    // Very high-frequency tiny helper/overlay swapchains should not outrank the
    // application's primary renderer simply because they publish more often.
    if(c.fps>240.0f) {
        const auto penalty=std::min<std::uint64_t>(1'000'000ull,static_cast<std::uint64_t>((c.fps-240.0f)*5000.0f));
        score=score>penalty?score-penalty:1;
    }
    if(pixels<640ull*360ull) score/=2;
    return score;
}

inline std::uint32_t electPrimaryRenderer(std::span<const RendererCandidate> candidates,
                                          std::uint32_t currentPid,
                                          std::uint64_t nowMs,
                                          std::uint64_t graceMs=2500) {
    const RendererCandidate* current=nullptr;
    const RendererCandidate* best=nullptr;
    std::uint64_t bestScore=0;
    for(const auto& c:candidates) {
        if(!rendererCandidateFresh(c,nowMs,graceMs)) continue;
        const auto score=rendererScore(c);
        if(c.pid==currentPid) current=&c;
        if(!best || score>bestScore){best=&c;bestScore=score;}
    }
    if(!best) return 0;
    if(current) {
        const auto currentScore=rendererScore(*current);
        // Hysteresis: the incumbent survives ordinary score jitter. A challenger
        // needs a material quality advantage, not merely a newer status write.
        if(best->pid==currentPid || bestScore<=currentScore+currentScore/4+250'000ull)
            return currentPid;
    }
    return best->pid;
}

} // namespace udlss
