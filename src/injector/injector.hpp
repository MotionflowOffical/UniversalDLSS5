#pragma once
#include <windows.h>
#include <string>
namespace udlss::injector { int injectDll(DWORD pid,const std::wstring& dll,std::wstring& message); }
