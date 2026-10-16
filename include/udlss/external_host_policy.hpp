#pragma once
#include <cstdint>
#include <string>
#include "settings.hpp"

namespace udlss {

struct NrHostSessionNames {
    std::wstring map;
    std::wstring frameEvent;
    std::wstring stopEvent;
    std::wstring doneEvent;
    std::wstring fence;
    std::wstring color;
    std::wstring output;
    std::wstring motion;
    std::wstring depth;
};

inline NrHostSessionNames makeNrHostSessionNames(std::uint32_t pid, std::uint32_t sequence) {
    const std::wstring base = L"Local\\UniversalDLSS5.NRHost." + std::to_wstring(pid) + L"." + std::to_wstring(sequence);
    return {base, base + L".Frame", base + L".Stop", base + L".Done", base + L".Fence", base + L".Color",
            base + L".Output", base + L".Motion", base + L".Depth"};
}

inline bool nrHostSequenceEnqueued(std::int32_t publishedSeq, std::int32_t requestedSeq) {
    return publishedSeq >= requestedSeq;
}

inline bool nrHostOutputReady(std::int32_t completedSeq, std::int32_t requestedSeq,
                              std::uint64_t gpuCompletedValue, std::uint64_t requestedOutputValue) {
    return completedSeq == requestedSeq && requestedOutputValue != 0 && gpuCompletedValue >= requestedOutputValue;
}

// This is the TOTAL bridge-side latency budget for a frame, including the
// host's CPU command recording/enqueue phase and the exact GPU-output wait.
// Previously the bridge imposed an unrelated fixed 12 ms enqueue timeout before
// this budget, which caused synchronized mode to alternate NR/passthrough.
// Synchronized mode keeps its current frame until exact completion within the
// existing two-second host watchdog. A 250 ms soft deadline rejected every
// output when the neural GPU interval was longer, then discarded that completed
// request during the next Present. Adaptive/async retain their short budgets.
inline constexpr std::uint32_t nrHostOutputWaitBudgetMs(FramePacingMode mode) {
    switch(mode) {
    case FramePacingMode::Synchronized: return 2000;
    case FramePacingMode::Adaptive: return 16;
    case FramePacingMode::Asynchronous: default: return 0;
    }
}

inline constexpr std::uint32_t nrHostRemainingWaitBudgetMs(FramePacingMode mode,std::uint64_t elapsedMs) {
    const auto total=nrHostOutputWaitBudgetMs(mode);
    return elapsedMs>=total?0u:static_cast<std::uint32_t>(total-elapsedMs);
}

inline constexpr std::uint32_t kNrHostWatchdogMs=2000;
inline constexpr std::uint32_t nrHostPendingWaitBudgetMs(FramePacingMode mode,std::uint64_t requestAgeMs) {
    if(mode!=FramePacingMode::Synchronized||requestAgeMs>=kNrHostWatchdogMs)return 0;
    const auto remaining=static_cast<std::uint32_t>(kNrHostWatchdogMs-requestAgeMs);
    return remaining<nrHostOutputWaitBudgetMs(mode)?remaining:nrHostOutputWaitBudgetMs(mode);
}

} // namespace udlss
