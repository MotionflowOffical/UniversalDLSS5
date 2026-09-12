#pragma once
#include <windows.h>
#include <string>
#include <vector>
namespace udlss::controller {
enum class Arch { Unknown, X86, X64, Arm64 };
struct ProcessInfo {
    DWORD pid{}, parentPid{};
    std::wstring name;
    std::wstring path;
    Arch arch{Arch::Unknown};
    bool accessible{};
    bool hasDxgi{};
    bool blocksThirdPartyModules{};
    bool visibleTopLevel{};
};
struct ApplicationInfo {
    ProcessInfo root;
    std::wstring displayName;
    std::size_t processCount{};
    std::size_t visibleWindowCount{};
    bool anyDxgi{};
    bool blocksThirdPartyModules{};
};
std::vector<ProcessInfo> enumerateProcesses();
std::vector<ApplicationInfo> enumerateApplications();
std::vector<ProcessInfo> processTree(DWORD rootPid);
bool moduleLoaded(DWORD pid, const wchar_t* moduleName);
Arch processArch(HANDLE process);
}
