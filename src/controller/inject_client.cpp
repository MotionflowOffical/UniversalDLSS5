#include "inject_client.hpp"
#include <windows.h>
#include <filesystem>
#include <vector>

namespace udlss::controller {
namespace {
std::wstring q(const std::wstring& s) { return L"\"" + s + L"\""; }
}

bool injectBridge(const ProcessInfo& p, const std::wstring& dir, std::wstring& message) {
    if (!p.accessible) { message = L"process not accessible"; return false; }
    if (p.blocksThirdPartyModules) { message = L"process policy blocks third-party DLL loading"; return false; }
    if (p.arch == Arch::Arm64 || p.arch == Arch::Unknown) { message = L"unsupported architecture"; return false; }

    std::filesystem::path base(dir);
    std::wstring injector, bridge;
    if (p.arch == Arch::X86) {
        injector = (base / L"UniversalDLSS5.Injector32.exe").wstring();
        bridge = (base / L"UniversalDLSS5.Bridge32.dll").wstring();
    } else {
        injector = (base / L"UniversalDLSS5.Injector.exe").wstring();
        bridge = (base / L"UniversalDLSS5.Bridge.dll").wstring();
    }
    if (!std::filesystem::exists(injector) || !std::filesystem::exists(bridge)) {
        message = L"matching injector/bridge binary missing";
        return false;
    }

    std::wstring cmd = q(injector) + L" " + std::to_wstring(p.pid) + L" " + q(bridge);
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(0);

    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                        nullptr, dir.c_str(), &si, &pi)) {
        message = L"failed to launch injector";
        return false;
    }

    // The injector may legitimately remain alive while the target's loader is
    // busy. Do not block the controller and, crucially, do not treat that state
    // as a failed attach that should be retried into the same process.
    const DWORD wait = WaitForSingleObject(pi.hProcess, 1500);
    if (wait == WAIT_TIMEOUT) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        message = L"injection pending; injector continues in background";
        return true;
    }
    if (wait == WAIT_FAILED) {
        const auto err = GetLastError();
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        message = L"injector wait failed: " + std::to_wstring(err);
        return false;
    }

    DWORD exitCode = 1;
    if (!GetExitCodeProcess(pi.hProcess, &exitCode)) {
        const auto err = GetLastError();
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        message = L"could not read injector result: " + std::to_wstring(err);
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    if (exitCode != 0) {
        message = L"injector error " + std::to_wstring(exitCode) +
                  L" (see %LOCALAPPDATA%\\UniversalDLSS5\\logs\\inject-" +
                  std::to_wstring(p.pid) + L".log)";
        return false;
    }

    message = L"attached";
    return true;
}
}
