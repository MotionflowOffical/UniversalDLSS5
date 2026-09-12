#pragma once
#include <array>
#include <span>
#include <string>
#include <string_view>

namespace udlss {

// The public Streamline 2.14.x SDK advertises kFeatureDLSS_NR but does not ship
// sl.dlss_nr.dll. The direct NGX-core path therefore requires only the feature
// runtime itself. Optional Streamline files may still be supplied for experiments,
// but attachment must never be blocked by their absence.
inline constexpr std::array<std::wstring_view,1> requiredDlssNrRuntimeFiles() {
    return {L"nvngx_dlssnr.dll"};
}

inline constexpr std::array<std::wstring_view,3> optionalStreamlineRuntimeFiles() {
    return {L"sl.interposer.dll", L"sl.common.dll", L"sl.dlss_nr.dll"};
}

inline std::wstring formatMissingRuntimeFiles(std::span<const std::wstring_view> missing) {
    if (missing.empty()) return L"";
    std::wstring out=L"Missing runtime files: ";
    for (std::size_t i=0;i<missing.size();++i) {
        if (i) out += L", ";
        out.append(missing[i]);
    }
    return out;
}

} // namespace udlss
