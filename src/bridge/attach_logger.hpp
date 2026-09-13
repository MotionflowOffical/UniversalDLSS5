#pragma once
#include <string_view>
#include <cstdint>

namespace udlss::bridge {

enum class AttachLogStage {
    BridgeThreadStarted,
    SharedControlOpened,
    SafeAttachDelayComplete,
    DxgiBootstrapCreated,
    CoreHooksInstalled,
    WaitingForPresent,
    PresentObserved,
    SourceD3D11Detected,
    SourceD3D12Detected,
    D3D12QueueCaptured,
    D3D11TrackersInstalled,
    D3D12TrackersInstalled,
    GameGuideHooksInstalled,
    SafeAttachStable,
    D3D11PipelineInitialized,
    D3D12On12Initialized,
    NeuralProcessingStarted,
    NvidiaDriverDetected,
    ColorSpaceChanged,
    Unloading,
    Failure,
};

enum class CrashStage : std::uint32_t {
    Idle,
    ProcessEntry,
    QueueProof,
    BackbufferAcquire,
    PreCopy,
    On12Acquire,
    NeuralProcess,
    On12Release,
    PostCopy,
    ProcessComplete,
    CallingPresent,
    PresentReturned,
};

void initializeAttachLog();
void logAttachStage(AttachLogStage stage,std::wstring_view detail={});
void setCrashStage(CrashStage stage);
CrashStage crashStage();
void removeCrashStageHandler();

} // namespace udlss::bridge
