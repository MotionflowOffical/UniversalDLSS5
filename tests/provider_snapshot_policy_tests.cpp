#include <cassert>
#include <cstdint>

#if __has_include("udlss/provider_snapshot_policy.hpp")
#include "udlss/provider_snapshot_policy.hpp"
using namespace udlss;
int main(){
    // Injector-owned snapshots are portable across queues only after their
    // private post-submit copy fence has completed.
    assert(recoveryGuideUsable(true, true, true, false, 0x10, 0x20));
    assert(!recoveryGuideUsable(true, true, false, false, 0x10, 0x20));

    // A game-owned Present-safe guide still needs queue provenance in recovery.
    assert(recoveryGuideUsable(true, false, true, true, 0x20, 0x20));
    assert(!recoveryGuideUsable(true, false, true, true, 0x10, 0x20));
    assert(!recoveryGuideUsable(true, false, true, true, 0, 0x20));

    // Native/non-recovery paths do not require recovery queue matching.
    assert(recoveryGuideUsable(false, false, true, true, 0, 0));

    // Owned captures are immutable after the provider copy fence completes and
    // remain reusable across generated/additional Presents. A newer owned
    // generation (or expiry/reset) retires the old ring slot instead.
    assert(!shouldConsumeRecoveryGuide(true, true, false, false));
    assert(!shouldConsumeRecoveryGuide(true, true, true, true));
    assert(shouldConsumeRecoveryGuide(true, false, true, true));
    assert(!shouldConsumeRecoveryGuide(true, false, false, false));
    assert(!shouldConsumeRecoveryGuide(false, true, true, true));

    assert(streamlineConventionFresh(1000));
    assert(streamlineConventionFresh(2000));
    assert(!streamlineConventionFresh(2001));
    assert(streamlineResetFresh(100));
    assert(!streamlineResetFresh(101));

    // A Streamline evaluation command list may safely provide provenance for a
    // recent Present-safe capture only when that capture is still unbound.
    assert(shouldBindStreamlineEvaluationCommandList(true, false, true, 0, 10));
    assert(!shouldBindStreamlineEvaluationCommandList(true, false, true, 0x1234, 10));
    assert(!shouldBindStreamlineEvaluationCommandList(true, true, true, 0, 10));
    assert(!shouldBindStreamlineEvaluationCommandList(false, false, true, 0, 10));
    assert(!shouldBindStreamlineEvaluationCommandList(true, false, false, 0, 10));
    assert(!shouldBindStreamlineEvaluationCommandList(true, false, true, 0, 251));

    // Once an unbound Present-safe capture is associated with evaluation,
    // SnapshotOnly recovery should also queue an owned copy as the preferred
    // path, while retaining the direct capture as a fallback.
    assert(shouldQueueBoundPresentSafeSnapshot(true, true, true));
    assert(!shouldQueueBoundPresentSafeSnapshot(false, true, true));
    assert(!shouldQueueBoundPresentSafeSnapshot(true, false, true));
    assert(!shouldQueueBoundPresentSafeSnapshot(true, true, false));

    // In SnapshotOnly recovery, a completed injector-owned snapshot must win
    // over a newer game-owned Present-safe capture. Otherwise the single live
    // capture slot can be perpetually occupied by an unproven current-frame
    // tag while the safe completed snapshot is never selected.
    assert(ownedSnapshotShouldSupersedeCurrent(true, true, false));
    assert(!ownedSnapshotShouldSupersedeCurrent(true, true, true));
    assert(!ownedSnapshotShouldSupersedeCurrent(false, true, false));
    assert(!ownedSnapshotShouldSupersedeCurrent(true, false, false));
    return 0;
}
#else
int main(){ return 1; }
#endif
