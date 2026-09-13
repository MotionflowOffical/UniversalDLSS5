#include "udlss/hook_quiescence.hpp"
#include <chrono>
#include <iostream>
#include <thread>

using namespace std::chrono_literals;

int main() {
    udlss::HookQuiescence q;

    // A hook that was already executing when detach begins must keep teardown
    // blocked until the hook returns.
    std::thread active([&] {
        udlss::HookQuiescence::Scope scope(q);
        if (!scope.customWorkAllowed()) std::exit(2);
        std::this_thread::sleep_for(30ms);
    });
    std::this_thread::sleep_for(5ms);
    q.beginUnload();
    if (q.waitForIdle(5ms)) {
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
