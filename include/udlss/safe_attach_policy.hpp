#pragma once
#include <cstdint>

namespace udlss {

struct SafeAttachState {
    std::uint64_t firstPresentTickMs{};
    std::uint32_t stablePresents{};
    bool queueCaptured{};
};

inline bool safeAttachReady(const SafeAttachState& s,std::uint64_t nowMs,bool d3d12) {
    if(!s.firstPresentTickMs || nowMs<s.firstPresentTickMs) return false;
    const auto elapsed=nowMs-s.firstPresentTickMs;
    if(d3d12) return s.queueCaptured && s.stablePresents>=8 && elapsed>=350;
    return s.stablePresents>=3 && elapsed>=120;
}

} // namespace udlss
