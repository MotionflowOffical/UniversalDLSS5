#pragma once
#include <string>
#include <vector>

namespace udlss::controller {

struct RuntimeImportItem {
    std::wstring name;
    std::wstring source;
    bool copied{};
    bool required{};
    std::wstring detail;
};

struct RuntimeImportResult {
    bool requiredRuntimeReady{};
    int copied{};
    int skipped{};
    std::vector<RuntimeImportItem> items;
    std::wstring summary;
};

RuntimeImportResult importNvidiaRuntimeFromSdk(const std::wstring& sdkRoot,
                                               const std::wstring& runtimeDestination);

} // namespace udlss::controller
