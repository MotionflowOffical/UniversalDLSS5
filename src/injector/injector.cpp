#include "injector.hpp"

#include <windows.h>
#include <tlhelp32.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace udlss::injector {
namespace {

std::filesystem::path injectionLogPath(DWORD pid) {
    wchar_t localAppData[32768]{};
    std::filesystem::path base;
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, static_cast<DWORD>(std::size(localAppData)));
    if (n > 0 && n < std::size(localAppData)) {
        base = localAppData;
    } else {
        wchar_t temp[MAX_PATH]{};
        const DWORD tn = GetTempPathW(MAX_PATH, temp);
        base = (tn > 0 && tn < MAX_PATH) ? std::filesystem::path(temp) : std::filesystem::current_path();
    }
    base /= L"UniversalDLSS5";
    base /= L"logs";
    std::error_code ec;
    std::filesystem::create_directories(base, ec);
    return base / (L"inject-" + std::to_wstring(pid) + L".log");
}

void logInjection(DWORD pid, const std::wstring& text) {
    std::wofstream out(injectionLogPath(pid), std::ios::app);
    if (!out) return;
    out << L"[" << GetTickCount64() << L"] " << text << L"\n";
}

std::wstring moduleFileName(HMODULE module) {
    wchar_t path[32768]{};
    const DWORD n = GetModuleFileNameW(module, path, static_cast<DWORD>(std::size(path)));
    if (!n || n >= std::size(path)) return {};
    return std::filesystem::path(path).filename().wstring();
}

std::uintptr_t remoteModuleBase(DWORD pid, const std::wstring& moduleName) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;

    MODULEENTRY32W entry{sizeof(entry)};
    std::uintptr_t base = 0;
    if (Module32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szModule, moduleName.c_str()) == 0) {
                base = reinterpret_cast<std::uintptr_t>(entry.modBaseAddr);
                break;
            }
        } while (Module32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return base;
}

std::uintptr_t remoteModuleBaseFromPath(DWORD pid, const std::wstring& path) {
    return remoteModuleBase(pid, std::filesystem::path(path).filename().wstring());
}

LPTHREAD_START_ROUTINE resolveRemoteProcAddress(DWORD pid, FARPROC localProc) {
    if (!localProc) return nullptr;

    HMODULE owner = nullptr;
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(localProc), &owner) || !owner) {
        return nullptr;
    }

    const auto ownerName = moduleFileName(owner);
    if (ownerName.empty()) return nullptr;
    const auto remoteOwner = remoteModuleBase(pid, ownerName);
    if (!remoteOwner) return nullptr;

    const auto localAddress = reinterpret_cast<std::uintptr_t>(localProc);
    const auto localBase = reinterpret_cast<std::uintptr_t>(owner);
    if (localAddress < localBase) return nullptr;
    const auto rva = localAddress - localBase;
    return reinterpret_cast<LPTHREAD_START_ROUTINE>(remoteOwner + rva);
}

enum class RemoteWaitResult { Completed, TargetExited, Failed };

RemoteWaitResult waitForRemoteThread(DWORD pid, HANDLE process, HANDLE thread, const wchar_t* operation) {
    HANDLE handles[2]{thread, process};
    ULONGLONG lastProgress = GetTickCount64();
    for (;;) {
        const DWORD wait = WaitForMultipleObjects(2, handles, FALSE, 1000);
        if (wait == WAIT_OBJECT_0) return RemoteWaitResult::Completed;
        if (wait == WAIT_OBJECT_0 + 1) {
            logInjection(pid, std::wstring(operation) + L": target process exited while operation was pending");
            return RemoteWaitResult::TargetExited;
        }
        if (wait == WAIT_TIMEOUT) {
            const auto now = GetTickCount64();
            if (now - lastProgress >= 5000) {
                logInjection(pid, std::wstring(operation) + L": still pending; keeping remote state alive");
                lastProgress = now;
            }
            continue;
        }
        logInjection(pid, std::wstring(operation) + L": WaitForMultipleObjects failed, error=" + std::to_wstring(GetLastError()));
        return RemoteWaitResult::Failed;
    }
}

std::uintptr_t waitForRemoteModule(DWORD pid, const std::wstring& dll) {
    for (int i = 0; i < 20; ++i) {
        if (auto base = remoteModuleBaseFromPath(pid, dll)) return base;
        Sleep(25);
    }
    return 0;
}

