#pragma once
#include <cstdint>

namespace udlss {

enum class GameGuideSource : std::uint32_t {
    None=0,
    D3D11Tracker=1,
    D3D12Tracker=2,
    Streamline=3,
    Ngx=4,
    GameAdapter=5,
};

inline constexpr std::uint32_t guideSourcePriority(GameGuideSource source) {
    switch(source) {
    case GameGuideSource::GameAdapter: return 500;
    case GameGuideSource::Ngx: return 400;
    case GameGuideSource::Streamline: return 350;
    case GameGuideSource::D3D12Tracker: return 200;
    case GameGuideSource::D3D11Tracker: return 150;
    default: return 0;
    }
}

inline constexpr bool guideCaptureFresh(std::uint64_t capturedTickMs,
                                        std::uint64_t nowTickMs,
                                        std::uint64_t maxAgeMs=750) {
    return capturedTickMs!=0 && nowTickMs>=capturedTickMs && (nowTickMs-capturedTickMs)<=maxAgeMs;
}

} // namespace udlss
