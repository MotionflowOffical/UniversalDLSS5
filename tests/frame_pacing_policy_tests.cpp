#include "udlss/neural_scheduler_policy.hpp"
#include "udlss/settings.hpp"
#include <iostream>
using namespace udlss;

int main() {
    if(defaultSettings().framePacing != FramePacingMode::Synchronized) {
        std::cerr << "synchronized pacing is not the default\n";
        return 1;
    }

    auto d = choosePresentation(FramePacingMode::Synchronized, true, 0);
    if(!d.waitForCurrent || d.useCached) {
        std::cerr << "synchronized pacing may present stale output\n";
        return 2;
    }

    d = choosePresentation(FramePacingMode::Adaptive, true, 1);
    if(d.waitForCurrent || !d.useCached) {
        std::cerr << "adaptive pacing did not accept one-frame-old output\n";
        return 3;
    }

    d = choosePresentation(FramePacingMode::Adaptive, true, 2);
    if(!d.waitForCurrent || d.useCached) {
        std::cerr << "adaptive pacing accepted output older than one frame\n";
        return 4;
    }

    d = choosePresentation(FramePacingMode::Adaptive, false, 0);
    if(!d.waitForCurrent || d.useCached) {
        std::cerr << "adaptive warm-up did not synchronize\n";
        return 5;
    }

    d = choosePresentation(FramePacingMode::Asynchronous, true, 9);
    if(d.waitForCurrent || !d.useCached) {
        std::cerr << "asynchronous mode unexpectedly blocks\n";
        return 6;
    }

    if(!shouldForceTemporalResetForWeakGuides(MotionSource::Zero, false, false, false)) {
        std::cerr << "zero-motion synthetic guides did not disable temporal accumulation\n";
        return 7;
    }
    if(shouldForceTemporalResetForWeakGuides(MotionSource::Zero, true, false, false)) {
        std::cerr << "real depth alone should not be classified as the exact synthetic-guide failure case\n";
        return 8;
    }
    return 0;
}
