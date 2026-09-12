#include "injector.hpp"
#include <iostream>
int wmain(int argc,wchar_t** argv){if(argc!=3){std::wcerr<<L"Usage: UniversalDLSS5.Injector.exe <pid> <dll-path>\n";return 64;}DWORD pid=wcstoul(argv[1],nullptr,10);std::wstring msg;int r=udlss::injector::injectDll(pid,argv[2],msg);std::wcout<<msg<<L"\n";return r;}
