#include "udlss/hook_quiescence.hpp"
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

using namespace std::chrono_literals;

int main() {
    udlss::HookQuiescence q;

    // A hook that was already executing when detach begins must keep teardown
    // blocked until the hook returns.
    std::atomic<bool> entered{false};
    std::atomic<bool> workerError{false};
    std::thread active([&] {
        udlss::HookQuiescence::Scope scope(q);
        if (!scope.customWorkAllowed()) {
            workerError.store(true, std::memory_order_release);
            entered.store(true, std::memory_order_release);
            return;
        }
        entered.store(true, std::memory_order_release);
        std::this_thread::sleep_for(30ms);
    });

    // Do not use a fixed sleep as a proxy for thread scheduling.  Windows can
    // legitimately leave the worker unscheduled for several milliseconds,
    // which made this test report a false quiescence failure and then abort
    // while destroying a still-joinable std::thread.
    const auto enteredDeadline = std::chrono::steady_clock::now() + 1s;
    while (!entered.load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() < enteredDeadline) {
        std::this_thread::yield();
    }
    if (!entered.load(std::memory_order_acquire)) {
        active.join();
        std::cerr << "hook worker did not enter scope before timeout\n";
        return 1;
    }
    if (workerError.load(std::memory_order_acquire)) {
        active.join();
        std::cerr << "hook worker unexpectedly rejected custom work before unload\n";
        return 1;
    }

    q.beginUnload();
    if (q.waitForIdle(5ms)) {
        active.join();
        std::cerr << "detach incorrectly considered an active hook quiescent\n";
        return 1;
    }
    active.join();
    if (!q.waitForIdle(50ms)) {
        std::cerr << "detach did not observe hook completion\n";
        return 1;
    }

    // Hooks that slip in after unload starts are still counted for module
    // lifetime, but must not touch injector-owned state.
    {
        udlss::HookQuiescence::Scope late(q);
        if (late.customWorkAllowed()) {
            std::cerr << "late hook was allowed to run custom work during unload\n";
            return 1;
        }
        if (q.activeCalls() != 1) {
            std::cerr << "late hook was not counted for teardown safety\n";
            return 1;
        }
    }

    q.resetForAttach();
    {
        udlss::HookQuiescence::Scope afterReset(q);
        if (!afterReset.customWorkAllowed()) {
            std::cerr << "reattach did not reopen custom hook work\n";
            return 1;
        }
    }
    return 0;
}
