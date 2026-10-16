#include "ui.hpp"
#include <shellapi.h>
int WINAPI wWinMain(HINSTANCE h,HINSTANCE,LPWSTR,int show){
    int argc{};auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);std::wstring executable;
    if(!argv)return 64;
    const bool valid=argc==1||(argc==3&&std::wstring(argv[1])==L"--launch");
    if(valid&&argc==3)executable=argv[2];LocalFree(argv);
    if(!valid)return 64;
    // Own the controller before it can create/write the shared control block.
    // A second controller must not change another renderer's profile or detach it.
    const auto owner=CreateMutexW(nullptr,FALSE,L"Local\\UniversalDLSS5.Controller.Owner");
    if(!owner)return 2;
    if(GetLastError()==ERROR_ALREADY_EXISTS){CloseHandle(owner);return ERROR_ALREADY_EXISTS;}
    const auto result=udlss::controller::runUi(h,show,executable);
    CloseHandle(owner);return result;
}
