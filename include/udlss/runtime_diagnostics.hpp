#pragma once
#include <array>
#include <cstdint>
#include <sstream>
#include <string>

namespace udlss {

enum class NeuralExecutionLocation : std::uint32_t { Unknown=0, InGame=1, ExternalHost=2 };
enum class NeuralBackendKind : std::uint32_t { Unknown=0, Streamline1004=1, SignedFeature18=2, ExternalHost=3, Passthrough=4 };

inline const wchar_t* neuralExecutionLocationLabel(NeuralExecutionLocation v) {
    switch(v) {
    case NeuralExecutionLocation::InGame: return L"IN-GAME";
    case NeuralExecutionLocation::ExternalHost: return L"EXTERNAL HOST";
    default: return L"UNKNOWN";
    }
}
inline const wchar_t* neuralBackendKindLabel(NeuralBackendKind v) {
    switch(v) {
    case NeuralBackendKind::Streamline1004: return L"Streamline feature 1004";
    case NeuralBackendKind::SignedFeature18: return L"Signed feature 18";
    case NeuralBackendKind::ExternalHost: return L"External NR host";
    case NeuralBackendKind::Passthrough: return L"Passthrough";
    default: return L"Unknown";
    }
}

struct RuntimeFeatureDiagnostics {
    std::uint32_t slMajor{}, slMinor{}, slBuild{};
    std::uint32_t ngxMajor{}, ngxMinor{}, ngxBuild{};
    std::uint32_t requiredTags{};
};

inline std::wstring formatRuntimeFeatureDiagnostics(const RuntimeFeatureDiagnostics& d) {
    if (!d.slMajor && !d.slMinor && !d.slBuild && !d.ngxMajor && !d.ngxMinor && !d.ngxBuild && !d.requiredTags)
        return {};
    std::wstringstream out;
    bool wrote=false;
    if (d.slMajor || d.slMinor || d.slBuild) {
        out << L"SL " << d.slMajor << L'.' << d.slMinor << L'.' << d.slBuild;
        wrote=true;
    }
    if (d.ngxMajor || d.ngxMinor || d.ngxBuild) {
        if (wrote) out << L" / ";
        out << L"NGX " << d.ngxMajor << L'.' << d.ngxMinor << L'.' << d.ngxBuild;
        wrote=true;
    }
    if (d.requiredTags) {
        if (wrote) out << L" / ";
        out << L"tags " << d.requiredTags;
    }
    return out.str();
}

enum class PipelineStage : std::uint32_t {
    None = 0,
    BridgeInjected = 1,
    HooksInstalled = 2,
    PresentObserved = 3,
    SourceApiDetected = 4,
    NeuralD3D12Ready = 5,
    GuideResourcesReady = 6,
    NgxCoreInitialized = 7,
    SnippetLoaded = 8,
    SnippetInitialized = 9,
    FeatureCreated = 10,
    FeatureEvaluated = 11,
    OutputComposited = 12,
    HostStarted = 13,
    HostConnected = 14,
    HostResourcesShared = 15,

