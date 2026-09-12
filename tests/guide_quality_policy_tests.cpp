#include <cassert>
#include <cmath>
#include "udlss/guide_quality_policy.hpp"

using namespace udlss;

int main(){
    FlowCandidate center{0,0,0.0f};
    FlowCandidate far{-4,-4,0.0f};
    assert(flowCandidateBetter(center, far));
    assert(!flowCandidateBetter(far, center));

    FlowCandidate shortMove{1,0,0.02f};
    FlowCandidate longMove{3,0,0.02f};
    assert(flowCandidateBetter(shortMove,longMove));

    assert(!acceptMotionVector(0.1f, 0.9f, 0.20f, 0.5f)); // below deadzone
    assert(!acceptMotionVector(2.0f, 0.1f, 0.20f, 0.5f)); // low confidence
    assert(acceptMotionVector(2.0f, 0.8f, 0.20f, 0.5f));

    assert(!shouldResetNeuralHistory(false, false, false, false, false));
    assert(shouldResetNeuralHistory(true, false, false, false, false));
    assert(shouldResetNeuralHistory(false, true, false, false, false));
    assert(shouldResetNeuralHistory(false, false, true, false, false));
    assert(shouldResetNeuralHistory(false, false, false, true, false));
    assert(shouldResetNeuralHistory(false, false, false, false, true));

    assert(std::fabs(nrApplicationMask(0.0f,1.0f)-1.0f)<1e-6f);
    assert(std::fabs(nrApplicationMask(1.0f,1.0f)-0.0f)<1e-6f);
    assert(std::fabs(nrApplicationMask(0.5f,0.5f)-0.75f)<1e-6f);
    return 0;
}
