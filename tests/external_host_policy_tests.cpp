#include "udlss/external_host_policy.hpp"
#include "udlss/settings.hpp"
#include <cassert>

int main() {
    using namespace udlss;
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
    return 0;
}
