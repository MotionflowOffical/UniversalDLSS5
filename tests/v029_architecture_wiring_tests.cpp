#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#ifndef UDLSS_SOURCE_DIR
#error UDLSS_SOURCE_DIR must be defined by CMake
#endif

static std::string readFile(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) { std::cerr << "Could not open " << p << "\n"; std::exit(2); }
    std::ostringstream ss; ss << f.rdbuf(); return ss.str();
}

static bool requireToken(const std::string& text, const char* token, const char* message) {
    if (text.find(token) != std::string::npos) return true;
    std::cerr << message << " (missing: " << token << ")\n";
    return false;
}

static bool forbidToken(const std::string& text, const char* token, const char* message) {
    if (text.find(token) == std::string::npos) return true;
    std::cerr << message << " (found: " << token << ")\n";
    return false;
}

int main() {
    const std::filesystem::path root = UDLSS_SOURCE_DIR;
    const auto ui = readFile(root / "src" / "controller" / "ui.cpp");
    const auto ngx = readFile(root / "src" / "neural" / "ngx_nr.cpp");
    const auto bridge = readFile(root / "src" / "bridge" / "dxgi_hooks.cpp");
    const auto temporalGuides = readFile(root / "src" / "gpu" / "game_temporal_guides.cpp");
    const auto d3d12Tracker = readFile(root / "src" / "gpu" / "d3d12_resource_tracker.cpp");
    const auto d3d11Tracker = readFile(root / "src" / "gpu" / "d3d11_resource_tracker.cpp");
    const auto on12 = readFile(root / "src" / "gpu" / "d3d12_on12.cpp");
    const auto shared = readFile(root / "include" / "udlss" / "shared_control.hpp");
    const auto settings = readFile(root / "include" / "udlss" / "settings.hpp");
    const auto cmake = readFile(root / "CMakeLists.txt");

    bool ok = true;
    ok &= requireToken(ui, "electPrimaryRenderer", "Controller is missing primary-renderer election");
    ok &= requireToken(ui, "setPrimaryRendererPid", "Controller does not publish renderer ownership");
    ok &= requireToken(ui, "maintenanceThread", "Process-tree maintenance is not off the UI timer");
    ok &= requireToken(ui, "std::jthread", "Controller is missing background maintenance thread");
    ok &= requireToken(ui, "for(int i=0;i<40", "Background maintenance cadence is missing");
    ok &= forbidToken(ui, "injectCurrentTree(false)", "UI polling still performs synchronous reinjection");
    ok &= requireToken(bridge, "primaryRendererPid", "Injected bridge does not honor primary-renderer ownership");
    ok &= requireToken(bridge, "GraphicsApi api{GraphicsApi::Unknown}", "SwapCtx does not persist the detected graphics API for suppressed renderer status");
    ok &= requireToken(bridge, "suppressed by primary renderer election", "Helper-renderer suppression diagnostic missing");
    ok &= requireToken(bridge, "safeAttachReady", "Staged safe-attach policy is not wired into Present processing");
    ok &= requireToken(bridge, "installD3D12ResourceTrackingHooks", "D3D12 guide tracking is not lazily installed on the real game device");
    ok &= requireToken(bridge, "installGameTemporalGuideHooks", "Game temporal-guide capture hooks are not installed");
    ok &= requireToken(bridge, "lastGuideHookProbeFrame", "Game guide hook installation is not periodically reprobed for late-loaded Streamline/NGX modules");
    ok &= forbidToken(bridge, "ok&=gpu::installD3D11ResourceTrackingHooks(ctx.Get())", "D3D11 guide hooks are still eagerly installed on the bootstrap device");
    ok &= requireToken(temporalGuides, "slSetTagForFrame", "Streamline frame-tag capture is missing");
    ok &= requireToken(temporalGuides, "slEvaluateFeature", "Streamline local-tag capture is missing");
    ok &= requireToken(temporalGuides, "NVSDK_NGX_Parameter_MotionVectors", "NGX motion-vector capture is missing");
    ok &= requireToken(temporalGuides, "NVSDK_NGX_Parameter_Depth", "NGX depth capture is missing");
    ok &= requireToken(temporalGuides, "NVSDK_NGX_DLSS_Feature_Flags_DepthInverted", "NGX depth convention is not recovered from DLSS create flags");
    ok &= requireToken(temporalGuides, "nativeFormat", "Streamline tagged native resource formats are not preserved");
    ok &= requireToken(temporalGuides, "constantsFresh", "Streamline motion is accepted without fresh scale/convention constants");
    ok &= requireToken(d3d12Tracker, "scoreTemporalDepthCandidate", "D3D12 depth candidate scoring is missing");
    ok &= requireToken(on12, "externalGuide", "D3D12On12 does not forward real game guides into D3D11 processing");
    ok &= requireToken(on12, "bestMotionCandidate", "D3D12 motion candidates are not surfaced for diagnostics");
    ok &= requireToken(on12, "nativeMotionCandidateId", "D3D12 candidate diagnostics are not published to runtime status");
    ok &= requireToken(d3d11Tracker, "scoreTemporalMotionCandidate", "D3D11 motion tracking has not been upgraded for dynamic-resolution guides");

    ok &= requireToken(ngx, "kFrameSlots = 8", "Neural backend is missing expanded command ring");
    ok &= requireToken(ngx, "chooseNeuralSlot", "Neural scheduler policy is not wired into NGX backend");
    ok &= requireToken(ngx, "producerFence12_", "Producer-ready and neural-completion synchronization are not separated");
    ok &= requireToken(ngx, "completionFence12_", "Dedicated D3D12 completion fence is missing");
    ok &= requireToken(ngx, "consumeLatestCompletedOutput", "Async latest-completed output selection is missing");
    ok &= requireToken(ngx, "waitForSlotCompletion", "Synchronized frame-matched neural wait path is missing");
    ok &= requireToken(ngx, "settings.framePacing", "Frame-pacing mode is not wired into NGX backend");
    ok &= requireToken(ngx, "neuralOutputAgeFrames", "Neural output age telemetry is missing");
    ok &= requireToken(ngx, "cacheValid_", "Latest-completed neural output cache is missing");
    ok &= forbidToken(ngx, "D3D12 neural command ring is busy; bypassing this frame instead of blocking the render thread",
                      "Old busy-ring hard bypass path is still present");
    ok &= requireToken(ngx, "settings.nrPasses", "Neural multipass control is not wired");
    ok &= requireToken(ngx, "refinementFeature_", "Reset-only same-frame refinement feature is missing");
    ok &= requireToken(ngx, "requestedPasses=1", "Safe 1x fallback for unavailable refinement is missing");

    ok &= requireToken(ui, "controllerPreferencesPath", "Controller theme is not stored globally");
    ok &= requireToken(ui, "saveControllerPreferences", "Controller theme persistence is not saved");
    ok &= requireToken(ui, "BTN_OPEN_LOGS", "Diagnostics page is missing an attach-log shortcut");
    ok &= requireToken(ui, "openLogsDirectory", "Controller cannot open the safe-attach log directory");
    ok &= requireToken(ui, "controllerTheme", "Application selection does not preserve the global theme");
    ok &= requireToken(settings, "enum class UiTheme", "System/Light/Dark theme setting is missing");
    ok &= requireToken(settings, "enum class FramePacingMode", "Frame-pacing setting is missing");
    ok &= requireToken(settings, "nrPasses", "Neural pass-count setting is missing");
    ok &= requireToken(ui, "D2D1CreateFactory", "Controller is not rendered through Direct2D");
    ok &= requireToken(ui, "DWriteCreateFactory", "Controller is not using DirectWrite text");
    ok &= requireToken(ui, "ID2D1HwndRenderTarget", "Controller is missing its Direct2D HWND render target");
    ok &= requireToken(ui, "FillRoundedRectangle", "Custom rounded-card/button rendering is missing");
    ok &= forbidToken(ui, "WC_TABCONTROLW", "Legacy Win32 tab control is still visible in the controller");
    ok &= forbidToken(ui, "TRACKBAR_CLASSW", "Legacy Win32 trackbar is still visible in the controller");
    ok &= forbidToken(ui, "WC_COMBOBOXW", "Legacy Win32 combo boxes are still visible in the controller");
    ok &= forbidToken(ui, "ES_MULTILINE", "Legacy Win32 diagnostics edit control is still visible");
    ok &= requireToken(ui, "DwmSetWindowAttribute", "Modern title-bar theming is missing");
    ok &= requireToken(ui, "Neural passes", "Neural pass-count UI is missing");
    ok &= requireToken(ui, "Synchronized", "Frame-pacing UI is missing");
    ok &= requireToken(cmake, "d2d1", "Controller is not linked with Direct2D");
    ok &= requireToken(cmake, "dwrite", "Controller is not linked with DirectWrite");

    ok &= requireToken(shared, "queueDepth", "Queue diagnostics are missing from shared status ABI");
    ok &= requireToken(shared, "neuralPassesExecuted", "Multipass diagnostics are missing from shared status ABI");
    ok &= requireToken(shared, "schedulerBackpressureFrames", "Scheduler-pressure diagnostics are missing");
    return ok ? 0 : 1;
}
