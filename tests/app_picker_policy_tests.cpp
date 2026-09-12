#include "udlss/app_picker_policy.hpp"
#include <cassert>
#include <string>
#include <vector>

using namespace udlss;

int main() {
    std::vector<AppPickerProcess> p{
        {100, 10, L"brave.exe", L"C:\\Program Files\\BraveSoftware\\Brave-Browser\\Application\\brave.exe", true,  true,  false, false},
        {101,100, L"brave.exe", L"C:\\Program Files\\BraveSoftware\\Brave-Browser\\Application\\brave.exe", true,  false, true,  false},
        {102,100, L"brave.exe", L"C:\\Program Files\\BraveSoftware\\Brave-Browser\\Application\\brave.exe", true,  false, false, false},
        {103,100, L"brave.exe", L"C:\\Program Files\\BraveSoftware\\Brave-Browser\\Application\\brave.exe", true,  true,  false, false},
        {200, 20, L"wallpaper64.exe", L"C:\\Steam\\wallpaper_engine\\wallpaper64.exe", true, true, true, false},
        {300, 30, L"AvastSvc.exe", L"C:\\Program Files\\Avast Software\\Avast\\AvastSvc.exe", true, false, false, false},
        {301, 30, L"AvastUI.exe", L"C:\\Program Files\\Avast Software\\Avast\\AvastUI.exe", true, true, false, true},
        {400, 40, L"hidden.exe", L"C:\\Tools\\hidden.exe", false, true, true, false},
    };

    const auto groups = groupVisibleApplications(p);
    assert(groups.size() == 3);

    const auto* brave = findApplicationGroup(groups, L"C:\\Program Files\\BraveSoftware\\Brave-Browser\\Application\\brave.exe");
    assert(brave);
    assert(brave->rootPid == 100);
    assert(brave->processCount == 4);
    assert(brave->visibleWindowCount == 2);
    assert(brave->anyDxgi);
    assert(!brave->blocksThirdPartyModules);

    const auto* wallpaper = findApplicationGroup(groups, L"C:\\Steam\\wallpaper_engine\\wallpaper64.exe");
    assert(wallpaper && wallpaper->rootPid == 200 && wallpaper->anyDxgi);

    const auto* avast = findApplicationGroup(groups, L"C:\\Program Files\\Avast Software\\Avast\\AvastUI.exe");
    assert(avast && avast->rootPid == 301 && avast->blocksThirdPartyModules);

    assert(findApplicationGroup(groups, L"C:\\Tools\\hidden.exe") == nullptr);
    return 0;
}
