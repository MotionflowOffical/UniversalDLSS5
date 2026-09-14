#pragma once
#include <windows.h>
#include <cstdint>

namespace udlss::neural::hostipc {
constexpr std::uint32_t kMagic=0x48524E35u; // 5NRH
constexpr std::uint32_t kAbi=6;
enum class State : LONG { Booting=0, WaitingConfig=1, Configuring=2, Ready=3, Active=4, Error=5, Stopping=6 };
enum class Route : LONG { None=0, CoreDispatch=1, SignedSnippet=2 };
#pragma pack(push,8)
struct Shared {
    std::uint32_t magic{kMagic};
    std::uint32_t abi{kAbi};
    volatile LONG state{static_cast<LONG>(State::Booting)};
    volatile LONG route{static_cast<LONG>(Route::None)};
    volatile LONG configGeneration{};
    volatile LONG frameSeq{};
    volatile LONG completedSeq{};
    volatile LONG enqueuedSeq{};
    volatile LONG stopRequested{};
    std::uint32_t bridgePid{};
    std::uint32_t hostPid{};
    std::uint32_t sourceApi{};
    std::uint32_t rendererRoute{};
    std::uint32_t bridgeArchitectureBits{};
    std::uint32_t canonicalColorSpace{};
    std::uint32_t resourceGeneration{};
    std::uint32_t capabilityFlags{};
    std::int32_t adapterHigh{};
    std::uint32_t adapterLow{};
    std::uint32_t width{},height{};
    std::uint32_t colorFormat{},motionFormat{},depthFormat{},controlMaskFormat{};
    volatile LONGLONG inputFenceValue{};
    volatile LONGLONG outputFenceValue{};
    std::uint64_t fenceHandle{};
    std::uint64_t colorHandle{};
    std::uint64_t outputHandle{};
    std::uint64_t motionHandle{};
    std::uint64_t depthHandle{};
    std::uint64_t controlMaskHandle{};
    std::uint64_t stageMask{};
    std::uint32_t failureStage{};
    std::int32_t lastResult{};
    std::uint32_t snippetVersion{};
    std::uint32_t snippetApplicationId{};
    std::uint32_t allowUnsupportedHardware{};
    std::uint32_t realGpuArchitecture{};
    std::uint32_t reportedGpuArchitecture{};
    std::uint32_t architectureCompatibilityActive{};
    std::uint32_t frameReset{};
    std::uint32_t depthInverted{1};
    std::uint32_t useControlMask{};
    std::uint32_t nrAutoMask{1};
    std::uint32_t nrUiCorrection{1};
    std::uint32_t nrStyle{1};
    std::uint32_t nrPreset{};
    std::uint32_t nrPassesRequested{1};
    std::uint32_t nrPassesExecuted{};
    std::uint32_t refinementAvailable{};
    std::int32_t refinementFailureResult{};
    float nrIntensity{0.85f};
    float nrTone{0.45f};
    float nrStructure{1.0f};
    float nrSkinStructure{-1.0f};
    float nrPaperWhite{1.0f};
    float nrTransferStrength{1.0f};
    float nrColorStrength{1.0f};
    float motionScaleX{1.0f};
    float motionScaleY{1.0f};
    wchar_t runtimePath[512]{};
    wchar_t fenceName[160]{};
    wchar_t colorName[160]{};
    wchar_t outputName[160]{};
    wchar_t motionName[160]{};
    wchar_t depthName[160]{};
    wchar_t controlMaskName[160]{};
    wchar_t message[384]{};
};
#pragma pack(pop)
static_assert(sizeof(void*)==4 || sizeof(void*)==8);
}
