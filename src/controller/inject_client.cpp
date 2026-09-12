#include "inject_client.hpp"
#include <windows.h>
#include <filesystem>
namespace udlss::controller {
static std::wstring q(const std::wstring&s){return L"\""+s+L"\"";}
bool injectBridge(const ProcessInfo& p,const std::wstring& dir,std::wstring& message){
    if(!p.accessible){message=L"process not accessible";return false;}
    if(p.blocksThirdPartyModules){message=L"process policy blocks third-party DLL loading";return false;}
    if(p.arch==Arch::Arm64||p.arch==Arch::Unknown){message=L"unsupported architecture";return false;}
    std::filesystem::path base(dir); std::wstring injector,bridge;
    if(p.arch==Arch::X86){injector=(base/L"UniversalDLSS5.Injector32.exe").wstring();bridge=(base/L"UniversalDLSS5.Bridge32.dll").wstring();}
    else{injector=(base/L"UniversalDLSS5.Injector.exe").wstring();bridge=(base/L"UniversalDLSS5.Bridge.dll").wstring();}
    if(!std::filesystem::exists(injector)||!std::filesystem::exists(bridge)){message=L"matching injector/bridge binary missing";return false;}
    std::wstring cmd=q(injector)+L" "+std::to_wstring(p.pid)+L" "+q(bridge); std::vector<wchar_t> buf(cmd.begin(),cmd.end());buf.push_back(0);
    STARTUPINFOW si{sizeof(si)};PROCESS_INFORMATION pi{}; if(!CreateProcessW(nullptr,buf.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,dir.c_str(),&si,&pi)){message=L"failed to launch injector";return false;}
    WaitForSingleObject(pi.hProcess,12000);DWORD ec=1;GetExitCodeProcess(pi.hProcess,&ec);CloseHandle(pi.hThread);CloseHandle(pi.hProcess); if(ec!=0){message=L"injector error "+std::to_wstring(ec);return false;} message=L"attached";return true;
}
}
