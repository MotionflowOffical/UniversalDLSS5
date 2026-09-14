#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>
#include "renderer_route_policy.hpp"

namespace udlss {

inline constexpr DetachMode detachDecision(bool hooksDisabled, bool quiesced) noexcept {
    return (hooksDisabled && quiesced) ? DetachMode::Unloaded : DetachMode::DetachedResident;
}

// Tracks bridge detours that are already executing while Detach disables the
// hook entry points.  Teardown must never destroy pipeline state or MinHook
// trampolines until this count reaches zero.
class HookQuiescence {
public:
    class Scope {
    public:
        explicit Scope(HookQuiescence& owner) noexcept
            : owner_(&owner) {
            owner_->active_.fetch_add(1, std::memory_order_acq_rel);
            customWorkAllowed_ = !owner_->unloading_.load(std::memory_order_acquire);
        }

        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;

        ~Scope() {
            if (owner_) owner_->active_.fetch_sub(1, std::memory_order_acq_rel);
        }

        bool customWorkAllowed() const noexcept { return customWorkAllowed_; }

    private:
        HookQuiescence* owner_{};
        bool customWorkAllowed_{};
    };

    void beginUnload() noexcept { unloading_.store(true, std::memory_order_release); }
    void resetForAttach() noexcept { unloading_.store(false, std::memory_order_release); }
    bool unloading() const noexcept { return unloading_.load(std::memory_order_acquire); }
    std::uint32_t activeCalls() const noexcept { return active_.load(std::memory_order_acquire); }

    bool waitForIdle(std::chrono::milliseconds timeout) const {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        unsigned stableZeroSamples = 0;
        do {
            if (activeCalls() == 0) {
                // Require zero to remain stable across a few scheduler turns.
                // MH_DisableHook has already closed new entry points when this
                // is used during teardown; the extra samples cover a thread
                // that was already at a detour prologue when hooks were disabled.
                if (++stableZeroSamples >= 3) return true;
            } else {
                stableZeroSamples = 0;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (std::chrono::steady_clock::now() < deadline);
        return activeCalls() == 0;
    }

private:
    std::atomic<std::uint32_t> active_{0};
    std::atomic<bool> unloading_{false};
};

} // namespace udlss
