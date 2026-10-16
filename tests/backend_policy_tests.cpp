#include "udlss/backend_policy.hpp"
#include <cassert>
#include <string_view>
#include <iostream>

int main() {
    using namespace udlss;
    if(!isNeuralEvaluation(BackendMode::ExternalHostNR,false,true)){
        std::cerr<<"A successful external-host evaluation must count as neural output\n";return 1;
    }
    if(isNeuralEvaluation(BackendMode::Passthrough,false,true)||
       isNeuralEvaluation(BackendMode::ExternalHostNR,true,true)||
       isNeuralEvaluation(BackendMode::InGameNR,false,false))return 1;
    assert(backendDisplayName(BackendMode::InGameNR,false)==std::wstring_view(L"Direct in-game DLSS 5 NR"));
    assert(backendDisplayName(BackendMode::InGameNR,true)==std::wstring_view(L"Passthrough (direct + external NR unavailable)"));
    assert(backendDisplayName(BackendMode::Passthrough,false)==std::wstring_view(L"Passthrough"));
    assert(backendDisplayName(BackendMode::ExternalHostNR,false)==std::wstring_view(L"External DLSS 5 NR Host"));
    assert(backendDisplayName(BackendMode::ExternalHostNR,true)==std::wstring_view(L"Passthrough (NR host unavailable)"));
    assert(preferredBackend(true)==BackendMode::InGameNR);
    assert(preferredBackend(false)==BackendMode::ExternalHostNR);
    return 0;
}