bool invokeBridgeStart(DWORD pid, HANDLE process, std::uintptr_t remoteBridgeBase,
                       const std::wstring& dll, std::wstring& message) {
    HMODULE localBridge = LoadLibraryExW(dll.c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES);
    if (!localBridge) {
        message = L"could not map bridge locally to resolve BridgeStart: " + std::to_wstring(GetLastError());
        logInjection(pid, message);
        return false;
    }

    FARPROC localStart = GetProcAddress(localBridge, "UniversalDLSS5_BridgeStart");
#if defined(_M_IX86)
    if (!localStart) localStart = GetProcAddress(localBridge, "_UniversalDLSS5_BridgeStart@4");
#endif
    if (!localStart) {
        message = L"UniversalDLSS5_BridgeStart export not found";
        logInjection(pid, message);
        FreeLibrary(localBridge);
        return false;
    }

    const auto rva = reinterpret_cast<std::uintptr_t>(localStart) - reinterpret_cast<std::uintptr_t>(localBridge);
    auto remoteStart = reinterpret_cast<LPTHREAD_START_ROUTINE>(remoteBridgeBase + rva);
    FreeLibrary(localBridge);

    logInjection(pid, L"bridge module confirmed; starting bridge outside loader lock");
    HANDLE startThread = CreateRemoteThread(process, nullptr, 0, remoteStart, nullptr, 0, nullptr);
    if (!startThread) {
        message = L"CreateRemoteThread(BridgeStart) failed: " + std::to_wstring(GetLastError());
        logInjection(pid, message);
        return false;
    }

    const auto wait = waitForRemoteThread(pid, process, startThread, L"BridgeStart");
    if (wait != RemoteWaitResult::Completed) {
        CloseHandle(startThread);
        message = wait == RemoteWaitResult::TargetExited
            ? L"target exited while BridgeStart was pending"
            : L"BridgeStart wait failed";
        return false;
    }

    DWORD startCode = ERROR_GEN_FAILURE;
    if (!GetExitCodeThread(startThread, &startCode)) {
        const auto err = GetLastError();
        CloseHandle(startThread);
        message = L"GetExitCodeThread(BridgeStart) failed: " + std::to_wstring(err);
        logInjection(pid, message);
        return false;
    }
    CloseHandle(startThread);

    if (startCode != ERROR_SUCCESS) {
        message = L"BridgeStart returned error " + std::to_wstring(startCode);
        logInjection(pid, message);
        return false;
    }

    logInjection(pid, L"BridgeStart completed successfully");
    return true;
}

} // namespace

int injectDll(DWORD pid, const std::wstring& dll, std::wstring& msg) {
    logInjection(pid, L"injector started; bridge=" + dll);

    if (pid <= 4) {
        msg = L"system process rejected";
        logInjection(pid, msg);
        return 2;
    }
    if (!std::filesystem::exists(dll)) {
        msg = L"bridge DLL not found";
        logInjection(pid, msg);
        return 3;
    }

    HANDLE process = OpenProcess(
        PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION |
        PROCESS_VM_WRITE | PROCESS_VM_READ | SYNCHRONIZE,
        FALSE, pid);
    if (!process) {
        msg = L"OpenProcess failed: " + std::to_wstring(GetLastError());
        logInjection(pid, msg);
        return 4;
    }
    logInjection(pid, L"target process opened");

    std::uintptr_t bridgeBase = remoteModuleBaseFromPath(pid, dll);
    if (!bridgeBase) {
        const SIZE_T bytes = (dll.size() + 1) * sizeof(wchar_t);
        void* remotePath = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!remotePath) {
            msg = L"VirtualAllocEx failed: " + std::to_wstring(GetLastError());
            logInjection(pid, msg);
            CloseHandle(process);
            return 5;
        }
        logInjection(pid, L"remote DLL-path buffer allocated");

        if (!WriteProcessMemory(process, remotePath, dll.c_str(), bytes, nullptr)) {
            msg = L"WriteProcessMemory failed: " + std::to_wstring(GetLastError());
            logInjection(pid, msg);
            VirtualFreeEx(process, remotePath, 0, MEM_RELEASE);
            CloseHandle(process);
            return 6;
        }
        logInjection(pid, L"remote DLL path written");

        auto localLoadLibrary = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
        auto remoteLoadLibrary = resolveRemoteProcAddress(pid, localLoadLibrary);
        if (!remoteLoadLibrary) {
            msg = L"could not resolve target-safe LoadLibraryW address";
            logInjection(pid, msg);
            VirtualFreeEx(process, remotePath, 0, MEM_RELEASE);
            CloseHandle(process);
            return 7;
        }

        HANDLE loadThread = CreateRemoteThread(process, nullptr, 0, remoteLoadLibrary, remotePath, 0, nullptr);
        if (!loadThread) {
            msg = L"CreateRemoteThread(LoadLibraryW) failed: " + std::to_wstring(GetLastError());
            logInjection(pid, msg);
            VirtualFreeEx(process, remotePath, 0, MEM_RELEASE);
            CloseHandle(process);
            return 8;
        }
        logInjection(pid, L"remote LoadLibraryW started");

        const auto loadWait = waitForRemoteThread(pid, process, loadThread, L"LoadLibraryW");
        CloseHandle(loadThread);
        if (loadWait == RemoteWaitResult::Completed) {
            // The remote parameter is only released after the remote loader has
            // definitely stopped reading it. This is the critical invariant that
            // the old fixed 10-second timeout violated.
            VirtualFreeEx(process, remotePath, 0, MEM_RELEASE);
        } else {
            // If waiting failed, the remote thread may still be reading the path.
            // Never free it speculatively. If the target exited, its address space
            // is already gone; otherwise a tiny allocation leak is safer than UAF.
            msg = loadWait == RemoteWaitResult::TargetExited
                ? L"target exited while LoadLibraryW was pending"
                : L"LoadLibraryW wait failed; remote path intentionally retained";
            logInjection(pid, msg);
            CloseHandle(process);
            return 9;
        }

        bridgeBase = waitForRemoteModule(pid, dll);
        if (!bridgeBase) {
            msg = L"LoadLibraryW completed but bridge module is not present in target";
            logInjection(pid, msg);
            CloseHandle(process);
            return 10;
        }
        logInjection(pid, L"remote LoadLibraryW completed; bridge module confirmed");
    } else {
        logInjection(pid, L"bridge module already loaded; requesting idempotent BridgeStart");
    }

    if (!invokeBridgeStart(pid, process, bridgeBase, dll, msg)) {
        CloseHandle(process);
        return 11;
    }

    CloseHandle(process);
    msg = L"injected and bridge started";
    logInjection(pid, msg);
    return 0;
}

} // namespace udlss::injector
