#pragma once
#include "settings.hpp"
#include "runtime_diagnostics.hpp"
#include "neural_route_policy.hpp"
#include <array>
#include <cstdint>
#include <cwchar>

namespace udlss {
constexpr std::uint32_t kControlMagic = 0x35534C44u; // DLS5
constexpr std::uint32_t kControlAbi = 16;
constexpr wchar_t kControlMapName[] = L"Local\\UniversalDLSS5.Control.v1";

enum class GraphicsApi : std::uint32_t { Unknown=0, D3D11=11, D3D12=12 };
enum class RuntimeState : std::uint32_t { Idle=0, Injected=1, Hooked=2, Processing=3, Bypassed=4, Error=5, Unloading=6 };

struct RuntimeStatus {
    std::uint32_t pid{};
    GraphicsApi api{GraphicsApi::Unknown};
    NeuralExecutionApi neuralApi{NeuralExecutionApi::None};
    RuntimeState state{RuntimeState::Idle};
    NeuralExecutionLocation neuralLocation{NeuralExecutionLocation::Unknown};
    NeuralBackendKind neuralBackendKind{NeuralBackendKind::Unknown};
    std::uint64_t stageMask{};
    PipelineStage failureStage{PipelineStage::None};
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint64_t presentedFrames{};
    std::uint64_t processedFrames{};
    std::uint64_t bypassedFrames{};
    std::uint64_t neuralFrames{};
    std::uint32_t neuralActive{};
    float lastGpuMs{};
    float estimatedFps{};
    std::uint64_t lastTickMs{};
    std::uint32_t requiredTagCount{};
    RuntimeFeatureDiagnostics feature{};
    std::uint32_t missingRequiredTag{};
    std::int32_t lastResult{};
    std::uint32_t attemptUnsupportedHardware{};
    std::uint32_t realGpuArchitecture{};
    std::uint32_t reportedGpuArchitecture{};
    std::uint32_t architectureCompatibilityActive{};
    std::uint32_t gameDepthActive{};
    std::uint32_t depthInverted{};
    std::uint32_t controlMaskActive{};
    std::uint32_t temporalResetThisFrame{};
    std::uint64_t nativeMotionCandidateId{};
    std::uint32_t nativeMotionCandidateScore{};
    std::uint32_t cameraCurrentValid{};
    std::uint32_t cameraPreviousValid{};
    std::uint32_t cameraConfidence{};
    std::uint32_t temporalHistoryValid{};
    std::uint32_t queueDepth{};
    std::uint32_t queueCapacity{};
    std::uint32_t queueLimit{};
    std::uint32_t neuralPassesRequested{1};
    std::uint32_t neuralPassesExecuted{};
    std::uint32_t reusedNeuralOutput{};
    std::uint32_t schedulerBackpressure{};
    std::uint64_t schedulerBackpressureFrames{};
    std::uint64_t reusedNeuralFrames{};
    wchar_t temporalReason[96]{};
    wchar_t backendName[64]{};
    wchar_t flowName[96]{};
    wchar_t depthName[96]{};
    wchar_t guideAdapterName[96]{};
    wchar_t guideFields[160]{};
    wchar_t message[384]{};
};

struct SharedControlBlock {
    std::uint32_t magic{kControlMagic};
    std::uint32_t abi{kControlAbi};
    volatile long settingsSeq{};
    volatile long statusSeq{};
    volatile long requestUnload{};
    volatile long profileGeneration{};
    volatile long historyResetGeneration{};
    volatile long neuralRetryGeneration{};
    volatile long primaryRendererPid{};
    std::uint32_t rootPid{};
    Settings settings{};
    wchar_t runtimePath[512]{};
    RuntimeStatus statuses[32]{};
};

class SharedControl {
public:
    SharedControl() = default;
    ~SharedControl();
    SharedControl(const SharedControl&) = delete;
    SharedControl& operator=(const SharedControl&) = delete;
    bool create();
    bool open();
    void close();
    bool valid() const { return block_ != nullptr; }
    SharedControlBlock* raw() const { return block_; }
    Settings readSettings() const;
    void writeSettings(const Settings& settings);
    RuntimeStatus readStatus() const;
    std::array<RuntimeStatus,32> readStatuses() const;
    void writeStatus(const RuntimeStatus& status);
    void setPrimaryRendererPid(std::uint32_t pid);
    std::uint32_t primaryRendererPid() const;
    void setRuntimePath(const wchar_t* path);
    void requestUnload(bool value);
    void requestHistoryReset();
    std::uint32_t historyResetGeneration() const;
    void requestNeuralRetry();
    std::uint32_t neuralRetryGeneration() const;
private:
    void* mapping_{};
    SharedControlBlock* block_{};
};
}
