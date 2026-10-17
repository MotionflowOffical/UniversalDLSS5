#include "../src/controller/inject_client.hpp"
#include <filesystem>
#include <iostream>
#include <array>
#include <algorithm>

namespace fs=std::filesystem;
struct Fixture {
    fs::path dir;
    HANDLE stop{};
    std::vector<HANDLE> children;
    std::vector<fs::path> copies;
    ~Fixture(){
        if(stop)SetEvent(stop);
        for(auto child:children){WaitForSingleObject(child,5000);CloseHandle(child);}
        if(stop)CloseHandle(stop);
        for(const auto& path:copies){std::error_code ec;fs::remove(path,ec);}
        std::error_code ec;fs::remove(dir,ec);
    }
};
int wmain(int argc,wchar_t** argv){
    if(argc==3&&std::wstring_view(argv[1])==L"--fixture"){
        auto event=OpenEventW(SYNCHRONIZE,FALSE,argv[2]);
        if(!event)return 2;
        const auto result=WaitForSingleObject(event,30000);CloseHandle(event);
        return result==WAIT_OBJECT_0?0:3;
    }
    const auto token=std::to_wstring(GetCurrentProcessId());
    const auto eventName=L"Local\\UDLSS.InjectionIsolation."+token;
    Fixture fixture{fs::temp_directory_path()/(L"UDLSS injection isolation "+token)};
    fs::create_directories(fixture.dir);
    fixture.stop=CreateEventW(nullptr,TRUE,FALSE,eventName.c_str());
    if(!fixture.stop)return 2;
    wchar_t self[32768]{};GetModuleFileNameW(nullptr,self,_countof(self));
    const std::array names={L"UNIVERSALdlss5.NRHOST.EXE",L"UniversalDLSS5.exe",
        L"UniversalDLSS5.Injector.exe",L"UniversalDLSS5.Injector32.exe",
        L"UniversalDLSS5.TestApp.exe"};
    std::vector<DWORD> ids;
    for(auto name:names){
        const auto path=fixture.dir/name;
        fs::copy_file(self,path);fixture.copies.push_back(path);
        auto command=L"\""+path.wstring()+L"\" --fixture \""+eventName+L"\"";
        STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION child{};
        if(!CreateProcessW(path.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,
                           nullptr,fixture.dir.c_str(),&startup,&child))return 2;
        CloseHandle(child.hThread);fixture.children.push_back(child.hProcess);ids.push_back(child.dwProcessId);
    }
    bool ok=true;
    const auto all=udlss::controller::enumerateProcesses();
    const auto tree=udlss::controller::processTree(GetCurrentProcessId());
    for(unsigned i=0;i<names.size();++i){
        const auto contains=[&](const auto& list){return std::any_of(list.begin(),list.end(),[&](const auto& p){return p.pid==ids[i];});};
        const bool ordinary=i==names.size()-1;
        if(contains(all)!=ordinary||contains(tree)!=ordinary){
            std::wcerr<<L"Target enumeration isolation failed for "<<names[i]<<L"\n";ok=false;
        }
        if(!ordinary){
            udlss::controller::ProcessInfo target{};
            target.pid=ids[i];target.name=names[i];target.path=fixture.copies[i].wstring();
            target.accessible=true;target.arch=udlss::controller::Arch::X64;
            std::wstring message;
            if(udlss::controller::injectBridge(target,fixture.dir.wstring(),message)||
               message.find(L"internal process")==std::wstring::npos){
                std::wcerr<<L"Direct injection guard failed for "<<names[i]<<L": "<<message<<L"\n";ok=false;
            }
            // A stale picker name must not bypass the actual executable-path check.
            target.name=L"game.exe";
            if(udlss::controller::injectBridge(target,fixture.dir.wstring(),message)||
               message.find(L"internal process")==std::wstring::npos){
                std::wcerr<<L"Executable-path isolation failed for "<<names[i]<<L"\n";ok=false;
            }
        }
    }
    if(ok)std::cout<<"PASS: internal helpers excluded from real process trees and direct injection; TestApp retained\n";
    return ok?0:1;
}
