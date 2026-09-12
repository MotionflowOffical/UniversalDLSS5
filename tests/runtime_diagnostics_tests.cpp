#include "udlss/runtime_diagnostics.hpp"
#include <cassert>
#include <string>

int main() {
    using namespace udlss;
    RuntimeFeatureDiagnostics d{};
    d.slMajor=2; d.slMinor=14; d.slBuild=1;
    d.ngxMajor=310; d.ngxMinor=8; d.ngxBuild=0;
    d.requiredTags=4;
    const std::wstring text=formatRuntimeFeatureDiagnostics(d);
    assert(text.find(L"SL 2.14.1") != std::wstring::npos);
    assert(text.find(L"NGX 310.8.0") != std::wstring::npos);
    assert(text.find(L"tags 4") != std::wstring::npos);

    assert(std::wstring_view(neuralExecutionLocationLabel(NeuralExecutionLocation::InGame))==L"IN-GAME");
    assert(std::wstring_view(neuralExecutionLocationLabel(NeuralExecutionLocation::ExternalHost))==L"EXTERNAL HOST");
    assert(std::wstring_view(neuralBackendKindLabel(NeuralBackendKind::Streamline1004))==L"Streamline feature 1004");
    assert(std::wstring_view(neuralBackendKindLabel(NeuralBackendKind::SignedFeature18))==L"Signed feature 18");

    RuntimeFeatureDiagnostics empty{};
    assert(formatRuntimeFeatureDiagnostics(empty).empty());

    std::uint64_t stages=0;
    markPipelineStage(stages, PipelineStage::BridgeInjected);
    markPipelineStage(stages, PipelineStage::HooksInstalled);
    markPipelineStage(stages, PipelineStage::PresentObserved);
    markPipelineStage(stages, PipelineStage::SourceApiDetected);
    markPipelineStage(stages, PipelineStage::NeuralD3D12Ready);
    markPipelineStage(stages, PipelineStage::NgxCoreInitialized);
    markPipelineStage(stages, PipelineStage::SnippetLoaded);
    markPipelineStage(stages, PipelineStage::SnippetInitialized);
    assert(hasPipelineStage(stages, PipelineStage::BridgeInjected));
    assert(hasPipelineStage(stages, PipelineStage::NeuralD3D12Ready));
    assert(!hasPipelineStage(stages, PipelineStage::FeatureCreated));
    const auto stageText=formatPipelineStages(stages, PipelineStage::FeatureCreateFailed);
    assert(stageText.find(L"Bridge injected: OK") != std::wstring::npos);
    assert(stageText.find(L"Feature 18 created: FAILED") != std::wstring::npos);
    assert(stageText.find(L"D3D12 neural device/queue: OK") != std::wstring::npos);
    assert(stageText.find(L"DLSS-NR snippet loaded: OK") != std::wstring::npos);
    assert(stageText.find(L"DLSS-NR snippet initialized: OK") != std::wstring::npos);
    std::uint64_t hostStages=0;
    markPipelineStage(hostStages, PipelineStage::BridgeInjected);
    markPipelineStage(hostStages, PipelineStage::HostStarted);
    markPipelineStage(hostStages, PipelineStage::HostConnected);
    markPipelineStage(hostStages, PipelineStage::NeuralD3D12Ready);
    markPipelineStage(hostStages, PipelineStage::HostResourcesShared);
    markPipelineStage(hostStages, PipelineStage::NgxCoreInitialized);
    const auto hostText=formatPipelineStages(hostStages, PipelineStage::None);
    assert(hostText.find(L"External NR host started") < hostText.find(L"NGX core initialized"));
    assert(hostText.find(L"Host GPU resources shared") < hostText.find(L"NGX core initialized"));
    return 0;
}
