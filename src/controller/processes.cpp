#include "processes.hpp"
#include <tlhelp32.h>
#include <algorithm>
#include <cwctype>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "udlss/injection_policy.hpp"
#include "udlss/app_picker_policy.hpp"

namespace udlss::controller {
namespace {

std::unordered_set<DWORD> visibleTopLevelPids() {
    std::unordered_set<DWORD> out;
    EnumWindows([](HWND hwnd,LPARAM param)->BOOL {
        if(!IsWindowVisible(hwnd) || GetWindow(hwnd,GW_OWNER)!=nullptr) return TRUE;
        const LONG_PTR ex=GetWindowLongPtrW(hwnd,GWL_EXSTYLE);
        if(ex & WS_EX_TOOLWINDOW) return TRUE;
        if(GetWindowTextLengthW(hwnd)<=0) return TRUE;
        DWORD pid=0; GetWindowThreadProcessId(hwnd,&pid);
        if(pid) reinterpret_cast<std::unordered_set<DWORD>*>(param)->insert(pid);
        return TRUE;
    },reinterpret_cast<LPARAM>(&out));
    return out;
}

std::wstring fallbackDisplayName(const ProcessInfo& p) {
    std::wstring n=p.name;
    if(n.size()>4 && _wcsicmp(n.c_str()+n.size()-4,L".exe")==0) n.resize(n.size()-4);
    if(!n.empty()) n[0]=static_cast<wchar_t>(std::towupper(n[0]));
    return n.empty()?L"Application":n;
}

std::wstring fileVersionString(const std::wstring& path,const wchar_t* field) {
    if(path.empty()) return {};
    DWORD dummy=0;
    const DWORD bytes=GetFileVersionInfoSizeW(path.c_str(),&dummy);
    if(!bytes) return {};
    std::vector<unsigned char> data(bytes);
    if(!GetFileVersionInfoW(path.c_str(),0,bytes,data.data())) return {};
    struct Translation { WORD language; WORD codePage; };
    Translation* trans=nullptr; UINT transBytes=0;
    if(!VerQueryValueW(data.data(),L"\\VarFileInfo\\Translation",reinterpret_cast<void**>(&trans),&transBytes) || transBytes<sizeof(Translation)) return {};
    wchar_t query[128]{};
    swprintf_s(query,L"\\StringFileInfo\\%04x%04x\\%s",trans[0].language,trans[0].codePage,field);
    wchar_t* value=nullptr; UINT chars=0;
    if(VerQueryValueW(data.data(),query,reinterpret_cast<void**>(&value),&chars) && value && chars>1) return std::wstring(value);
    return {};
}

std::wstring friendlyName(const ProcessInfo& p) {
    auto value=fileVersionString(p.path,L"FileDescription");
    if(value.empty()) value=fileVersionString(p.path,L"ProductName");
    return value.empty()?fallbackDisplayName(p):value;
}

}

Arch processArch(HANDLE p){
    using Fn=BOOL(WINAPI*)(HANDLE,USHORT*,USHORT*); static auto fn=(Fn)GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"IsWow64Process2");
    if(fn){USHORT proc=0,native=0;if(fn(p,&proc,&native)){USHORT m=proc?proc:native; if(m==IMAGE_FILE_MACHINE_I386)return Arch::X86; if(m==IMAGE_FILE_MACHINE_AMD64)return Arch::X64; if(m==IMAGE_FILE_MACHINE_ARM64)return Arch::Arm64;}}
    BOOL wow=FALSE; if(IsWow64Process(p,&wow)) return wow?Arch::X86:(sizeof(void*)==8?Arch::X64:Arch::X86); return Arch::Unknown;
}
bool moduleLoaded(DWORD pid,const wchar_t* wanted){
    HANDLE s=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,pid); if(s==INVALID_HANDLE_VALUE)return false; MODULEENTRY32W m{sizeof(m)}; bool found=false;
    if(Module32FirstW(s,&m)){do{if(_wcsicmp(m.szModule,wanted)==0){found=true;break;}}while(Module32NextW(s,&m));} CloseHandle(s); return found;
}
std::vector<ProcessInfo> enumerateProcesses(){
    const auto visible=visibleTopLevelPids();
    std::vector<ProcessInfo> out; HANDLE s=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0); if(s==INVALID_HANDLE_VALUE)return out; PROCESSENTRY32W e{sizeof(e)};
    if(Process32FirstW(s,&e)){do{
        if(e.th32ProcessID<=4 || e.th32ProcessID==GetCurrentProcessId()) continue;
        ProcessInfo i{};i.pid=e.th32ProcessID;i.parentPid=e.th32ParentProcessID;i.name=e.szExeFile;i.visibleTopLevel=visible.contains(i.pid);
        HANDLE p=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|PROCESS_VM_READ,FALSE,i.pid); if(p){
            i.accessible=true;i.arch=processArch(p);
            wchar_t path[32768];DWORD n=_countof(path);if(QueryFullProcessImageNameW(p,0,path,&n))i.path.assign(path,n);
            PROCESS_MITIGATION_BINARY_SIGNATURE_POLICY sig{};
            if(GetProcessMitigationPolicy(p,ProcessSignaturePolicy,&sig,sizeof(sig)))
                i.blocksThirdPartyModules=blocksThirdPartyModules({sig.MicrosoftSignedOnly!=0,sig.StoreSignedOnly!=0});
            CloseHandle(p);
        }
        i.hasDxgi=moduleLoaded(i.pid,L"dxgi.dll"); out.push_back(std::move(i));
    }while(Process32NextW(s,&e));} CloseHandle(s);
    std::sort(out.begin(),out.end(),[](auto&a,auto&b){int c=_wcsicmp(a.name.c_str(),b.name.c_str());return c==0?a.pid<b.pid:c<0;}); return out;
}

