#pragma once
#include <algorithm>
#include <cstdint>
#include <cwctype>
#include <string>
#include <unordered_map>
#include <vector>

namespace udlss {

struct AppPickerProcess {
    std::uint32_t pid{};
    std::uint32_t parentPid{};
    std::wstring name;
    std::wstring path;
    bool accessible{};
    bool visibleTopLevel{};
    bool hasDxgi{};
    bool blocksThirdPartyModules{};
};

struct AppPickerGroup {
    std::uint32_t rootPid{};
    std::wstring name;
    std::wstring path;
    std::size_t processCount{};
    std::size_t visibleWindowCount{};
    bool anyDxgi{};
    bool blocksThirdPartyModules{};
};

inline std::wstring appIdentityKey(const std::wstring& path,const std::wstring& name) {
    std::wstring key = path.empty() ? name : path;
    std::transform(key.begin(),key.end(),key.begin(),[](wchar_t c){ return static_cast<wchar_t>(std::towlower(c)); });
    return key;
}

inline std::vector<AppPickerGroup> groupVisibleApplications(const std::vector<AppPickerProcess>& processes) {
    std::unordered_map<std::uint32_t,std::size_t> byPid;
    byPid.reserve(processes.size());
    for(std::size_t i=0;i<processes.size();++i) byPid.emplace(processes[i].pid,i);

    struct Accum {
        AppPickerGroup group;
        std::wstring key;
        std::vector<std::uint32_t> visiblePids;
    };
    std::vector<Accum> groups;
    std::unordered_map<std::wstring,std::size_t> byKey;

    // Only applications with at least one visible top-level window enter the normal picker.
    for(const auto& p:processes) {
        if(!p.accessible || !p.visibleTopLevel) continue;
        const auto key=appIdentityKey(p.path,p.name);
        if(key.empty()) continue;
        auto [it,inserted]=byKey.emplace(key,groups.size());
        if(inserted) {
            Accum a{};
            a.key=key;
            a.group.rootPid=p.pid;
            a.group.name=p.name;
            a.group.path=p.path;
            groups.push_back(std::move(a));
        }
        auto& a=groups[it->second];
        ++a.group.visibleWindowCount;
        a.visiblePids.push_back(p.pid);
        a.group.blocksThirdPartyModules = a.group.blocksThirdPartyModules || p.blocksThirdPartyModules;
    }

    // Fold duplicate helper/renderer/GPU processes that use the same executable into one app row.
    for(auto& a:groups) {
        for(const auto& p:processes) {
            if(appIdentityKey(p.path,p.name)!=a.key) continue;
            ++a.group.processCount;
            a.group.anyDxgi = a.group.anyDxgi || p.hasDxgi;
            a.group.blocksThirdPartyModules = a.group.blocksThirdPartyModules || p.blocksThirdPartyModules;
        }

        // Prefer the visible process whose parent is not another process with the same identity.
        for(const auto pid:a.visiblePids) {
            const auto pit=byPid.find(pid);
            if(pit==byPid.end()) continue;
            const auto& p=processes[pit->second];
            bool parentSame=false;
            if(const auto parent=byPid.find(p.parentPid); parent!=byPid.end()) {
                const auto& pp=processes[parent->second];
                parentSame=appIdentityKey(pp.path,pp.name)==a.key;
            }
            if(!parentSame) { a.group.rootPid=pid; a.group.name=p.name; a.group.path=p.path; break; }
        }
    }

    std::vector<AppPickerGroup> out;
    out.reserve(groups.size());
    for(auto& a:groups) out.push_back(std::move(a.group));
    std::sort(out.begin(),out.end(),[](const auto& a,const auto& b){
        std::wstring an=a.name,bn=b.name;
        std::transform(an.begin(),an.end(),an.begin(),[](wchar_t c){return static_cast<wchar_t>(std::towlower(c));});
        std::transform(bn.begin(),bn.end(),bn.begin(),[](wchar_t c){return static_cast<wchar_t>(std::towlower(c));});
        if(an!=bn) return an<bn;
        return a.rootPid<b.rootPid;
    });
    return out;
}

inline const AppPickerGroup* findApplicationGroup(const std::vector<AppPickerGroup>& groups,const std::wstring& path) {
    const auto key=appIdentityKey(path,L"");
    for(const auto& g:groups) if(appIdentityKey(g.path,g.name)==key) return &g;
    return nullptr;
}

} // namespace udlss
