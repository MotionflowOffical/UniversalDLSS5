#include "udlss/runtime_policy.hpp"
#include <cassert>
#include <string>

int main() {
    using namespace udlss;

    const auto required = requiredDlssNrRuntimeFiles();
    static_assert(required.size() == 1);
    assert(required[0] == L"nvngx_dlssnr.dll");

    const auto optional = optionalStreamlineRuntimeFiles();
    static_assert(optional.size() == 3);
    assert(optional[0] == L"sl.interposer.dll");
    assert(optional[1] == L"sl.common.dll");
    assert(optional[2] == L"sl.dlss_nr.dll");

    const std::array<std::wstring_view,1> missing{required[0]};
    const std::wstring report = formatMissingRuntimeFiles(missing);
    assert(report.find(L"nvngx_dlssnr.dll") != std::wstring::npos);
    assert(report.find(L"sl.dlss_nr.dll") == std::wstring::npos);
    return 0;
}
