#include "udlss/ipc_security.hpp"
#include <cassert>
#include <string>
int main(){
    using namespace udlss;
    const std::wstring sddl=lowIntegrityMappingSddl(L"S-1-5-21-1-2-3-1001");
    assert(sddl.find(L"S-1-5-21-1-2-3-1001")!=std::wstring::npos);
    assert(sddl.find(L";;;WD") == std::wstring::npos);
    assert(sddl.find(L"S:(ML;;NW;;;LW)")!=std::wstring::npos);
    return 0;
}
