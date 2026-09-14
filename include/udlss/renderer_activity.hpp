#pragma once
#include <atomic>
#include <cstdint>
#include <windows.h>

namespace udlss::bridge {
inline std::atomic_uint64_t g_rendererActivityTickMs{0};
inline void noteRendererActivity() noexcept { g_rendererActivityTickMs.store(GetTickCount64(), std::memory_order_release); }
inline std::uint64_t lastRendererActivityTick() noexcept { return g_rendererActivityTickMs.load(std::memory_order_acquire); }
}
