#pragma once
#include <algorithm>
#include <cstdint>

namespace udlss {

constexpr std::uint32_t kSettingsVersion = 10;

enum class BackendMode : std::uint32_t { InGameNR = 0, StreamlineDLSS5 = InGameNR, Passthrough = 1, ExternalHostNR = 2 };
enum class MotionSource : std::uint32_t { Auto = 0, SynthesizedOpticalFlow = 1, Zero = 2 };
enum class LatencyMode : std::uint32_t { UltraLow = 0, Balanced = 1, Quality = 2 };
enum class HdrMode : std::uint32_t { Auto = 0, SDR = 1, HDR = 2 };
enum class DepthGuideMode : std::uint32_t { Auto = 0, SyntheticFar = 1, ForceNormal = 2, ForceInverted = 3 };
enum class DebugView : std::uint32_t { Final = 0, Original = 1, Split = 2, Difference = 3, Motion = 4, MotionConfidence = 5, ControlMask = 6, Depth = 7, RawNeural = 8 };
enum class TuningPreset : std::uint32_t { Default = 0, Browser = 1, Game2D = 2, Video = 3, Aggressive = 4 };
enum class UiTheme : std::uint32_t { System = 0, Light = 1, Dark = 2 };
enum class FramePacingMode : std::uint32_t { Synchronized = 0, Adaptive = 1, Asynchronous = 2 };

struct Settings {
    std::uint32_t structVersion = kSettingsVersion;
    bool enabled = true;
    bool protectUI = true;
    bool protectCursor = true;
    bool useControlMask = true;
    bool invertMotionY = false;
    bool processSecondarySwapchains = false;
    bool attachProcessTree = true;
    bool allowD3D11On12 = true;
    bool attemptUnsupportedHardware = false;
    bool resetOnTemporalGap = true;
    bool useGameDepth = true;
    bool loadGameGuideAdapter = true;
    bool nrAutoMask = false;
    bool nrUiCorrection = false;

    BackendMode backend = BackendMode::InGameNR;
    MotionSource motionSource = MotionSource::Auto;
    LatencyMode latencyMode = LatencyMode::UltraLow;
    HdrMode hdrMode = HdrMode::Auto;
    DepthGuideMode depthMode = DepthGuideMode::Auto;
    DebugView debugView = DebugView::Final;
    UiTheme uiTheme = UiTheme::System;
    FramePacingMode framePacing = FramePacingMode::Synchronized;

    float sharpness = 0.15f;
    float exposure = 1.0f;
    float temporalStrength = 1.00f;
    float motionScale = 1.0f; // legacy/global multiplier
    float motionScaleX = 1.0f;
    float motionScaleY = 1.0f;
    float staticMotionDeadzone = 0.50f;
    float flowConfidenceThreshold = 0.20f;
    float disocclusionThreshold = 0.35f;
    float textProtection = 0.80f;
    float uiProtection = 0.90f;
    float controlMaskStrength = 0.70f;
    float historyClamp = 0.75f;
    float reactiveStrength = 0.25f;
    float edgeThreshold = 0.12f;

    // Verified DLSS-NR controls.
    // Keep the private Feature-18 controls neutral by default.  The prior
    // 0.85/0.45/-1 defaults were not a verified baseline and visibly shifted
    // tone/contrast on SDR content.
    float nrIntensity = 1.00f;
    float nrTone = 1.00f;
    float nrStructure = 1.00f;
    float nrSkinStructure = -1.00f;
    float nrPaperWhite = 1.0f;
    float nrTransferStrength = 1.0f;
    float nrColorStrength = 1.0f;
    float debugSplit = 0.5f;
    std::uint32_t nrStyle = 1;  // natural on runtimes that expose 0/1/2
    std::uint32_t nrPreset = 0;
    std::uint32_t nrPasses = 1; // 1 temporal pass + up to 3 same-frame refinement passes

