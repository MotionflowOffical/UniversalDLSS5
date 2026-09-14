#pragma once
#include <algorithm>
#include <cstdint>

namespace udlss {

enum class D3D12QueueEvidenceKind : std::uint32_t {
    GenericExecute = 0,
    RepeatedExecute,
    LegacyBackbufferBarrier,
    EnhancedBackbufferBarrier,
    SwapchainCreation
};

struct D3D12RecoveryEvidence {
    std::uint32_t score{};
    std::uintptr_t candidateQueue{};
    std::uint32_t repeatedMatches{};
    bool directQueue{};
    bool sameDevice{};
    bool backbufferSpecific{};
    bool creationProven{};
    bool presentTransitionSeen{};
};

inline void observeD3D12RecoveryEvidence(
    D3D12RecoveryEvidence& state,
    std::uintptr_t queueId,
    D3D12QueueEvidenceKind kind,
    bool directQueue,
    bool sameDevice,
    bool backbufferSpecific,
    bool presentTransition=true) noexcept {
    if (!queueId) return;
    if (state.candidateQueue != queueId) {
        state = {};
        state.candidateQueue = queueId;
    }
    state.directQueue = state.directQueue || directQueue;
    state.sameDevice = state.sameDevice || sameDevice;
    state.backbufferSpecific = state.backbufferSpecific || backbufferSpecific;
    state.presentTransitionSeen = state.presentTransitionSeen || (backbufferSpecific && presentTransition);

    if (!directQueue || !sameDevice) return;
    switch (kind) {
    case D3D12QueueEvidenceKind::SwapchainCreation:
        if (backbufferSpecific) {
            state.creationProven = true;
            state.score = 100;
            state.repeatedMatches = std::max<std::uint32_t>(state.repeatedMatches, 1u);
        }
        break;
    case D3D12QueueEvidenceKind::EnhancedBackbufferBarrier:
    case D3D12QueueEvidenceKind::LegacyBackbufferBarrier:
        if (backbufferSpecific && presentTransition) {
            state.score = std::min<std::uint32_t>(100u, state.score + 45u);
            ++state.repeatedMatches;
        }
        break;
    case D3D12QueueEvidenceKind::RepeatedExecute:
        if (backbufferSpecific && state.presentTransitionSeen) {
            state.score = std::min<std::uint32_t>(100u, state.score + 20u);
            ++state.repeatedMatches;
        }
        break;
    case D3D12QueueEvidenceKind::GenericExecute:
        // Generic activity is intentionally not evidence that a queue presents
        // a particular swapchain.  UE5 commonly owns several DIRECT queues.
        break;
    }
}

inline std::uint32_t d3d12RecoveryConfidence(const D3D12RecoveryEvidence& state) noexcept {
    if (!state.directQueue || !state.sameDevice || !state.backbufferSpecific || (!state.creationProven && !state.presentTransitionSeen)) return 0;
    return std::min<std::uint32_t>(100u, state.score);
}

inline bool d3d12RecoveryTrusted(const D3D12RecoveryEvidence& state) noexcept {
    return state.creationProven ||
        (state.directQueue && state.sameDevice && state.backbufferSpecific && state.presentTransitionSeen &&
         state.repeatedMatches >= 2u && state.score >= 80u);
}

} // namespace udlss
