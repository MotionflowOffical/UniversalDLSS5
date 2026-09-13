#include "hook_lifecycle.hpp"

namespace udlss::bridge {
namespace {
HookQuiescence g_hookQuiescence;
}

HookQuiescence& globalHookQuiescence() { return g_hookQuiescence; }
HookCallScope::HookCallScope() noexcept : scope_(globalHookQuiescence()) {}
void resetHookLifecycle() noexcept { g_hookQuiescence.resetForAttach(); }
void beginHookUnload() noexcept { g_hookQuiescence.beginUnload(); }
bool waitForHookIdle(std::chrono::milliseconds timeout) { return g_hookQuiescence.waitForIdle(timeout); }
std::uint32_t activeHookCalls() noexcept { return g_hookQuiescence.activeCalls(); }

} // namespace udlss::bridge
