#include "udlss/app_picker_policy.hpp"
#include "udlss/d3d12_route_policy.hpp"
#include <cassert>
#include <cstdint>

int main(){
    using namespace udlss;

    // Legacy/custom renderer children must be attach targets even when they do
    // not load DXGI. This is the common launcher -> x86 D3D9 game case.
    assert(shouldAttachRendererProcess(false,false,AppRendererD3D9));
    assert(shouldAttachRendererProcess(false,false,AppRendererOpenGL));
    assert(shouldAttachRendererProcess(false,false,AppRendererVulkan));
    assert(!shouldAttachRendererProcess(false,false,AppRendererNone));
    assert(shouldAttachRendererProcess(true,false,AppRendererNone));

    // Two observed PRESENT transitions are strong enough for the recovery
    // path, but they are not proof that the queue was the one supplied to the
    // swapchain at creation. Recovery must therefore synchronize completion
    // before DXGI Present instead of taking the native zero-extra-wait route.
    PresentQueueProofState inferred{};
    observePresentQueueEvidence(inferred,0x1000,1000,false);
    observePresentQueueEvidence(inferred,0x1000,1016,false);
    assert(presentQueueTrusted(inferred,1016)); // legacy policy still sees strong evidence

    D3D12RecoveryEvidence recovery{};
    observeD3D12RecoveryEvidence(recovery,0x1000,D3D12QueueEvidenceKind::LegacyBackbufferBarrier,true,true,true,true);
    observeD3D12RecoveryEvidence(recovery,0x1000,D3D12QueueEvidenceKind::LegacyBackbufferBarrier,true,true,true,true);
    auto recovered=chooseD3D12Route(inferred,recovery,1016);
    assert(recovered.trusted);
    assert(!recovered.exactCreationQueue);
    assert(recovered.route==RendererRoute::ModernD3D12Recovery);
    assert(recovered.synchronizeBeforePresent);
    assert(recovered.queueId==0x1000);

    PresentQueueProofState exact{};
    observePresentQueueEvidence(exact,0x2000,2000,true);
    D3D12RecoveryEvidence exactRecovery{};
    observeD3D12RecoveryEvidence(exactRecovery,0x2000,D3D12QueueEvidenceKind::SwapchainCreation,true,true,true,true);
    auto native=chooseD3D12Route(exact,exactRecovery,500000);
    assert(native.trusted);
    assert(native.exactCreationQueue);
    assert(native.route==RendererRoute::NativeD3D12);
    assert(!native.synchronizeBeforePresent);
    assert(native.queueId==0x2000);

    return 0;
}
