#include <cassert>
#include <cmath>
#include "udlss/settings.hpp"

using namespace udlss;

static bool nearf(float a, float b) { return std::fabs(a-b) < 1e-6f; }

int main() {
    Settings s{};
    s.sharpness = 5.0f;
    s.exposure = 0.01f;
    s.temporalStrength = -2.0f;
    s.motionScale = 20.0f;
    s.flowSearchRadius = 99;
    s.flowDownsample = 3;
    s.textProtection = 2.0f;
    s.disocclusionThreshold = -1.0f;
    s.maxFramesInFlight = 99;
    s.staticMotionDeadzone = -1.0f;
    s.nrIntensity = 9.0f;
    s.nrTone = -2.0f;
    s.nrStructure = 9.0f;
    s.nrSkinStructure = -9.0f;
    s.nrPaperWhite = 99.0f;
    s.nrTransferStrength = 9.0f;
    s.nrColorStrength = -3.0f;
    s.motionScaleX = 9.0f;
    s.motionScaleY = -9.0f;
    normalize(s);
    assert(nearf(s.sharpness, 1.0f));
    assert(nearf(s.exposure, 0.25f));
    assert(nearf(s.temporalStrength, 0.0f));
    assert(nearf(s.motionScale, 4.0f));
    assert(s.flowSearchRadius == 12);
    assert(s.flowDownsample == 4); // normalized to supported power-of-two
    assert(nearf(s.textProtection, 1.0f));
    assert(nearf(s.disocclusionThreshold, 0.0f));
    assert(s.maxFramesInFlight == 3);
    assert(nearf(s.staticMotionDeadzone, 0.0f));
    assert(nearf(s.nrIntensity, 2.0f));
    assert(nearf(s.nrTone, 0.0f));
    assert(nearf(s.nrStructure, 2.0f));
    assert(nearf(s.nrSkinStructure, -1.0f));
    assert(nearf(s.nrPaperWhite, 16.0f));
    assert(nearf(s.nrTransferStrength, 2.0f));
    assert(nearf(s.nrColorStrength, 0.0f));
    assert(nearf(s.motionScaleX, 4.0f));
    assert(nearf(s.motionScaleY, -4.0f));

    Settings d = defaultSettings();
    assert(d.enabled);
    assert(d.backend == BackendMode::InGameNR);
    assert(d.motionSource == MotionSource::Auto);
    assert(d.flowDownsample == 4);
    assert(d.flowSearchRadius >= 2);
    assert(d.resetOnTemporalGap);
    assert(d.depthMode == DepthGuideMode::Auto);
    assert(d.debugView == DebugView::Final);
    assert(nearf(d.nrIntensity, 0.85f));
    assert(nearf(d.nrTone, 0.45f));
    assert(nearf(d.nrStructure, 1.0f));
    assert(nearf(d.nrSkinStructure, -1.0f));

    Settings ultra = d;
    ultra.latencyMode = LatencyMode::UltraLow;
    ultra.flowDownsample = 2;
    ultra.flowSearchRadius = 9;
    auto u = effectiveFlowTuning(ultra);
    assert(u.downsample == 4);
    assert(u.searchRadius == 4);

    Settings balanced = d;
    balanced.latencyMode = LatencyMode::Balanced;
    balanced.flowDownsample = 4;
    balanced.flowSearchRadius = 7;
    auto b = effectiveFlowTuning(balanced);
    assert(b.downsample == 4);
    assert(b.searchRadius == 7);

    Settings quality = d;
    quality.latencyMode = LatencyMode::Quality;
    quality.flowDownsample = 4;
    quality.flowSearchRadius = 7;
    auto q = effectiveFlowTuning(quality);
    assert(q.downsample == 2);
    assert(q.searchRadius == 9);
    return 0;
}
