#include "udlss/app_picker_policy.hpp"
#include <cassert>
#include <string>
#include <vector>

using namespace udlss;

int main() {
    std::vector<AppPickerProcess> p{
        {100, 10, L"brave.exe", L"C:\\Program Files\\BraveSoftware\\Brave-Browser\\Application\\brave.exe", true,  true,  false, false, AppRendererD3D11},
        {101,100, L"brave.exe", L"C:\\Program Files\\BraveSoftware\\Brave-Browser\\Application\\brave.exe", true,  false, true,  false, AppRendererD3D12},
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
    assert(brave->rendererModules == (AppRendererD3D11 | AppRendererD3D12));
    assert(!brave->blocksThirdPartyModules);

    const auto* wallpaper = findApplicationGroup(groups, L"C:\\Steam\\wallpaper_engine\\wallpaper64.exe");
    assert(wallpaper && wallpaper->rootPid == 200 && wallpaper->anyDxgi);

    const auto* avast = findApplicationGroup(groups, L"C:\\Program Files\\Avast Software\\Avast\\AvastUI.exe");
    assert(avast && avast->rootPid == 301 && avast->blocksThirdPartyModules);

    assert(findApplicationGroup(groups, L"C:\\Tools\\hidden.exe") == nullptr);

    // App-list viewport policy: wheel scrolling is row-based, clamped, and
    // selection changes keep the selected application visible.
    assert(appPickerVisibleRows(240.0f, 48.0f) == 5);
    assert(clampAppPickerScroll(0, 12, 5) == 0);
    assert(clampAppPickerScroll(20, 12, 5) == 7);
    assert(scrollAppPicker(0, -120, 12, 5) == 1);
    assert(scrollAppPicker(3, 120, 12, 5) == 2);
    assert(ensureAppPickerSelectionVisible(0, 7, 12, 5) == 3);
    assert(ensureAppPickerSelectionVisible(5, 2, 12, 5) == 2);

    // Refresh scans immediately but the UI keeps a short, deterministic
    // feedback window so a fast enumeration still feels responsive.
    static_assert(kAppPickerRefreshFeedbackMs == 450);
    assert(appPickerRefreshFeedbackActive(1000, 1200));
    assert(!appPickerRefreshFeedbackActive(1000, 1450));
    return 0;
}
