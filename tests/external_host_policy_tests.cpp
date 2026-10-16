#include "udlss/external_host_policy.hpp"
#include "udlss/settings.hpp"
#include <cassert>
#include <iostream>

int main() {
    using namespace udlss;
    if(nrHostPendingWaitBudgetMs(FramePacingMode::Synchronized,1500)!=500||
       nrHostPendingWaitBudgetMs(FramePacingMode::Synchronized,2000)!=0||
       nrHostPendingWaitBudgetMs(FramePacingMode::Synchronized,5000)!=0||
       nrHostPendingWaitBudgetMs(FramePacingMode::Adaptive,0)!=0||
       nrHostRemainingWaitBudgetMs(FramePacingMode::Synchronized,1500)!=500){
        std::cerr<<"Pending drain must share the request watchdog and frame budget\n";return 1;
    }
    assert(defaultSettings().backend == BackendMode::InGameNR);
    const auto n = makeNrHostSessionNames(1234, 7);
    assert(n.map == L"Local\\UniversalDLSS5.NRHost.1234.7");
    assert(n.frameEvent == L"Local\\UniversalDLSS5.NRHost.1234.7.Frame");
    assert(n.stopEvent == L"Local\\UniversalDLSS5.NRHost.1234.7.Stop");
    assert(n.doneEvent == L"Local\\UniversalDLSS5.NRHost.1234.7.Done");
    assert(n.fence == L"Local\\UniversalDLSS5.NRHost.1234.7.Fence");
    assert(n.color == L"Local\\UniversalDLSS5.NRHost.1234.7.Color");
    assert(n.output == L"Local\\UniversalDLSS5.NRHost.1234.7.Output");
    assert(nrHostSequenceEnqueued(7, 7));
    assert(nrHostSequenceEnqueued(9, 7));
    assert(!nrHostSequenceEnqueued(6, 7));

    // An output belongs to a frame only when BOTH the host publication and
    // the shared GPU fence prove that exact request has completed. A later
    // sequence must never be accepted as the output for an earlier frame.
    assert(nrHostOutputReady(7, 7, 42, 42));
    assert(nrHostOutputReady(7, 7, 99, 42));
    assert(!nrHostOutputReady(6, 7, 99, 42));
    assert(!nrHostOutputReady(8, 7, 99, 42));
    assert(!nrHostOutputReady(7, 7, 41, 42));

    assert(nrHostOutputWaitBudgetMs(FramePacingMode::Synchronized) >= 33);
    assert(nrHostOutputWaitBudgetMs(FramePacingMode::Adaptive) > 0);
    assert(nrHostOutputWaitBudgetMs(FramePacingMode::Adaptive) < nrHostOutputWaitBudgetMs(FramePacingMode::Synchronized));
    assert(nrHostOutputWaitBudgetMs(FramePacingMode::Asynchronous) == 0);
    assert(nrHostRemainingWaitBudgetMs(FramePacingMode::Synchronized, 0) == nrHostOutputWaitBudgetMs(FramePacingMode::Synchronized));
    assert(nrHostRemainingWaitBudgetMs(FramePacingMode::Synchronized, 12) == nrHostOutputWaitBudgetMs(FramePacingMode::Synchronized)-12);
    assert(nrHostRemainingWaitBudgetMs(FramePacingMode::Synchronized, 5000) == 0);
    assert(nrHostRemainingWaitBudgetMs(FramePacingMode::Asynchronous, 0) == 0);
    return 0;
}
