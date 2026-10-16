#pragma once
#include "udlss/d3d12_recovery_policy.hpp"
#include "udlss/present_queue_policy.hpp"
#include "udlss/renderer_route_policy.hpp"
#include <cstdint>

namespace udlss {

struct D3D12RouteDecision {
    bool trusted{};
    bool exactCreationQueue{};
    bool synchronizeBeforePresent{};
    std::uintptr_t queueId{};
    std::uint32_t confidence{};
    RendererRoute route{RendererRoute::Unsupported};
};

inline D3D12RouteDecision chooseD3D12Route(
    const PresentQueueProofState& proof,
    const D3D12RecoveryEvidence& recovery,
    std::uint64_t nowMs, bool externalHost=false) noexcept {
    D3D12RouteDecision out{};
    const bool exact = proof.creationProven && presentQueueTrusted(proof,nowMs);
    if(exact) {
        out.trusted=true;
        out.exactCreationQueue=true;
        // An external host needs owned, shareable guide snapshots even when
        // the presenting queue was captured at swapchain creation. Queue proof
        // establishes ordering; it does not establish cross-device shareability.
        out.synchronizeBeforePresent=externalHost;
        out.queueId=proof.trustedQueueId;
        out.confidence=100;
        out.route=externalHost?RendererRoute::ModernD3D12Recovery:RendererRoute::NativeD3D12;
        return out;
    }
    if(d3d12RecoveryTrusted(recovery)) {
        out.trusted=true;
        out.exactCreationQueue=false;
        out.synchronizeBeforePresent=true;
        out.queueId=recovery.candidateQueue;
        out.confidence=d3d12RecoveryConfidence(recovery);
        out.route=RendererRoute::ModernD3D12Recovery;
    }
    return out;
}

} // namespace udlss
