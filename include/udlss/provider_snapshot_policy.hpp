#pragma once
#include <cstdint>

namespace udlss {

// Recovery may consume an injector-owned provider snapshot on any queue once
// the private copy fence has completed. A game-owned resource remains tied to
// the provider/presentation queue relationship even when Streamline declares it
// valid through Present.
inline constexpr bool recoveryGuideUsable(bool recoveryMode,
                                          bool snapshotOwned,
                                          bool snapshotReady,
                                          bool providerPresentSafe,
                                          std::uintptr_t producerQueueId,
                                          std::uintptr_t recoveryQueueId) {
    if(snapshotOwned) return snapshotReady;
    if(!providerPresentSafe) return false;
    if(!recoveryMode) return true;
    return producerQueueId!=0 && recoveryQueueId!=0 && producerQueueId==recoveryQueueId;
}

inline constexpr bool shouldConsumeRecoveryGuide(bool recoveryMode,
                                                 bool snapshotOwned,
                                                 bool usedGuide,
                                                 bool queueSafe) {
    if(!recoveryMode) return false;
    // Injector-owned snapshots are immutable after their copy fence completes.
    // Keep the latest rendered-frame generation alive across multiple Presents
    // (including frame-generation Presents) until a newer snapshot replaces it.
    // publishCaptureLocked retires the old slot when that replacement occurs.
    if(snapshotOwned) return false;
    return usedGuide && queueSafe;
}

inline constexpr bool streamlineConventionFresh(std::uint64_t ageMs) {
    return ageMs<=2000;
}

inline constexpr bool streamlineResetFresh(std::uint64_t ageMs) {
    return ageMs<=100;
}

// Streamline integrations may tag eValidUntilPresent resources without a
// command buffer and supply the actual evaluation command list later through
// slEvaluateFeature. Bind only an unbound, recent, game-owned Present-safe
// capture. Never rewrite provenance that was already explicit and never bind an
// injector-owned snapshot generation.
inline constexpr bool shouldBindStreamlineEvaluationCommandList(bool isStreamline,
                                                                bool snapshotOwned,
                                                                bool providerPresentSafe,
                                                                std::uintptr_t existingCommandListId,
                                                                std::uint64_t ageMs) {
    return isStreamline && !snapshotOwned && providerPresentSafe &&
           existingCommandListId==0 && ageMs<=250;
}

// SnapshotOnly recovery must prefer a completed injector-owned generation over
// the live game-owned capture slot. The live provider may publish another
// Present-safe tag every frame; if normal timestamp ordering is used, that
// newer unproven tag can starve the completed safe snapshot indefinitely.
inline constexpr bool ownedSnapshotShouldSupersedeCurrent(bool snapshotOnlyMode,
                                                          bool incomingSnapshotOwned,
                                                          bool currentSnapshotOwned) {
    return snapshotOnlyMode && incomingSnapshotOwned && !currentSnapshotOwned;
}

inline constexpr bool shouldQueueBoundPresentSafeSnapshot(bool snapshotOnlyMode,
                                                          bool hasD3D12Guide,
                                                          bool commandListBound) {
    return snapshotOnlyMode && hasD3D12Guide && commandListBound;
}

} // namespace udlss
