#include <cassert>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#ifndef UDLSS_SOURCE_DIR
#define UDLSS_SOURCE_DIR "."
#endif

static std::string readAll(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

int main() {
    const auto ui = readAll(std::filesystem::path(UDLSS_SOURCE_DIR) / "src/controller/ui.cpp");
    assert(!ui.empty());

    // The app picker must own a scroll offset and route wheel input to it
    // when the pointer is over the picker viewport instead of scrolling the page.
    assert(ui.find("appListScroll") != std::string::npos);
    assert(ui.find("appListViewport") != std::string::npos);
    assert(ui.find("scrollAppPicker") != std::string::npos);
    assert(ui.find("ensureAppPickerSelectionVisible") != std::string::npos);

    // Refresh gives visible feedback without delaying the actual enumeration.
    assert(ui.find("refreshingApps") != std::string::npos);
    assert(ui.find("kAppPickerRefreshFeedbackMs") != std::string::npos);
    assert(ui.find("Refreshing") != std::string::npos);
    assert(ui.find("SetTimer(g.hwnd") != std::string::npos);

    return 0;
}
