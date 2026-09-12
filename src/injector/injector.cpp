#include "injector.hpp"
#include <tlhelp32.h>
#include <filesystem>

namespace udlss::injector {
static bool alreadyLoaded(DWORD pid,const std::wstring& path){std::wstring file=std::filesystem::path(path).filename().wstring();HANDLE s=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,pid);if(s==INVALID_HANDLE_VALUE)return false;MODULEENTRY32W m{sizeof(m)};bool yes=false;if(Module32FirstW(s,&m)){do{if(_wcsicmp(m.szModule,file.c_str())==0){yes=true;break;}}while(Module32NextW(s,&m));}CloseHandle(s);return yes;}
int injectDll(DWORD pid,const std::wstring& dll,std::wstring& msg){
    if(pid<=4){msg=L"system process rejected";return 2;} if(!std::filesystem::exists(dll)){msg=L"bridge DLL not found";return 3;} if(alreadyLoaded(pid,dll)){msg=L"already loaded";return 0;}
    HANDLE p=OpenProcess(PROCESS_CREATE_THREAD|PROCESS_QUERY_INFORMATION|PROCESS_VM_OPERATION|PROCESS_VM_WRITE|PROCESS_VM_READ,FALSE,pid); if(!p){msg=L"OpenProcess failed: "+std::to_wstring(GetLastError());return 4;}
    SIZE_T bytes=(dll.size()+1)*sizeof(wchar_t); void* remote=VirtualAllocEx(p,nullptr,bytes,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE); if(!remote){msg=L"VirtualAllocEx failed";CloseHandle(p);return 5;}
    if(!WriteProcessMemory(p,remote,dll.c_str(),bytes,nullptr)){msg=L"WriteProcessMemory failed";VirtualFreeEx(p,remote,0,MEM_RELEASE);CloseHandle(p);return 6;}
    auto load=(LPTHREAD_START_ROUTINE)GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"LoadLibraryW"); HANDLE t=CreateRemoteThread(p,nullptr,0,load,remote,0,nullptr); if(!t){msg=L"CreateRemoteThread failed: "+std::to_wstring(GetLastError());VirtualFreeEx(p,remote,0,MEM_RELEASE);CloseHandle(p);return 7;}
    WaitForSingleObject(t,10000);DWORD code=0;GetExitCodeThread(t,&code);CloseHandle(t);VirtualFreeEx(p,remote,0,MEM_RELEASE);CloseHandle(p); if(!code){msg=L"remote LoadLibraryW returned null";return 8;} msg=L"injected";return 0;
}
}
