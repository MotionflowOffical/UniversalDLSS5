#include "dxgi_hooks.hpp"
#include "attach_logger.hpp"
#include "d3d9_hooks.hpp"
#include "opengl_hooks.hpp"
#include "vulkan_hooks.hpp"
#include "udlss/renderer_ownership_policy.hpp"
#include "udlss/auto_detach_policy.hpp"
#include "udlss/renderer_activity.hpp"
#include <windows.h>

namespace {
HMODULE g_self{};
udlss::SharedControl g_control;
volatile LONG g_started = 0;

struct WindowProbe { DWORD pid{}; bool found{}; };

bool isUniversalDlssWindow(HWND hwnd) {
    wchar_t className[128]{};
    if (!GetClassNameW(hwnd, className, _countof(className))) return false;
    return wcsncmp(className, L"UDLSS5_", 7) == 0 ||
           wcsncmp(className, L"UniversalDLSS5.", 15) == 0;
}

bool processHasTopLevelWindow(DWORD pid) {
    WindowProbe probe{pid, false};
    EnumWindows([](HWND hwnd, LPARAM param) -> BOOL {
        auto* p = reinterpret_cast<WindowProbe*>(param);
        DWORD ownerPid = 0;
        GetWindowThreadProcessId(hwnd, &ownerPid);
        if (ownerPid != p->pid || isUniversalDlssWindow(hwnd)) return TRUE;
        if (!IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER) != nullptr) return TRUE;
        if (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) return TRUE;
        p->found = true;
        return FALSE;
    }, reinterpret_cast<LPARAM>(&probe));
    return probe.found;
}
}

DWORD WINAPI bridgeThread(void*) {
    udlss::resetProcessRendererOwnership();
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
    udlss::bridge::ensureD3D9HooksInstalled(g_self, &g_control);
    udlss::bridge::installOpenGLHooks(g_self, &g_control);
    udlss::bridge::installVulkanHooks(g_self, &g_control);
    udlss::bridge::logAttachStage(udlss::bridge::AttachLogStage::WaitingForPresent);

    ULONGLONG lastCompatHookProbe=0;
    udlss::AutoDetachState autoDetach{};
    while (g_control.valid() &&
           InterlockedCompareExchange((volatile LONG*)&g_control.raw()->requestUnload, 0, 0) == 0) {
        // Legacy/custom APIs can load after injection. Probe at low frequency;
        // installed hooks are idempotent and native D3D11/D3D12 frames never
        // execute compatibility work.
        const ULONGLONG now=GetTickCount64();
        if(!lastCompatHookProbe || now-lastCompatHookProbe>=1000){
            lastCompatHookProbe=now;
            udlss::bridge::ensureD3D9HooksInstalled(g_self, &g_control);
            udlss::bridge::installOpenGLHooks(g_self, &g_control);
            udlss::bridge::installVulkanHooks(g_self, &g_control);
        }
        const bool hasGameWindow=processHasTopLevelWindow(GetCurrentProcessId());
        if(hasGameWindow){autoDetach.everSawGameWindow=true;autoDetach.lastGameWindowTickMs=now;}
        if(udlss::shouldAutoDetach(autoDetach, now, hasGameWindow, udlss::bridge::lastRendererActivityTick())){
            udlss::bridge::logAttachStage(udlss::bridge::AttachLogStage::Unloading, L"automatic detach after application exit");
            break;
        }
        Sleep(250);
    }

    udlss::bridge::logAttachStage(udlss::bridge::AttachLogStage::Unloading);
    const auto detachMode = udlss::bridge::removeHooks();
    g_control.close();
    if (detachMode == udlss::DetachMode::Unloaded) {
        udlss::bridge::shutdownVulkanCompatibility();
        udlss::bridge::shutdownOpenGLCompatibility();
        udlss::bridge::shutdownD3D9Compatibility();
        FreeLibraryAndExitThread(g_self, 0);
    }
    // DetachedResident deliberately leaves the module loaded. The worker exits
    // normally while every remaining detour is forwarding-only until process exit.
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