    HookInstallFailed = 101,
    SourceApiUnsupported = 102,
    QueueCaptureFailed = 103,
    NeuralD3D12InitFailed = 104,
    GuideResourcesFailed = 105,
    NgxCoreInitFailed = 106,
    SnippetLoadFailed = 107,
    SnippetInitFailed = 108,
    FeatureCreateFailed = 109,
    FeatureEvaluateFailed = 110,
    OutputCompositeFailed = 111,
    HostLaunchFailed = 112,
    HostConnectFailed = 113,
    HostResourceShareFailed = 114,
    HostRuntimeFailed = 115,
};

inline constexpr std::uint64_t pipelineStageBit(PipelineStage stage) {
    const auto v = static_cast<std::uint32_t>(stage);
    return (v >= 1u && v <= 63u) ? (1ull << (v - 1u)) : 0ull;
}

inline constexpr void markPipelineStage(std::uint64_t& mask, PipelineStage stage) {
    mask |= pipelineStageBit(stage);
}

inline constexpr bool hasPipelineStage(std::uint64_t mask, PipelineStage stage) {
    return (mask & pipelineStageBit(stage)) != 0;
}

inline const wchar_t* pipelineStageLabel(PipelineStage stage) {
    switch(stage) {
    case PipelineStage::BridgeInjected: return L"Bridge injected";
    case PipelineStage::HooksInstalled: return L"DXGI hooks installed";
    case PipelineStage::PresentObserved: return L"Present observed";
    case PipelineStage::SourceApiDetected: return L"Source graphics API detected";
    case PipelineStage::NeuralD3D12Ready: return L"D3D12 neural device/queue";
    case PipelineStage::GuideResourcesReady: return L"Guide resources";
    case PipelineStage::NgxCoreInitialized: return L"NGX core initialized";
    case PipelineStage::SnippetLoaded: return L"DLSS-NR snippet loaded";
    case PipelineStage::SnippetInitialized: return L"DLSS-NR snippet initialized";
    case PipelineStage::FeatureCreated: return L"Feature 18 created";
    case PipelineStage::FeatureEvaluated: return L"Feature 18 evaluated";
    case PipelineStage::OutputComposited: return L"Output composited";
    case PipelineStage::HostStarted: return L"External NR host started";
    case PipelineStage::HostConnected: return L"External NR host connected";
    case PipelineStage::HostResourcesShared: return L"Host GPU resources shared";
    case PipelineStage::HookInstallFailed: return L"DXGI hooks installed";
    case PipelineStage::SourceApiUnsupported: return L"Source graphics API detected";
    case PipelineStage::QueueCaptureFailed: return L"D3D12 command queue captured";
    case PipelineStage::NeuralD3D12InitFailed: return L"D3D12 neural device/queue";
    case PipelineStage::GuideResourcesFailed: return L"Guide resources";
    case PipelineStage::NgxCoreInitFailed: return L"NGX core initialized";
    case PipelineStage::SnippetLoadFailed: return L"DLSS-NR snippet loaded";
    case PipelineStage::SnippetInitFailed: return L"DLSS-NR snippet initialized";
    case PipelineStage::FeatureCreateFailed: return L"Feature 18 created";
    case PipelineStage::FeatureEvaluateFailed: return L"Feature 18 evaluated";
    case PipelineStage::OutputCompositeFailed: return L"Output composited";
    case PipelineStage::HostLaunchFailed: return L"External NR host started";
    case PipelineStage::HostConnectFailed: return L"External NR host connected";
    case PipelineStage::HostResourceShareFailed: return L"Host GPU resources shared";
    case PipelineStage::HostRuntimeFailed: return L"External NR host runtime";
    default: return L"Unknown stage";
    }
}

inline PipelineStage successStageForFailure(PipelineStage failure) {
    switch(failure) {
    case PipelineStage::HookInstallFailed: return PipelineStage::HooksInstalled;
    case PipelineStage::SourceApiUnsupported: return PipelineStage::SourceApiDetected;
    case PipelineStage::QueueCaptureFailed: return PipelineStage::NeuralD3D12Ready;
    case PipelineStage::NeuralD3D12InitFailed: return PipelineStage::NeuralD3D12Ready;
    case PipelineStage::GuideResourcesFailed: return PipelineStage::GuideResourcesReady;
    case PipelineStage::NgxCoreInitFailed: return PipelineStage::NgxCoreInitialized;
    case PipelineStage::SnippetLoadFailed: return PipelineStage::SnippetLoaded;
    case PipelineStage::SnippetInitFailed: return PipelineStage::SnippetInitialized;
    case PipelineStage::FeatureCreateFailed: return PipelineStage::FeatureCreated;
    case PipelineStage::FeatureEvaluateFailed: return PipelineStage::FeatureEvaluated;
    case PipelineStage::OutputCompositeFailed: return PipelineStage::OutputComposited;
    case PipelineStage::HostLaunchFailed: return PipelineStage::HostStarted;
    case PipelineStage::HostConnectFailed: return PipelineStage::HostConnected;
    case PipelineStage::HostResourceShareFailed: return PipelineStage::HostResourcesShared;
    default: return PipelineStage::None;
    }
}

inline std::wstring formatPipelineStages(std::uint64_t mask, PipelineStage failure) {
    static constexpr std::array<PipelineStage,15> stages = {
        PipelineStage::BridgeInjected, PipelineStage::HooksInstalled, PipelineStage::PresentObserved,
        PipelineStage::SourceApiDetected, PipelineStage::HostStarted, PipelineStage::HostConnected,
        PipelineStage::NeuralD3D12Ready, PipelineStage::GuideResourcesReady, PipelineStage::HostResourcesShared,
        PipelineStage::NgxCoreInitialized, PipelineStage::SnippetLoaded, PipelineStage::SnippetInitialized,
        PipelineStage::FeatureCreated, PipelineStage::FeatureEvaluated, PipelineStage::OutputComposited
    };
    const PipelineStage failedSuccessStage = successStageForFailure(failure);
    std::wstringstream out;
    for (std::size_t i=0;i<stages.size();++i) {
        if (i) out << L"\r\n";
        const auto stage = stages[i];
        out << pipelineStageLabel(stage) << L": ";
        if (stage == failedSuccessStage) out << L"FAILED";
        else if (hasPipelineStage(mask,stage)) out << L"OK";
        else out << L"--";
    }
    return out.str();
}

} // namespace udlss
