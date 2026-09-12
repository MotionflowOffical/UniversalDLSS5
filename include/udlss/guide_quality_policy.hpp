#pragma once
#include <algorithm>
#include <cmath>

namespace udlss {

struct FlowCandidate {
    int dx{};
    int dy{};
    float error{};
};

inline constexpr int flowManhattan(const FlowCandidate& c) {
    return (c.dx < 0 ? -c.dx : c.dx) + (c.dy < 0 ? -c.dy : c.dy);
}

// Lower error wins. Ties prefer the shortest reprojection, then deterministic axes.
// This prevents flat/static patches from inheriting the first extreme search offset.
inline bool flowCandidateBetter(const FlowCandidate& candidate, const FlowCandidate& incumbent,
                                float epsilon = 1.0e-6f) {
    if (candidate.error + epsilon < incumbent.error) return true;
    if (incumbent.error + epsilon < candidate.error) return false;
    const int cm = flowManhattan(candidate), im = flowManhattan(incumbent);
    if (cm != im) return cm < im;
    const int ca = candidate.dx * candidate.dx + candidate.dy * candidate.dy;
    const int ia = incumbent.dx * incumbent.dx + incumbent.dy * incumbent.dy;
    if (ca != ia) return ca < ia;
    if (candidate.dy != incumbent.dy) return candidate.dy < incumbent.dy;
    return candidate.dx < incumbent.dx;
}

inline bool acceptMotionVector(float magnitudePixels, float confidence,
                               float confidenceThreshold, float staticDeadzonePixels) {
    return magnitudePixels >= std::max(0.0f, staticDeadzonePixels) &&
           confidence >= std::clamp(confidenceThreshold, 0.0f, 1.0f);
}

inline bool shouldResetNeuralHistory(bool neuralFrameSkipped,
                                     bool resolutionChanged,
                                     bool backendReset,
                                     bool cameraCut,
                                     bool guideDiscontinuity) {
    return neuralFrameSkipped || resolutionChanged || backendReset || cameraCut || guideDiscontinuity;
}

// DLSSNR.ControlMask uses R=1 to apply NR and R=0 to bypass NR.
// Our internal reliability mask is the opposite (1 = unreliable/protected).
inline float nrApplicationMask(float unreliableOrProtected, float strength) {
    const float u = std::clamp(unreliableOrProtected, 0.0f, 1.0f);
    const float s = std::clamp(strength, 0.0f, 1.0f);
    return std::clamp(1.0f - u * s, 0.0f, 1.0f);
}

} // namespace udlss