std::vector<ApplicationInfo> enumerateApplications() {
    const auto all=enumerateProcesses();
    std::vector<AppPickerProcess> picker;
    picker.reserve(all.size());
    for(const auto& p:all) picker.push_back({p.pid,p.parentPid,p.name,p.path,p.accessible,p.visibleTopLevel,p.hasDxgi,p.blocksThirdPartyModules});
    const auto groups=groupVisibleApplications(picker);
    std::unordered_map<DWORD,const ProcessInfo*> byPid;
    for(const auto& p:all) byPid.emplace(p.pid,&p);
    std::vector<ApplicationInfo> out;
    out.reserve(groups.size());
    for(const auto& group:groups) {
        const auto it=byPid.find(group.rootPid);
        if(it==byPid.end()) continue;
        ApplicationInfo app{};
        app.root=*it->second;
        app.root.hasDxgi=group.anyDxgi;
        app.root.blocksThirdPartyModules=group.blocksThirdPartyModules;
        app.displayName=friendlyName(app.root);
        app.processCount=group.processCount;
        app.visibleWindowCount=group.visibleWindowCount;
        app.anyDxgi=group.anyDxgi;
        app.blocksThirdPartyModules=group.blocksThirdPartyModules;
        out.push_back(std::move(app));
    }
    std::sort(out.begin(),out.end(),[](const auto& a,const auto& b){
        const int c=_wcsicmp(a.displayName.c_str(),b.displayName.c_str());
        return c==0?a.root.pid<b.root.pid:c<0;
    });
    return out;
}

std::vector<ProcessInfo> processTree(DWORD root){auto all=enumerateProcesses();std::unordered_set<DWORD> ids{root}; bool changed=true;while(changed){changed=false;for(auto& p:all)if(ids.contains(p.parentPid)&&!ids.contains(p.pid)){ids.insert(p.pid);changed=true;}}std::vector<ProcessInfo> r;for(auto&p:all)if(ids.contains(p.pid))r.push_back(p);return r;}
}
