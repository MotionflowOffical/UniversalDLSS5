#pragma once
#include "settings.hpp"
#include <string_view>

namespace udlss {
inline constexpr std::wstring_view backendDisplayName(BackendMode requested, bool fallback) {
    if (requested == BackendMode::InGameNR)
        return fallback ? std::wstring_view(L"Passthrough (direct + external NR unavailable)")
                        : std::wstring_view(L"Direct in-game DLSS 5 NR");
    if (requested == BackendMode::ExternalHostNR)
        return fallback ? std::wstring_view(L"Passthrough (NR host unavailable)")
                        : std::wstring_view(L"External DLSS 5 NR Host");
    return std::wstring_view(L"Passthrough");
}
inline constexpr BackendMode preferredBackend(bool directMountAvailable) {
    return directMountAvailable ? BackendMode::InGameNR : BackendMode::ExternalHostNR;
}
} // namespace udlss
