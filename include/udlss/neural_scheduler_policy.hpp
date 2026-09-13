#pragma once
#include <algorithm>
#include <cstdint>
#include <span>

namespace udlss {
struct NeuralSlotState { std::uint64_t completionValue{}; };
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
