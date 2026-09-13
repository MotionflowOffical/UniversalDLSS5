#pragma once
#include <algorithm>
#include <cstdint>
#include <span>
#include "settings.hpp"

namespace udlss {
struct NeuralSlotState { std::uint64_t completionValue{}; };
struct PresentationDecision {
    bool useCached{};
    bool waitForCurrent{};
};

inline constexpr bool shouldCacheCurrentNeuralOutput(FramePacingMode mode) {
    return mode!=FramePacingMode::Synchronized;
}

inline constexpr bool needsAsyncNeuralCache(FramePacingMode mode) {
    return shouldCacheCurrentNeuralOutput(mode);
}

inline constexpr bool needsNeuralRefinementScratch(std::uint32_t requestedPasses) {
    return requestedPasses>1;
}

inline constexpr bool needsNeuralControlMask(bool enabled,bool supplied) {
    return enabled&&supplied;
}

inline PresentationDecision choosePresentation(FramePacingMode mode,bool cacheValid,std::uint32_t outputAgeFrames) {
    switch(mode) {
    case FramePacingMode::Synchronized:
        return {false,true};
    case FramePacingMode::Adaptive:
        if(cacheValid && outputAgeFrames<=1) return {true,false};
        return {false,true};
    case FramePacingMode::Asynchronous:
    default:
        return {cacheValid,false};
    }
}

inline bool shouldForceTemporalResetForWeakGuides(MotionSource resolvedMotionSource,
                                                  bool realDepth,
                                                  bool trustedMotion,
                                                  bool cameraMotionReady) {
    return resolvedMotionSource==MotionSource::Zero && !realDepth && !trustedMotion && !cameraMotionReady;
}

struct NeuralScheduleDecision {
    int submitSlot{-1};
    std::uint32_t activeSlots{};
    bool grew{};
    bool backpressured{};
};

inline NeuralScheduleDecision chooseNeuralSlot(std::span<const NeuralSlotState> slots,
                                               std::uint64_t completedValue,
                                               std::uint32_t activeSlots,
                                               std::uint32_t maxSlots) {
    NeuralScheduleDecision out{};
    if(slots.empty()) { out.backpressured=true; return out; }
    maxSlots=std::clamp<std::uint32_t>(maxSlots,1,static_cast<std::uint32_t>(slots.size()));
    activeSlots=std::clamp<std::uint32_t>(activeSlots,1,maxSlots);
    out.activeSlots=activeSlots;
    for(std::uint32_t i=0;i<activeSlots;++i) {
        if(slots[i].completionValue==0 || slots[i].completionValue<=completedValue) {
            out.submitSlot=static_cast<int>(i);
            return out;
        }
    }
    if(activeSlots<maxSlots) {
        out.submitSlot=static_cast<int>(activeSlots);
        out.activeSlots=activeSlots+1;
        out.grew=true;
        return out;
    }
    out.backpressured=true;
    return out;
}
}