    std::uint32_t flowSearchRadius = 6;
    std::uint32_t flowDownsample = 4;
    std::uint32_t maxFramesInFlight = 6;
};

inline Settings defaultSettings() { return Settings{}; }

struct FlowTuning {
    std::uint32_t downsample{};
    std::uint32_t searchRadius{};
};

inline FlowTuning effectiveFlowTuning(const Settings& s) {
    FlowTuning out{s.flowDownsample, s.flowSearchRadius};
    switch (s.latencyMode) {
    case LatencyMode::UltraLow:
        out.downsample = std::max<std::uint32_t>(out.downsample, 4);
        out.searchRadius = std::min<std::uint32_t>(out.searchRadius, 4);
        break;
    case LatencyMode::Balanced:
        break;
    case LatencyMode::Quality:
        out.downsample = std::min<std::uint32_t>(out.downsample, 2);
        out.searchRadius = std::min<std::uint32_t>(12, out.searchRadius + 2);
        break;
    }
    return out;
}

inline void normalize(Settings& s) {
    auto cf = [](float v, float lo, float hi) { return std::clamp(v, lo, hi); };
    s.sharpness = cf(s.sharpness, 0.0f, 1.0f);
    s.exposure = cf(s.exposure, 0.25f, 4.0f);
    s.temporalStrength = cf(s.temporalStrength, 0.0f, 1.0f);
    s.motionScale = cf(s.motionScale, 0.0f, 4.0f);
    s.motionScaleX = cf(s.motionScaleX, -4.0f, 4.0f);
    s.motionScaleY = cf(s.motionScaleY, -4.0f, 4.0f);
    s.staticMotionDeadzone = cf(s.staticMotionDeadzone, 0.0f, 8.0f);
    s.flowConfidenceThreshold = cf(s.flowConfidenceThreshold, 0.0f, 1.0f);
    s.disocclusionThreshold = cf(s.disocclusionThreshold, 0.0f, 1.0f);
    s.textProtection = cf(s.textProtection, 0.0f, 1.0f);
    s.uiProtection = cf(s.uiProtection, 0.0f, 1.0f);
    s.controlMaskStrength = cf(s.controlMaskStrength, 0.0f, 1.0f);
    s.historyClamp = cf(s.historyClamp, 0.0f, 1.0f);
    s.reactiveStrength = cf(s.reactiveStrength, 0.0f, 1.0f);
    s.edgeThreshold = cf(s.edgeThreshold, 0.0f, 1.0f);
    s.nrIntensity = cf(s.nrIntensity, 0.0f, 2.0f);
    s.nrTone = cf(s.nrTone, 0.0f, 2.0f);
    s.nrStructure = cf(s.nrStructure, 0.0f, 2.0f);
    s.nrSkinStructure = cf(s.nrSkinStructure, -1.0f, 2.0f);
    s.nrPaperWhite = cf(s.nrPaperWhite, 0.1f, 16.0f);
    s.nrTransferStrength = cf(s.nrTransferStrength, 0.0f, 2.0f);
    s.nrColorStrength = cf(s.nrColorStrength, 0.0f, 4.0f);
    s.debugSplit = cf(s.debugSplit, 0.0f, 1.0f);
    s.nrStyle = std::min<std::uint32_t>(s.nrStyle, 6);
    s.nrPreset = std::min<std::uint32_t>(s.nrPreset, 3);
    s.nrPasses = std::clamp<std::uint32_t>(s.nrPasses, 1, 4);
    if(static_cast<std::uint32_t>(s.uiTheme)>2) s.uiTheme=UiTheme::System;
    if(static_cast<std::uint32_t>(s.framePacing)>2) s.framePacing=FramePacingMode::Synchronized;
    s.flowSearchRadius = std::clamp<std::uint32_t>(s.flowSearchRadius, 1, 12);
    // Supported GPU kernels are specialized for 1x/2x/4x/8x reduction.
    if (s.flowDownsample <= 1) s.flowDownsample = 1;
    else if (s.flowDownsample <= 2) s.flowDownsample = 2;
    else if (s.flowDownsample <= 4) s.flowDownsample = 4;
    else s.flowDownsample = 8;
    s.maxFramesInFlight = std::clamp<std::uint32_t>(s.maxFramesInFlight, 3, 8);
    s.structVersion = kSettingsVersion;
}

inline Settings settingsForPreset(TuningPreset preset) {
    Settings s = defaultSettings();
    switch (preset) {
    case TuningPreset::Browser:
        s.protectUI = true;
        s.protectCursor = true;
        s.textProtection = 0.96f;
        s.uiProtection = 0.96f;
        s.temporalStrength = 0.68f;
        s.reactiveStrength = 0.35f;
        s.historyClamp = 0.88f;
        s.edgeThreshold = 0.08f;
        s.sharpness = 0.18f;
        s.latencyMode = LatencyMode::UltraLow;
        s.flowDownsample = 4;
        s.flowSearchRadius = 4;
        break;
    case TuningPreset::Game2D:
        s.protectUI = true;
        s.protectCursor = true;
        s.textProtection = 0.88f;
        s.uiProtection = 0.92f;
        s.temporalStrength = 0.82f;
        s.reactiveStrength = 0.40f;
        s.historyClamp = 0.80f;
        s.sharpness = 0.24f;
        s.latencyMode = LatencyMode::UltraLow;
        s.flowDownsample = 4;
        s.flowSearchRadius = 4;
        break;
    case TuningPreset::Video:
        s.protectUI = true;
        s.protectCursor = true;
        s.textProtection = 0.90f;
        s.uiProtection = 0.93f;
        s.temporalStrength = 0.78f;
        s.reactiveStrength = 0.22f;
        s.historyClamp = 0.72f;
        s.sharpness = 0.12f;
        s.latencyMode = LatencyMode::Balanced;
        s.flowDownsample = 2;
        s.flowSearchRadius = 6;
        break;
    case TuningPreset::Aggressive:
        s.protectUI = true;
        s.protectCursor = true;
        s.textProtection = 0.72f;
        s.uiProtection = 0.80f;
        s.temporalStrength = 0.94f;
        s.reactiveStrength = 0.30f;
        s.historyClamp = 0.64f;
        s.sharpness = 0.36f;
        s.latencyMode = LatencyMode::Quality;
        s.flowDownsample = 2;
        s.flowSearchRadius = 8;
        break;
    case TuningPreset::Default:
    default:
        break;
    }
    normalize(s);
    return s;
}

inline void applyPreset(Settings& target, TuningPreset preset) {
    const bool enabled = target.enabled;
    const bool attachTree = target.attachProcessTree;
    const bool secondary = target.processSecondarySwapchains;
    const bool on12 = target.allowD3D11On12;
    const bool unsupported = target.attemptUnsupportedHardware;
    const BackendMode backend = target.backend;
    const HdrMode hdr = target.hdrMode;
    const DepthGuideMode depth = target.depthMode;
    const DebugView debug = target.debugView;
    const UiTheme theme = target.uiTheme;
    const FramePacingMode framePacing = target.framePacing;
    const std::uint32_t nrPasses = target.nrPasses;
    const std::uint32_t maxFramesInFlight = target.maxFramesInFlight;
    const bool gameDepth = target.useGameDepth;
    const bool adapter = target.loadGameGuideAdapter;
    target = settingsForPreset(preset);
    target.enabled = enabled;
    target.attachProcessTree = attachTree;
    target.processSecondarySwapchains = secondary;
    target.allowD3D11On12 = on12;
    target.attemptUnsupportedHardware = unsupported;
    target.backend = backend;
    target.hdrMode = hdr;
    target.depthMode = depth;
    target.debugView = debug;
    target.uiTheme = theme;
    target.framePacing = framePacing;
    target.nrPasses = nrPasses;
    target.maxFramesInFlight = maxFramesInFlight;
    target.useGameDepth = gameDepth;
    target.loadGameGuideAdapter = adapter;
    normalize(target);
}

} // namespace udlss
