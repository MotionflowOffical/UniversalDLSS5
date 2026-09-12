#include "udlss/d3d12_backbuffer_policy.hpp"
#include <cassert>

int main() {
    using namespace udlss;
    assert(!shouldResetTemporalHistoryForBackbuffer(false, false));
    assert(!shouldResetTemporalHistoryForBackbuffer(true, false)); // normal flip-model rotation
    assert(shouldResetTemporalHistoryForBackbuffer(false, true));  // actual resize
    assert(shouldResetTemporalHistoryForBackbuffer(true, true));
    return 0;
}
