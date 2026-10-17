#pragma once
#include <string_view>
namespace udlss {
// Product infrastructure can be a descendant of an attached game. Loading the
// capture bridge there would hook the isolated neural host's own GPU work.
// Match exact executable basenames; TestApp and ordinary renderer children stay
// eligible. This is an isolation rule, not executable identity verification.
inline bool isBridgeInfrastructureProcess(std::wstring_view nameOrPath) {
    const auto slash=nameOrPath.find_last_of(L"\\/");
    if(slash!=std::wstring_view::npos)nameOrPath.remove_prefix(slash+1);
    constexpr std::wstring_view names[]={L"universaldlss5.exe",L"universaldlss5.nrhost.exe",
        L"universaldlss5.injector.exe",L"universaldlss5.injector32.exe"};
    for(auto name:names){
        if(name.size()!=nameOrPath.size())continue;
        bool same=true;
        for(std::size_t i=0;i<name.size();++i){
            auto c=nameOrPath[i];if(c>=L'A'&&c<=L'Z')c+=L'a'-L'A';
            if(c!=name[i]){same=false;break;}
        }
        if(same)return true;
    }
    return false;
}
struct ProcessModulePolicy {
    bool microsoftSignedOnly{};
    bool storeSignedOnly{};
};
inline constexpr bool blocksThirdPartyModules(ProcessModulePolicy p) {
    return p.microsoftSignedOnly || p.storeSignedOnly;
}
} // namespace udlss
