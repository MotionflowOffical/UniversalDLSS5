#include "udlss/backend_policy.hpp"
#include <cassert>
#include <string_view>

int main() {
    using namespace udlss;
    assert(backendDisplayName(BackendMode::InGameNR,false)==std::wstring_view(L"Direct in-game DLSS 5 NR"));
    assert(backendDisplayName(BackendMode::InGameNR,true)==std::wstring_view(L"Passthrough (direct + external NR unavailable)"));
    assert(backendDisplayName(BackendMode::Passthrough,false)==std::wstring_view(L"Passthrough"));
    assert(backendDisplayName(BackendMode::ExternalHostNR,false)==std::wstring_view(L"External DLSS 5 NR Host"));
    assert(backendDisplayName(BackendMode::ExternalHostNR,true)==std::wstring_view(L"Passthrough (NR host unavailable)"));
    assert(preferredBackend(true)==BackendMode::InGameNR);
    assert(preferredBackend(false)==BackendMode::ExternalHostNR);
    return 0;
}
