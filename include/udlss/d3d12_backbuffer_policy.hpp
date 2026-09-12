#pragma once
namespace udlss {
// Flip-model D3D12 swapchains rotate resource identities every frame. Resource
// identity changes alone must not invalidate temporal history; only a true
// size/format lifecycle reset should do that.
inline constexpr bool shouldResetTemporalHistoryForBackbuffer(bool resourceChanged, bool dimensionsChanged) {
    (void)resourceChanged;
    return dimensionsChanged;
}
} // namespace udlss
