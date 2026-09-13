#pragma once
#include "udlss/hook_quiescence.hpp"
#include <chrono>

namespace udlss::bridge {

HookQuiescence& globalHookQuiescence();

class HookCallScope {
public:
    HookCallScope() noexcept;
    bool customWorkAllowed() const noexcept { return scope_.customWorkAllowed(); }
private:
    HookQuiescence::Scope scope_;
};

void resetHookLifecycle() noexcept;
void beginHookUnload() noexcept;
bool waitForHookIdle(std::chrono::milliseconds timeout);
std::uint32_t activeHookCalls() noexcept;

} // namespace udlss::bridge
