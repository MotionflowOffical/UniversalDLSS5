#pragma once
#include <cstdint>

namespace udlss {

struct PresentQueueProofState {
    std::uintptr_t candidateQueueId{};
    std::uintptr_t trustedQueueId{};
    std::uint32_t consecutiveEvidence{};
    std::uint64_t candidateLastTickMs{};
    std::uint64_t trustedLastTickMs{};
    bool creationProven{};
};

inline void observePresentQueueEvidence(PresentQueueProofState& s,std::uintptr_t queueId,std::uint64_t nowMs,bool exactCreation) {
    if(!queueId) return;
    if(exactCreation) {
        s.candidateQueueId=queueId;
        s.trustedQueueId=queueId;
        s.consecutiveEvidence=2;
        s.candidateLastTickMs=nowMs;
        s.trustedLastTickMs=nowMs;
        s.creationProven=true;
        return;
    }
    if(s.creationProven && s.trustedQueueId==queueId) {
        s.trustedLastTickMs=nowMs;
        return;
    }
    if(!s.creationProven && s.trustedQueueId && s.trustedQueueId!=queueId) s.trustedQueueId=0;
    if(s.candidateQueueId==queueId) ++s.consecutiveEvidence;
    else {
        s.candidateQueueId=queueId;
        s.consecutiveEvidence=1;
    }
    s.candidateLastTickMs=nowMs;
    if(s.consecutiveEvidence>=2) { s.trustedQueueId=queueId; s.trustedLastTickMs=nowMs; }
}

inline bool presentQueueTrusted(const PresentQueueProofState& s,std::uint64_t nowMs,std::uint64_t inferredTtlMs=5000) {
    if(!s.trustedQueueId) return false;
    if(s.creationProven) return true;
    if(nowMs<s.trustedLastTickMs) return false;
    return nowMs-s.trustedLastTickMs<=inferredTtlMs;
}

} // namespace udlss
