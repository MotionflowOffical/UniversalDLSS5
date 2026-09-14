#pragma once
#include <algorithm>
#include <cstdint>

namespace udlss {

struct AutoDetachState {
    bool everSawGameWindow{};
    std::uint64_t lastGameWindowTickMs{};
};

inline constexpr std::uint64_t kAutoDetachRendererSilenceMs = 5000;

inline bool shouldAutoDetach(
    const AutoDetachState& state,
    std::uint64_t nowMs,
    bool hasGameWindow,
    std::uint64_t lastRendererActivityMs,
    std::uint64_t silenceMs = kAutoDetachRendererSilenceMs) noexcept {
    if (!state.everSawGameWindow || hasGameWindow) return false;
    const auto lastActivity = std::max(state.lastGameWindowTickMs, lastRendererActivityMs);
    if (!lastActivity || nowMs < lastActivity) return false;
    return (nowMs - lastActivity) >= silenceMs;
}

} // namespace udlss
