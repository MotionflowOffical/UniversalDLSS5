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
    if (!f) {
        std::cerr << "Could not open " << p << "\n";
        std::exit(2);
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

int main() {
    const std::filesystem::path root = UDLSS_SOURCE_DIR;
    const auto cmake = readFile(root / "CMakeLists.txt");
    const auto buildScript = readFile(root / "BUILD_WINDOWS.bat");
    if (cmake.find("project(UniversalDLSS5 VERSION 0.3.1") == std::string::npos ||
        buildScript.find("UniversalDLSS5 v0.3.1") == std::string::npos ||
        buildScript.find("-DUDLSS_WITH_STREAMLINE=ON") == std::string::npos) {
        std::cerr << "v0.2.8 Windows build does not enable the direct in-game Streamline mount/header path\n";
        return 1;
    }

    if (cmake.find("src/gpu/nv_optical_flow.cpp") == std::string::npos) {
        std::cerr << "Bridge target does not compile src/gpu/nv_optical_flow.cpp\n";
        return 1;
    }

    const auto controllerPos = cmake.find("add_executable(UniversalDLSS5 WIN32");
    if (controllerPos == std::string::npos) {
        std::cerr << "Controller target not found\n";
        return 1;
    }
    const auto controllerDefs = cmake.find("target_compile_definitions(UniversalDLSS5 PRIVATE", controllerPos);
    if (controllerDefs == std::string::npos || cmake.find("NOMINMAX", controllerDefs) == std::string::npos) {
        std::cerr << "Controller target does not define NOMINMAX\n";
        return 1;
    }


    if (cmake.find("src/neural/external_host.cpp") == std::string::npos ||
        cmake.find("add_executable(UniversalDLSS5.NRHost") == std::string::npos ||
        cmake.find("src/host/main.cpp") == std::string::npos) {
        std::cerr << "External NR host transport/target is not wired into the Windows build\n";
        return 1;
    }

    if (cmake.find("src/neural/ngx_nr.cpp") == std::string::npos) {
        std::cerr << "Bridge target does not compile the direct NGX DLSS-NR backend\n";
        return 1;
    }

    const auto backendHeader = readFile(root / "src" / "neural" / "backend.hpp");
    const auto d3d11Header = readFile(root / "src" / "gpu" / "d3d11_pipeline.hpp");
    const auto d3d12Source = readFile(root / "src" / "gpu" / "d3d12_on12.cpp");
    if (backendHeader.find("BackendInitContext") == std::string::npos ||
        backendHeader.find("ID3D12CommandQueue") == std::string::npos) {
        std::cerr << "Backend init context does not carry optional native D3D12 execution objects\n";
        return 1;
    }
    if (d3d11Header.find("setNativeD3D12") == std::string::npos ||
        d3d12Source.find("setNativeD3D12") == std::string::npos) {
        std::cerr << "D3D12 swapchain route does not pass its native D3D12 device/queue into the neural backend\n";
        return 1;
    }

    const auto ngxBackend = readFile(root / "src" / "neural" / "ngx_nr.cpp");
    if (ngxBackend.find("NVSDK_NGX_D3D11_CreateFeature") != std::string::npos ||
        ngxBackend.find("NVSDK_NGX_D3D11_EvaluateFeature") != std::string::npos ||
        ngxBackend.find("NVSDK_NGX_D3D11_Init_with_ProjectID") != std::string::npos) {
        std::cerr << "DLSS-NR backend still executes feature 18 through D3D11 NGX\n";
        return 1;
    }
    if (ngxBackend.find("NVSDK_NGX_D3D12_CreateFeature") == std::string::npos ||
        ngxBackend.find("NVSDK_NGX_D3D12_EvaluateFeature") == std::string::npos ||
        ngxBackend.find("NVSDK_NGX_D3D12_Init_with_ProjectID") == std::string::npos) {
        std::cerr << "DLSS-NR backend is missing the native D3D12 NGX route\n";
        return 1;
    }
    if (ngxBackend.find("LoadLibraryExW") == std::string::npos ||
        ngxBackend.find("GetProcAddress") == std::string::npos ||
        ngxBackend.find("NVSDK_NGX_D3D12_AllocateParameters(&params_)") == std::string::npos ||
        ngxBackend.find("\"NVSDK_NGX_D3D12_Init_Ext\"") == std::string::npos ||
        ngxBackend.find("\"NVSDK_NGX_D3D12_PopulateParameters_Impl\"") == std::string::npos ||
        ngxBackend.find("snippetPopulate_") == std::string::npos ||
        ngxBackend.find("snippetCreate_") == std::string::npos ||
        ngxBackend.find("snippetEvaluate_") == std::string::npos) {
        std::cerr << "DLSS-NR backend does not implement the D3D12 NGX core + nvngx_dlssnr snippet route\n";
        return 1;
    }
    if (ngxBackend.find("safeSnippetInit(snippetInit_,kFallbackSnippetApplicationId,runtime_.c_str(),d12_.Get(),nullptr,initException)") == std::string::npos) {
        std::cerr << "DLSS-NR signed snippet init does not match the verified app-id/runtime-path/null-params contract\n";
        return 1;
    }
    if (ngxBackend.find("NVSDK_NGX_GetApplicationId") == std::string::npos ||
        ngxBackend.find("reportedApplicationId") == std::string::npos) {
        std::cerr << "DLSS-NR backend no longer reports the staged snippet application ID for diagnostics\n";
        return 1;
    }
    if (ngxBackend.find("DLSSNRComputeScalingRatioCallback") == std::string::npos ||
        ngxBackend.find("NVSDK_NGX_Parameter_PerfQualityValue") == std::string::npos) {
        std::cerr << "DLSS-NR create contract is missing scaling callback/perf-quality parameters\n";
        return 1;
    }
    if (ngxBackend.find("D3D11_RESOURCE_MISC_SHARED|D3D11_RESOURCE_MISC_SHARED_NTHANDLE") == std::string::npos ||
        ngxBackend.find("D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX") != std::string::npos ||
        ngxBackend.find("OpenSharedHandle") == std::string::npos ||
        ngxBackend.find("OpenSharedFence") == std::string::npos) {
        std::cerr << "D3D11 source path is missing GPU-shared texture/fence interop into D3D12\n";
        return 1;
    }
    if (ngxBackend.find("DLSS.Indicator.Invert.X.Axis") == std::string::npos ||
        ngxBackend.find("DLSS.Indicator.Invert.Y.Axis") == std::string::npos) {
        std::cerr << "DLSS-NR evaluate contract is missing the verified indicator axis parameters\n";
        return 1;
    }
    if (ngxBackend.find("NVSDK_NGX_Parameter_SetF(params_,kPaperWhite") != std::string::npos ||
        ngxBackend.find("NVSDK_NGX_Parameter_SetF(params_,kTransferStrength") != std::string::npos ||
        ngxBackend.find("NVSDK_NGX_Parameter_SetF(params_,kColorStrength") != std::string::npos) {
        std::cerr << "Direct Feature-18 path still sends non-contract generic color parameters\n";
        return 1;
    }
    if (ngxBackend.find("safeNgxCreateFeature") == std::string::npos ||
        ngxBackend.find("safeNgxEvaluateFeature") == std::string::npos) {
        std::cerr << "NGX feature 18 calls are not protected by the crash-to-fallback wrappers\n";
        return 1;
    }
    if (ngxBackend.find("createAttempted_") == std::string::npos) {
        std::cerr << "NGX backend can retry a failing feature-18 creation every frame\n";
        return 1;
    }
    // Feature 18 initialization commands must be submitted and completed before
    // the first EvaluateFeature call. The D3D11 producer signal must also be
    // flushed before the D3D12 queue waits on it.
    if (ngxBackend.find("submitFeatureInitialization") == std::string::npos ||
        ngxBackend.find("ctx11_->Flush()") == std::string::npos) {
        std::cerr << "Feature-18 create/evaluate lifecycle is not explicitly synchronized\n";
        return 1;
    }

    const auto streamlineBackend = readFile(root / "src" / "neural" / "streamline_nr.cpp");
    if (streamlineBackend.find("bool initialize(ID3D11Device*") != std::string::npos) {
        std::cerr << "Optional Streamline fallback still implements the pre-BackendInitContext initialize signature\n";
        return 1;
    }
    if (streamlineBackend.find("D3D11_RESOURCE_MISC_SHARED_NTHANDLE") == std::string::npos ||
        streamlineBackend.find("D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX") == std::string::npos) {
        std::cerr << "In-game Streamline D3D11/D3D12 textures do not use the validated NT-handle sharing flags\n";
        return 1;
    }

    const auto externalHost = readFile(root / "src" / "neural" / "external_host.cpp");
    const auto hostMain = readFile(root / "src" / "host" / "main.cpp");
    if (hostMain.find("NVSDK_NGX_D3D12_Init_with_ProjectID") == std::string::npos ||
        hostMain.find("NVSDK_NGX_D3D12_Init(kGenericCoreApplicationId") != std::string::npos ||
        hostMain.find("kHostProjectId") == std::string::npos ||
        hostMain.find("kHostEngineVersion") == std::string::npos) {
        std::cerr << "External NR host does not initialize NGX core through the documented Project-ID route\n";
        return 1;
    }
    if (hostMain.find("mark(shared,PipelineStage::HostStarted)") == std::string::npos ||
        hostMain.find("mark(shared,PipelineStage::HostConnected)") == std::string::npos ||
        hostMain.find("mark(shared,PipelineStage::NeuralD3D12Ready)") == std::string::npos) {
        std::cerr << "External NR host does not persist startup/device stages into shared diagnostics\n";
        return 1;
    }
    if (externalHost.find("waitForEnqueuedSequence") == std::string::npos ||
        externalHost.find("pendingSeq_") == std::string::npos ||
        externalHost.find("pendingOutputValue_") == std::string::npos ||
        externalHost.find("NRHost warm-up/busy") == std::string::npos ||
        externalHost.find("woke the bridge without enqueueing the output fence signal") != std::string::npos) {
        std::cerr << "External NR host still treats an event wake as proof that the current frame was enqueued\n";
        return 1;
    }

    if (externalHost.find("D3D11_RESOURCE_MISC_SHARED_NTHANDLE") == std::string::npos ||
        externalHost.find("D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX") == std::string::npos ||
        externalHost.find("CreateSharedHandle") == std::string::npos ||
        externalHost.find("DuplicateHandle") == std::string::npos ||
        externalHost.find("UniversalDLSS5.NRHost.exe") == std::string::npos ||
        externalHost.find("WaitForSingleObject(hostProcess") == std::string::npos ||
        externalHost.find("void reset() override { forceResetNext_=true; }") == std::string::npos ||
        hostMain.find("OpenSharedHandle(") == std::string::npos ||
        hostMain.find("OpenSharedHandleByName") != std::string::npos ||
        hostMain.find("NVSDK_NGX_D3D12_CreateFeature") == std::string::npos ||
        hostMain.find("CoreDispatch") == std::string::npos ||
        hostMain.find("signed-snippet") == std::string::npos) {
        std::cerr << "External NR host is missing duplicated-handle GPU transport or core/snippet routing\n";
        return 1;
    }

    const auto forwarderPath = root / "src" / "host" / "nr_forwarder.cpp";
    if (!std::filesystem::is_regular_file(forwarderPath)) {
        std::cerr << "NR caller-compatible forwarder source is missing\n";
        return 1;
    }
    const auto forwarder = readFile(forwarderPath);
    if (cmake.find("UniversalDLSS5.NRForwarder") == std::string::npos ||
        cmake.find("nvngx.dll_UniversalDLSS5_NRForwarder") == std::string::npos ||
        forwarder.find("UdlssNrFwdInitExt") == std::string::npos ||
        forwarder.find("UdlssNrFwdPopulate") == std::string::npos ||
        forwarder.find("UdlssNrFwdCreate") == std::string::npos ||
        forwarder.find("UdlssNrFwdEvaluate") == std::string::npos ||
        forwarder.find("UdlssNrFwdRelease") == std::string::npos ||
        forwarder.find("UdlssNrFwdShutdown") == std::string::npos ||
        forwarder.find("__declspec(noinline)") == std::string::npos ||
        forwarder.find("InterlockedExchange") == std::string::npos) {
        std::cerr << "NR forwarder is not wired with non-tail-call NGX wrappers and caller-compatible output name\n";
        return 1;
    }
    if (hostMain.find("nvngx.dll_UniversalDLSS5_NRForwarder.dll") == std::string::npos ||
        hostMain.find("UdlssNrFwdLoad") == std::string::npos ||
        hostMain.find("UdlssNrFwdInitExt") == std::string::npos ||
        hostMain.find("UdlssNrFwdCreate") == std::string::npos ||
        hostMain.find("UdlssNrFwdEvaluate") == std::string::npos ||
        hostMain.find("MH_CreateHook") == std::string::npos ||
        hostMain.find("NVAPI_ID_GET_ARCH") == std::string::npos ||
        hostMain.find("architectureCompatibilityActive") == std::string::npos) {
        std::cerr << "NRHost does not route through the forwarder and host-local architecture compatibility hook\n";
        return 1;
    }

    const auto externalProtocol = readFile(root / "src" / "neural" / "external_host_protocol.hpp");
    if (externalProtocol.find("controlMaskHandle") == std::string::npos ||
        externalProtocol.find("frameReset") == std::string::npos ||
        externalProtocol.find("nrIntensity") == std::string::npos ||
        externalProtocol.find("nrSkinStructure") == std::string::npos ||
        externalProtocol.find("nrUiCorrection") == std::string::npos) {
        std::cerr << "External NR protocol does not carry control mask, reset, and verified NR tuning values\n";
        return 1;
    }
    if (externalProtocol.find("kAbi=4") == std::string::npos ||
        externalProtocol.find("fenceHandle") == std::string::npos ||
        externalProtocol.find("colorHandle") == std::string::npos ||
        externalProtocol.find("outputHandle") == std::string::npos ||
        externalProtocol.find("motionHandle") == std::string::npos ||
        externalProtocol.find("depthHandle") == std::string::npos ||
        externalProtocol.find("allowUnsupportedHardware") == std::string::npos ||
        externalProtocol.find("realGpuArchitecture") == std::string::npos ||
        externalProtocol.find("reportedGpuArchitecture") == std::string::npos ||
        externalProtocol.find("architectureCompatibilityActive") == std::string::npos) {
        std::cerr << "External NR host protocol does not carry GPU handles/compatibility diagnostics\n";
        return 1;
    }

    if (externalHost.find("settings.motionScaleX*frame.motionScaleX") == std::string::npos ||
        externalHost.find("settings.motionScaleY*frame.motionScaleY") == std::string::npos ||
        externalHost.find("lastPreset_") == std::string::npos) {
        std::cerr << "External NR host does not compose adapter/user MV scale or recreate on preset changes\n";
        return 1;
    }

    if (externalHost.find("frame.controlMask") == std::string::npos ||
        externalHost.find("forceResetNext_") == std::string::npos ||
        hostMain.find("DLSSNR.ControlMask") == std::string::npos ||
        hostMain.find("DLSSNR.SkinStructureStrength") == std::string::npos ||
        hostMain.find("DLSSNR.UICorrection") == std::string::npos ||
        hostMain.find("PaperWhiteScale") == std::string::npos ||
        hostMain.find("TransferStrength") == std::string::npos ||
        hostMain.find("ColorStrength") == std::string::npos) {
        std::cerr << "External NR host does not bind the verified anti-ghosting mask/tuning contract\n";
        return 1;
    }

    const auto runtimeReadme = readFile(root / "runtime" / "README.txt");
    if (runtimeReadme.find("nvngx_dlssnr.dll") == std::string::npos ||
        runtimeReadme.find("sl.interposer.dll") == std::string::npos ||
        runtimeReadme.find("sl.common.dll") == std::string::npos ||
        runtimeReadme.find("sl.dlss_nr.dll") == std::string::npos ||
        runtimeReadme.find("optional Streamline") == std::string::npos) {
        std::cerr << "v0.2.8 runtime instructions do not describe direct signed-feature and optional Streamline-1004 stacks\n";
        return 1;
    }

    if (cmake.find("copy_directory \"${CMAKE_CURRENT_SOURCE_DIR}/runtime\"") == std::string::npos) {
        std::cerr << "Build does not copy the user-supplied runtime directory into the output folder\n";
        return 1;
    }

    const auto sharedHeader = readFile(root / "include" / "udlss" / "shared_control.hpp");
    const auto d3d11Source = readFile(root / "src" / "gpu" / "d3d11_pipeline.cpp");
    if (d3d11Source.find("createInGameNR") == std::string::npos ||
        d3d11Source.find("createExternalHostNR") == std::string::npos ||
        d3d11Source.find("Direct in-game NR failed; external host fallback active") == std::string::npos) {
        std::cerr << "v0.2.8 backend order does not preserve automatic external-host fallback after direct mount failure\n";
        return 1;
    }
    if (d3d11Header.find("nrInput8_") == std::string::npos ||
        d3d11Header.find("nrOutput8_") == std::string::npos ||
        d3d11Source.find("DXGI_FORMAT_R8G8B8A8_UNORM,nrInput8_") == std::string::npos ||
        d3d11Source.find("fr.input=nrInput8_.Get()") == std::string::npos ||
        d3d11Source.find("fr.output=nrOutput8_.Get()") == std::string::npos) {
        std::cerr << "Feature-18 color interop is not using dedicated RGBA8 GPU surfaces\n";
        return 1;
    }

    if (sharedHeader.find("stageMask") == std::string::npos ||
        sharedHeader.find("failureStage") == std::string::npos ||
        sharedHeader.find("neuralActive") == std::string::npos) {
        std::cerr << "RuntimeStatus is missing stage/neural-active diagnostics\n";
        return 1;
    }
    if (sharedHeader.find("nativeMotionCandidateId") == std::string::npos ||
        sharedHeader.find("nativeMotionCandidateScore") == std::string::npos ||
        sharedHeader.find("cameraCurrentValid") == std::string::npos ||
        sharedHeader.find("cameraPreviousValid") == std::string::npos ||
        sharedHeader.find("cameraConfidence") == std::string::npos ||
        sharedHeader.find("temporalReason") == std::string::npos) {
        std::cerr << "RuntimeStatus is missing v0.2.8 guide provenance fields\n";
        return 1;
    }
    if (readFile(root / "src" / "bridge" / "dxgi_hooks.cpp").find("PipelineStage::PresentObserved") == std::string::npos ||
        readFile(root / "src" / "bridge" / "dxgi_hooks.cpp").find("PipelineStage::SourceApiDetected") == std::string::npos ||
        d3d11Source.find("PipelineStage::OutputComposited") == std::string::npos) {
        std::cerr << "Bridge/pipeline stage diagnostics are not wired through the frame path\n";
        return 1;
    }

    const auto processSource = readFile(root / "src" / "controller" / "processes.cpp");
    const auto uiSource = readFile(root / "src" / "controller" / "ui.cpp");
    const auto bridgeSource = readFile(root / "src" / "bridge" / "dxgi_hooks.cpp");
    if (processSource.find("visibleTopLevelPids") == std::string::npos ||
        processSource.find("enumerateApplications") == std::string::npos) {
        std::cerr << "Controller does not build the visible app-centric process list\n";
        return 1;
    }
    if (uiSource.find("drawDiagnosticsPage") == std::string::npos ||
        uiSource.find("BTN_COPY_DIAG") == std::string::npos ||
        uiSource.find("BTN_SAVE_DIAG") == std::string::npos ||
        uiSource.find("copyDiagnosticsToClipboard") == std::string::npos ||
        uiSource.find("saveDiagnosticsToFile") == std::string::npos ||
        uiSource.find("OpenClipboard") == std::string::npos ||
        uiSource.find("WM_SIZE") == std::string::npos ||
        uiSource.find("WM_GETMINMAXINFO") == std::string::npos ||
        uiSource.find("formatPipelineStages") == std::string::npos ||
        uiSource.find("ES_MULTILINE") != std::string::npos) {
        std::cerr << "Controller is missing the resizable custom-rendered/copyable diagnostics page\n";
        return 1;
    }
    if (uiSource.find("drawApplicationPage") == std::string::npos ||
        uiSource.find("HitKind::AppRow") == std::string::npos ||
        uiSource.find("Show all") == std::string::npos ||
        uiSource.find("enumerateApplications") == std::string::npos ||
        uiSource.find("WC_COMBOBOXEXW") != std::string::npos) {
        std::cerr << "Controller app picker is missing custom grouped-process list wiring\n";
        return 1;
    }
    const auto sharedControlSource = readFile(root / "src" / "common" / "shared_control_win.cpp");
    if (sharedControlSource.find("#ifndef WIN32_LEAN_AND_MEAN") == std::string::npos) {
        std::cerr << "shared_control_win.cpp redefines WIN32_LEAN_AND_MEAN when supplied by the target\n";
        return 1;
    }
    if (bridgeSource.find("NVAPI_ID_GET_ARCH") != std::string::npos ||
        bridgeSource.find("nrHostGetArchInfoHook") != std::string::npos) {
        std::cerr << "RTX architecture compatibility leaked into the injected game bridge\n";
        return 1;
    }
    if (uiSource.find("Neural Rendering") == std::string::npos ||
        uiSource.find("Motion & Temporal Guides") == std::string::npos ||
        uiSource.find("Composition") == std::string::npos ||
        uiSource.find("Debug view") == std::string::npos ||
        uiSource.find("Original / NR split") == std::string::npos ||
        uiSource.find("Depth guide") == std::string::npos ||
        uiSource.find("Reset after temporal gap") == std::string::npos ||
        uiSource.find("Load GameGuides adapter") == std::string::npos ||
        uiSource.find("NR intensity") == std::string::npos ||
        uiSource.find("Skin structure") == std::string::npos) {
        std::cerr << "Controller is missing v0.2.6 neural/temporal/composition/debug controls\n";
        return 1;
    }
    if (uiSource.find("Direct in-game") == std::string::npos ||
        uiSource.find("Auto prefers native game motion, then camera+depth, NVOFA, and finally safe zero motion.") == std::string::npos ||
        uiSource.find("Execution location: ") == std::string::npos ||
        uiSource.find("Native motion candidate: ") == std::string::npos ||
        uiSource.find("Camera matrices: ") == std::string::npos) {
        std::cerr << "Controller is missing v0.2.8 direct-mount/motion provenance diagnostics\n";
        return 1;
    }
    if (uiSource.find("Debug view: ") == std::string::npos ||
        uiSource.find("diagnostic visualization") == std::string::npos) {
        std::cerr << "Diagnostics do not identify when a non-final debug visualization is active\n";
        return 1;
    }
    if (uiSource.find("requestNeuralRetry") == std::string::npos ||
        bridgeSource.find("neuralRetryGeneration") == std::string::npos) {
        std::cerr << "Neural retry generation is not wired controller-to-bridge\n";
        return 1;
    }

    const auto guideExtractorHeader = readFile(root / "src" / "gpu" / "d3d11_guide_extractor.hpp");
    const auto guideExtractorSource = readFile(root / "src" / "gpu" / "d3d11_guide_extractor.cpp");
    const auto guideApi = readFile(root / "include" / "udlss" / "game_guides_api.hpp");
    if (cmake.find("src/gpu/d3d11_guide_extractor.cpp") == std::string::npos ||
        guideExtractorSource.find("OMGetRenderTargets") == std::string::npos ||
        guideExtractorSource.find("UniversalDLSS5.GameGuides.dll") == std::string::npos ||
        guideExtractorSource.find("LoadLibraryW") == std::string::npos ||
        guideExtractorSource.find("GetProcAddress") == std::string::npos ||
        guideExtractorSource.find("D3D11_RESOURCE_MISC") != std::string::npos ||
        guideApi.find("UdlssGameGuides_GetFrameV1") == std::string::npos) {
        std::cerr << "D3D11 game-guide extractor/explicit adapter ABI is not wired conservatively\n";
        return 1;
    }

    const auto flowShader = readFile(root / "shaders" / "flow.hlsl");
    const auto motionShader = readFile(root / "shaders" / "motion.hlsl");
    const auto maskShader = readFile(root / "shaders" / "mask.hlsl");
    const auto postShader = readFile(root / "shaders" / "post.hlsl");
    if (postShader.find("Texture2D<float2> Motion") == std::string::npos ||
        postShader.find("DebugView==2") == std::string::npos ||
        postShader.find("DebugView==7") == std::string::npos ||
        postShader.find("DebugView==8") == std::string::npos) {
        std::cerr << "GPU debug views are not wired into the post shader\n";
        return 1;
    }

    if (flowShader.find("length2") == std::string::npos ||
        flowShader.find("best==0?1:conf") != std::string::npos ||
        flowShader.find("abs(e-best)<=") == std::string::npos) {
        std::cerr << "HLSL flow matcher does not prefer zero/short motion on ambiguous equal-error matches\n";
        return 1;
    }
    if (motionShader.find("ConfidenceThreshold") == std::string::npos ||
        motionShader.find("StaticDeadzone") == std::string::npos ||
        motionShader.find("f.z<ConfidenceThreshold") == std::string::npos) {
        std::cerr << "Motion shader does not reject low-confidence/static optical flow\n";
        return 1;
    }
    if (maskShader.find("nrApplication") == std::string::npos ||
        maskShader.find("1.0-unreliable") == std::string::npos ||
        postShader.find("m.a") == std::string::npos) {
        std::cerr << "Control mask polarity/protected-pixel channel is not anti-ghosting safe\n";
        return 1;
    }
    if (maskShader.find("prevPos") == std::string::npos ||
        maskShader.find("Texture2D<float2> Motion") == std::string::npos ||
        maskShader.find("Motion.Load(int3(p,0))") == std::string::npos ||
        maskShader.find("Prev.Load(int3(prevPos,0))") == std::string::npos ||
        maskShader.find("Prev.Load(int3(p,0))") != std::string::npos ||
        maskShader.find("inBounds") == std::string::npos) {
        std::cerr << "Control mask does not compare against the motion-reprojected previous frame\n";
        return 1;
    }

    if (d3d11Source.find("GuideProbeResult guide") == std::string::npos ||
        d3d11Source.find("guideExtractor_.probe") == std::string::npos ||
        d3d11Source.find("guideExtractor_.prepareDepth") == std::string::npos ||
        d3d11Source.find("Synthetic far depth") == std::string::npos ||
        d3d11Source.find("nrControlMaskTex_") == std::string::npos ||
        d3d11Source.find("fr.controlMask=explicitControlMask?nrControlMaskTex_.Get():nullptr") == std::string::npos ||
        d3d11Source.find("guide.cameraCut") == std::string::npos) {
        std::cerr << "D3D11 pipeline does not consume the extracted guides/control mask safely\n";
        return 1;
    }

    std::cout << "Windows build wiring policy OK\n";
    return 0;
}
