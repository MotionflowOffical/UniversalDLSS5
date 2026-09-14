#include "udlss/d3d12_recovery_policy.hpp"
#include <cassert>
using namespace udlss;
int main(){
    D3D12RecoveryEvidence generic{};
    observeD3D12RecoveryEvidence(generic,0x1000,D3D12QueueEvidenceKind::GenericExecute,true,true,false);
    assert(!d3d12RecoveryTrusted(generic));
    assert(d3d12RecoveryConfidence(generic)==0);

    D3D12RecoveryEvidence created{};
    observeD3D12RecoveryEvidence(created,0x2000,D3D12QueueEvidenceKind::SwapchainCreation,true,true,true);
    assert(d3d12RecoveryTrusted(created));
    assert(created.candidateQueue==0x2000);
    assert(d3d12RecoveryConfidence(created)==100);

    D3D12RecoveryEvidence legacy{};
    observeD3D12RecoveryEvidence(legacy,0x3000,D3D12QueueEvidenceKind::LegacyBackbufferBarrier,true,true,true);
    assert(!d3d12RecoveryTrusted(legacy));
    observeD3D12RecoveryEvidence(legacy,0x3000,D3D12QueueEvidenceKind::RepeatedExecute,true,true,true);
    assert(!d3d12RecoveryTrusted(legacy));
    observeD3D12RecoveryEvidence(legacy,0x3000,D3D12QueueEvidenceKind::LegacyBackbufferBarrier,true,true,true);
    assert(d3d12RecoveryTrusted(legacy));
    assert(d3d12RecoveryConfidence(legacy)>=80);

    D3D12RecoveryEvidence enhanced{};
    observeD3D12RecoveryEvidence(enhanced,0x4000,D3D12QueueEvidenceKind::EnhancedBackbufferBarrier,true,true,true);
    assert(!d3d12RecoveryTrusted(enhanced));
    observeD3D12RecoveryEvidence(enhanced,0x4000,D3D12QueueEvidenceKind::EnhancedBackbufferBarrier,true,true,true);
    assert(d3d12RecoveryTrusted(enhanced));

    D3D12RecoveryEvidence wrongType{};
    observeD3D12RecoveryEvidence(wrongType,0x5000,D3D12QueueEvidenceKind::EnhancedBackbufferBarrier,false,true,true);
    observeD3D12RecoveryEvidence(wrongType,0x5000,D3D12QueueEvidenceKind::EnhancedBackbufferBarrier,false,true,true);
    assert(!d3d12RecoveryTrusted(wrongType));

    D3D12RecoveryEvidence wrongDevice{};
    observeD3D12RecoveryEvidence(wrongDevice,0x6000,D3D12QueueEvidenceKind::LegacyBackbufferBarrier,true,false,true);
    observeD3D12RecoveryEvidence(wrongDevice,0x6000,D3D12QueueEvidenceKind::LegacyBackbufferBarrier,true,false,true);
    assert(!d3d12RecoveryTrusted(wrongDevice));


    D3D12RecoveryEvidence nonPresent{};
    observeD3D12RecoveryEvidence(nonPresent,0x9000,D3D12QueueEvidenceKind::EnhancedBackbufferBarrier,true,true,true,false);
    observeD3D12RecoveryEvidence(nonPresent,0x9000,D3D12QueueEvidenceKind::EnhancedBackbufferBarrier,true,true,true,false);
    assert(!d3d12RecoveryTrusted(nonPresent));
    assert(d3d12RecoveryConfidence(nonPresent)==0);

    D3D12RecoveryEvidence presentTransition{};
    observeD3D12RecoveryEvidence(presentTransition,0xA000,D3D12QueueEvidenceKind::EnhancedBackbufferBarrier,true,true,true,true);
    observeD3D12RecoveryEvidence(presentTransition,0xA000,D3D12QueueEvidenceKind::EnhancedBackbufferBarrier,true,true,true,true);
    assert(d3d12RecoveryTrusted(presentTransition));

    D3D12RecoveryEvidence switched{};
    observeD3D12RecoveryEvidence(switched,0x7000,D3D12QueueEvidenceKind::LegacyBackbufferBarrier,true,true,true);
    observeD3D12RecoveryEvidence(switched,0x8000,D3D12QueueEvidenceKind::LegacyBackbufferBarrier,true,true,true);
    assert(switched.candidateQueue==0x8000);
    assert(!d3d12RecoveryTrusted(switched));
    return 0;
}
