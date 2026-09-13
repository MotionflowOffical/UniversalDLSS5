#include "dxgi_hooks.hpp"
#include "attach_logger.hpp"
#include <windows.h>

namespace {
HMODULE g_self{};
udlss::SharedControl g_control;
volatile LONG g_started = 0;
}

DWORD WINAPI bridgeThread(void*) {
    udlss::bridge::initializeAttachLog();
    udlss::bridge::logAttachStage(udlss::bridge::AttachLogStage::BridgeThreadStarted);

    // BridgeStart is invoked explicitly by the injector after LoadLibraryW has
    // completed, so this work no longer begins from inside DllMain/loader lock.
    Sleep(100);
    if (!g_control.open()) {
        udlss::bridge::logAttachStage(udlss::bridge::AttachLogStage::Failure, L"shared control mapping unavailable");
        InterlockedExchange(&g_started, 0);
        FreeLibraryAndExitThread(g_self, 1);
    }
    udlss::bridge::logAttachStage(udlss::bridge::AttachLogStage::SharedControlOpened);
    Sleep(150);
    udlss::bridge::logAttachStage(udlss::bridge::AttachLogStage::SafeAttachDelayComplete);

    if (!udlss::bridge::installHooks(g_self, &g_control)) {
        udlss::bridge::logAttachStage(udlss::bridge::AttachLogStage::Failure, L"core hook installation failed");
        g_control.close();
        InterlockedExchange(&g_started, 0);
        FreeLibraryAndExitThread(g_self, 2);
    }
    udlss::bridge::logAttachStage(udlss::bridge::AttachLogStage::WaitingForPresent);

    while (g_control.valid() &&
           InterlockedCompareExchange((volatile LONG*)&g_control.raw()->requestUnload, 0, 0) == 0) {
        Sleep(250);
    }

    udlss::bridge::logAttachStage(udlss::bridge::AttachLogStage::Unloading);
    udlss::bridge::removeHooks();
    g_control.close();
    FreeLibraryAndExitThread(g_self, 0);
    return 0;
}

extern "C" __declspec(dllexport) DWORD WINAPI UniversalDLSS5_BridgeStart(void*) {
    // Idempotent by design. This also repairs the case where a bridge module is
    // already present after an interrupted injector run but its worker was never
    // started.
    if (InterlockedCompareExchange(&g_started, 1, 0) != 0) return ERROR_SUCCESS;

    HANDLE thread = CreateThread(nullptr, 0, bridgeThread, nullptr, 0, nullptr);
    if (!thread) {
        const DWORD error = GetLastError();
        InterlockedExchange(&g_started, 0);
        return error ? error : ERROR_GEN_FAILURE;
    }
    CloseHandle(thread);
    return ERROR_SUCCESS;
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = module;
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}
