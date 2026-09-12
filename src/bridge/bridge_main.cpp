#include "dxgi_hooks.hpp"
#include <windows.h>
namespace { HMODULE g_self{}; udlss::SharedControl g_control; }
DWORD WINAPI bridgeThread(void*){
 if(!g_control.open()) return 0;
 if(!udlss::bridge::installHooks(g_self,&g_control)) return 0;
 while(g_control.valid() && InterlockedCompareExchange((volatile LONG*)&g_control.raw()->requestUnload,0,0)==0) Sleep(250);
 udlss::bridge::removeHooks(); g_control.close(); FreeLibraryAndExitThread(g_self,0); return 0;
}
BOOL WINAPI DllMain(HINSTANCE h,DWORD reason,LPVOID){if(reason==DLL_PROCESS_ATTACH){g_self=h;DisableThreadLibraryCalls(h);HANDLE t=CreateThread(nullptr,0,bridgeThread,nullptr,0,nullptr);if(t)CloseHandle(t);}return TRUE;}
