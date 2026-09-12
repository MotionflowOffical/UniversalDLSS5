#include "backend.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <windows.h>
#include <dxgi1_2.h>
#include <d3d11_4.h>
#include <wrl/client.h>
#include "udlss/ngx_failure_policy.hpp"
#include "udlss/neural_route_policy.hpp"

#ifdef UDLSS_WITH_NGX_NR
#include <nvsdk_ngx.h>
#include <nvsdk_ngx_params.h>
#endif

using Microsoft::WRL::ComPtr;

namespace udlss::neural {

#ifdef UDLSS_WITH_NGX_NR
namespace {
constexpr const char* kProjectId = "a4df2ee7-cd2a-47ba-9c83-1d7f7c0a5b51";
constexpr const char* kEngineVersion = "0.2.8";
constexpr NVSDK_NGX_Feature kFeatureDLSSNR = NVSDK_NGX_Feature_Reserved18;
constexpr std::size_t kFrameSlots = 3;
constexpr unsigned long long kFallbackSnippetApplicationId = 0x0876232Cull;

// Feature-18 names are isolated here until NVIDIA publishes a dedicated NR header.
constexpr const char* kWidth = "DLSSNR.Width";
constexpr const char* kHeight = "DLSSNR.Height";
constexpr const char* kInputWidth = "DLSSNR.InputWidth";
constexpr const char* kInputHeight = "DLSSNR.InputHeight";
constexpr const char* kOutputWidth = "DLSSNR.OutputWidth";
constexpr const char* kOutputHeight = "DLSSNR.OutputHeight";
constexpr const char* kOutputWidth2 = "DLSSNR.Output.Width";
constexpr const char* kOutputHeight2 = "DLSSNR.Output.Height";
constexpr const char* kUpscaling = "DLSSNR.Upscaling";
constexpr const char* kScale = "DLSSNR.Scale";
constexpr const char* kScalingRatio = "DLSSNR.ScalingRatio";
constexpr const char* kScalingRatioCallbackParameter = "DLSSNRComputeScalingRatioCallback";
constexpr const char* kRenderPreset = "DLSSNR.Hint.Render.Preset";
constexpr const char* kColor = "DLSSNR.Color";
constexpr const char* kOutput = "DLSSNR.Output";
constexpr const char* kMVec = "DLSSNR.MVec";
constexpr const char* kDepth = "DLSSNR.Depth";
constexpr const char* kControlMask = "DLSSNR.ControlMask";
constexpr const char* kMVecScaleX = "DLSSNR.MVecScaleX";
constexpr const char* kMVecScaleY = "DLSSNR.MVecScaleY";
constexpr const char* kDepthInverted = "DLSSNR.DepthInverted";
constexpr const char* kEnabled = "DLSSNR.Enabled";
constexpr const char* kReset = "DLSSNR.Reset";
constexpr const char* kStyle = "DLSSNR.Style";
constexpr const char* kIntensity = "DLSSNR.Intensity";
constexpr const char* kLocalTone = "DLSSNR.LocalToneStrength";
constexpr const char* kLocalStructure = "DLSSNR.LocalStructureStrength";
constexpr const char* kSkinStructure = "DLSSNR.SkinStructureStrength";
constexpr const char* kUseAutoMask = "DLSSNR.UseAutoMask";
constexpr const char* kUiCorrection = "DLSSNR.UICorrection";
constexpr const char* kPaperWhite = "PaperWhiteScale";
constexpr const char* kTransferStrength = "TransferStrength";
constexpr const char* kColorStrength = "ColorStrength";
constexpr const char* kColorSubrectX = "DLSSNR.ColorSubrectBaseX";
constexpr const char* kColorSubrectY = "DLSSNR.ColorSubrectBaseY";
constexpr const char* kColorSubrectW = "DLSSNR.ColorSubrectWidth";
constexpr const char* kColorSubrectH = "DLSSNR.ColorSubrectHeight";
constexpr const char* kOutputSubrectX = "DLSSNR.OutputSubrectBaseX";
constexpr const char* kOutputSubrectY = "DLSSNR.OutputSubrectBaseY";
constexpr const char* kOutputSubrectW = "DLSSNR.OutputSubrectWidth";
constexpr const char* kOutputSubrectH = "DLSSNR.OutputSubrectHeight";
constexpr const char* kMVecSubrectX = "DLSSNR.MVecSubrectBaseX";
constexpr const char* kMVecSubrectY = "DLSSNR.MVecSubrectBaseY";
constexpr const char* kMVecSubrectW = "DLSSNR.MVecSubrectWidth";
constexpr const char* kMVecSubrectH = "DLSSNR.MVecSubrectHeight";
constexpr const char* kDepthSubrectX = "DLSSNR.DepthSubrectBaseX";
constexpr const char* kDepthSubrectY = "DLSSNR.DepthSubrectBaseY";
constexpr const char* kDepthSubrectW = "DLSSNR.DepthSubrectWidth";
constexpr const char* kDepthSubrectH = "DLSSNR.DepthSubrectHeight";
constexpr const char* kControlSubrectW = "DLSSNR.ControlMaskSubrectWidth";
constexpr const char* kControlSubrectH = "DLSSNR.ControlMaskSubrectHeight";

std::filesystem::path ngxLogDirectory() {
    wchar_t local[MAX_PATH]{};
    const DWORD n=GetEnvironmentVariableW(L"LOCALAPPDATA",local,MAX_PATH);
    std::filesystem::path out=(n && n<MAX_PATH)?std::filesystem::path(local):std::filesystem::temp_directory_path();
    out/=L"UniversalDLSS5";
    out/=L"ngx";
    std::error_code ec;
    std::filesystem::create_directories(out,ec);
    return out;
}

using SnippetInitExtFn = NVSDK_NGX_Result (NVSDK_CONV *)(unsigned long long,const wchar_t*,ID3D12Device*,NVSDK_NGX_Version,const NVSDK_NGX_Parameter*);
using SnippetPopulateFn = NVSDK_NGX_Result (NVSDK_CONV *)(NVSDK_NGX_Parameter*);
using SnippetCreateFn = NVSDK_NGX_Result (NVSDK_CONV *)(ID3D12GraphicsCommandList*,NVSDK_NGX_Feature,NVSDK_NGX_Parameter*,NVSDK_NGX_Handle**);
using SnippetEvaluateFn = NVSDK_NGX_Result (NVSDK_CONV *)(ID3D12GraphicsCommandList*,const NVSDK_NGX_Handle*,const NVSDK_NGX_Parameter*,PFN_NVSDK_NGX_ProgressCallback);
using SnippetReleaseFn = NVSDK_NGX_Result (NVSDK_CONV *)(NVSDK_NGX_Handle*);
using SnippetShutdownFn = NVSDK_NGX_Result (NVSDK_CONV *)(ID3D12Device*);
using SnippetGetApplicationIdFn = std::uint32_t (NVSDK_CONV *)();
using SnippetGetVersionFn = std::uint32_t (NVSDK_CONV *)();
using ForwarderLoadFn = int (WINAPI *)(const wchar_t*);
using ForwarderUnloadFn = void (WINAPI *)();

std::uint32_t safeSnippetApplicationId(SnippetGetApplicationIdFn fn,DWORD& exceptionCode) {
    exceptionCode=0;
    if(!fn) return 0;
#if defined(_MSC_VER)
    __try { return fn(); }
    __except(EXCEPTION_EXECUTE_HANDLER) { exceptionCode=GetExceptionCode(); return 0; }
#else
    return fn();
#endif
}

std::uint32_t safeSnippetVersion(SnippetGetVersionFn fn) {
    if(!fn) return 0;
#if defined(_MSC_VER)
    __try { return fn(); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
#else
    return fn();
#endif
}

NVSDK_NGX_Result NVSDK_CONV scalingRatioCallback(NVSDK_NGX_Parameter* params) noexcept {
    if(!params) return NVSDK_NGX_Result_FAIL_InvalidParameter;
#if defined(_MSC_VER)
    __try {
        NVSDK_NGX_Parameter_SetF(params,kScalingRatio,1.0f);
        return NVSDK_NGX_Result_Success;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return NVSDK_NGX_Result_FAIL_PlatformError;
    }
#else
    NVSDK_NGX_Parameter_SetF(params,kScalingRatio,1.0f);
    return NVSDK_NGX_Result_Success;
#endif
}

NVSDK_NGX_Result safeSnippetInit(SnippetInitExtFn fn,unsigned long long applicationId,const wchar_t* dataPath,ID3D12Device* device,const NVSDK_NGX_Parameter* params,DWORD& exceptionCode) {
    exceptionCode=0;
#if defined(_MSC_VER)
    __try { return fn(applicationId,dataPath,device,NVSDK_NGX_Version_API,params); }
    __except(EXCEPTION_EXECUTE_HANDLER) { exceptionCode=GetExceptionCode(); return NVSDK_NGX_Result_Fail; }
#else
    return fn(applicationId,dataPath,device,NVSDK_NGX_Version_API,params);
#endif
}

NVSDK_NGX_Result safeSnippetPopulate(SnippetPopulateFn fn,NVSDK_NGX_Parameter* params,DWORD& exceptionCode) {
    exceptionCode=0;
#if defined(_MSC_VER)
    __try { return fn(params); }
    __except(EXCEPTION_EXECUTE_HANDLER) { exceptionCode=GetExceptionCode(); return NVSDK_NGX_Result_Fail; }
#else
    return fn(params);
#endif
}

NVSDK_NGX_Result safeNgxCreateFeature(SnippetCreateFn fn,ID3D12GraphicsCommandList* list,NVSDK_NGX_Parameter* params,NVSDK_NGX_Handle** handle,DWORD& exceptionCode) {
    exceptionCode=0;
#if defined(_MSC_VER)
    __try { return fn(list,kFeatureDLSSNR,params,handle); }
    __except(EXCEPTION_EXECUTE_HANDLER) { exceptionCode=GetExceptionCode(); return NVSDK_NGX_Result_Fail; }
#else
    return fn(list,kFeatureDLSSNR,params,handle);
#endif
}

NVSDK_NGX_Result safeNgxEvaluateFeature(SnippetEvaluateFn fn,ID3D12GraphicsCommandList* list,const NVSDK_NGX_Handle* handle,const NVSDK_NGX_Parameter* params,DWORD& exceptionCode) {
    exceptionCode=0;
#if defined(_MSC_VER)
    __try { return fn(list,handle,params,nullptr); }
    __except(EXCEPTION_EXECUTE_HANDLER) { exceptionCode=GetExceptionCode(); return NVSDK_NGX_Result_Fail; }
#else
    return fn(list,handle,params,nullptr);
#endif
}

NVSDK_NGX_Result safeSnippetRelease(SnippetReleaseFn fn,NVSDK_NGX_Handle* handle) {
    if(!fn || !handle) return NVSDK_NGX_Result_Success;
#if defined(_MSC_VER)
    __try { return fn(handle); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return NVSDK_NGX_Result_Fail; }
#else
    return fn(handle);
#endif
}

NVSDK_NGX_Result safeSnippetShutdown(SnippetShutdownFn fn,ID3D12Device* device) {
    if(!fn || !device) return NVSDK_NGX_Result_Success;
#if defined(_MSC_VER)
    __try { return fn(device); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return NVSDK_NGX_Result_Fail; }
#else
    return fn(device);
#endif
}

void setFailure(RuntimeStatus& status,PipelineStage stage,std::int32_t result,const std::wstring& message) {
    status.failureStage=stage;
    status.lastResult=result;
    wcsncpy_s(status.message,message.c_str(),_TRUNCATE);
}

struct SharedTexture {
    ComPtr<ID3D11Texture2D> d11;
    ComPtr<ID3D12Resource> d12;
    UINT width{};
    UINT height{};
    DXGI_FORMAT format{DXGI_FORMAT_UNKNOWN};
    void reset(){ d12.Reset(); d11.Reset(); width=height=0; format=DXGI_FORMAT_UNKNOWN; }
};

struct FrameSlot {
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    std::uint64_t completionValue{};
};
}

class NgxNR final : public Backend {
public:
    ~NgxNR() override { shutdown(); }

    bool initialize(const BackendInitContext& init,const std::wstring& runtime,const Settings&,RuntimeStatus& status) override {
        shutdown();
        status.neuralApi=NeuralExecutionApi::D3D12;
        if(!init.d3d11Device || !init.d3d11Context) {
            setFailure(status,PipelineStage::NeuralD3D12InitFailed,0,L"NGX DLSS-NR: missing D3D11 guide device/context");
            return false;
        }
        const auto dll=std::filesystem::path(runtime)/L"nvngx_dlssnr.dll";
        std::error_code ec;
        if(!std::filesystem::is_regular_file(dll,ec)) {
            setFailure(status,PipelineStage::NgxCoreInitFailed,0,L"nvngx_dlssnr.dll not found in selected runtime folder");
            return false;
        }

        d11_=init.d3d11Device;
        ctx11_=init.d3d11Context;
        runtime_=runtime;
        if(FAILED(d11_.As(&d11v5_)) || FAILED(ctx11_.As(&ctx11v4_))) {
            setFailure(status,PipelineStage::NeuralD3D12InitFailed,0,L"D3D11/D3D12 GPU fence interop requires ID3D11Device5/ID3D11DeviceContext4");
            return false;
        }

        if(init.d3d12Device && init.d3d12Queue) {
            d12_=init.d3d12Device;
            queue12_=init.d3d12Queue;
            if(queue12_->GetDesc().Type!=D3D12_COMMAND_LIST_TYPE_DIRECT) {
                setFailure(status,PipelineStage::NeuralD3D12InitFailed,0,L"Captured D3D12 queue is not a DIRECT queue");
                return false;
            }
        } else if(!createSameAdapterD3D12(status)) {
            return false;
        }

        if(!createSharedFence(status) || !createCommandSlots(status)) return false;
        markPipelineStage(status.stageMask,PipelineStage::NeuralD3D12Ready);

        const wchar_t* searchPaths[]={runtime_.c_str()};
        NVSDK_NGX_FeatureCommonInfo featureInfo{};
        featureInfo.PathListInfo.Path=searchPaths;
        featureInfo.PathListInfo.Length=1;
        const auto logDir=ngxLogDirectory();
        const auto result=NVSDK_NGX_D3D12_Init_with_ProjectID(
            kProjectId,NVSDK_NGX_ENGINE_TYPE_CUSTOM,kEngineVersion,
            logDir.c_str(),d12_.Get(),&featureInfo,NVSDK_NGX_Version_API);
        if(NVSDK_NGX_FAILED(result)) {
            setFailure(status,PipelineStage::NgxCoreInitFailed,static_cast<int>(result),
                       L"NGX D3D12 core init failed for DLSS-NR (0x"+hex8(result)+L")");
            shutdown();
            return false;
        }
        ngxInitialized_=true;
        markPipelineStage(status.stageMask,PipelineStage::NgxCoreInitialized);

        auto paramResult=NVSDK_NGX_D3D12_GetCapabilityParameters(&params_);
        if(NVSDK_NGX_FAILED(paramResult) || !params_) {
            params_=nullptr;
            paramResult=NVSDK_NGX_D3D12_AllocateParameters(&params_);
        }
        if(NVSDK_NGX_FAILED(paramResult) || !params_) {
            setFailure(status,PipelineStage::NgxCoreInitFailed,static_cast<int>(paramResult),L"NGX D3D12 capability/parameter allocation failed");
            shutdown();
            return false;
        }

        const auto forwarderPath=std::filesystem::path(runtime_).parent_path()/L"nvngx.dll_UniversalDLSS5_NRForwarder.dll";
        if(std::filesystem::is_regular_file(forwarderPath,ec)) {
            snippetModule_=LoadLibraryExW(forwarderPath.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
            if(snippetModule_) {
                auto load=reinterpret_cast<ForwarderLoadFn>(GetProcAddress(snippetModule_,"UdlssNrFwdLoad"));
                forwarderUnload_=reinterpret_cast<ForwarderUnloadFn>(GetProcAddress(snippetModule_,"UdlssNrFwdUnload"));
                if(load && load(dll.c_str())==0) {
                    usingForwarder_=true;
                    snippetInit_=reinterpret_cast<SnippetInitExtFn>(GetProcAddress(snippetModule_,"UdlssNrFwdInitExt"));
                    snippetPopulate_=reinterpret_cast<SnippetPopulateFn>(GetProcAddress(snippetModule_,"UdlssNrFwdPopulate"));
                    snippetGetApplicationId_=reinterpret_cast<SnippetGetApplicationIdFn>(GetProcAddress(snippetModule_,"UdlssNrFwdGetApplicationId"));
                    snippetGetVersion_=reinterpret_cast<SnippetGetVersionFn>(GetProcAddress(snippetModule_,"UdlssNrFwdGetSnippetVersion"));
                    snippetCreate_=reinterpret_cast<SnippetCreateFn>(GetProcAddress(snippetModule_,"UdlssNrFwdCreate"));
                    snippetEvaluate_=reinterpret_cast<SnippetEvaluateFn>(GetProcAddress(snippetModule_,"UdlssNrFwdEvaluate"));
                    snippetRelease_=reinterpret_cast<SnippetReleaseFn>(GetProcAddress(snippetModule_,"UdlssNrFwdRelease"));
                    snippetShutdown_=reinterpret_cast<SnippetShutdownFn>(GetProcAddress(snippetModule_,"UdlssNrFwdShutdown"));
                }
            }
        }
        if(!usingForwarder_) {
            forwarderUnload_=nullptr;
            if(snippetModule_) { FreeLibrary(snippetModule_); snippetModule_=nullptr; }
            snippetModule_=LoadLibraryExW(dll.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
            if(!snippetModule_) {
                setFailure(status,PipelineStage::SnippetLoadFailed,static_cast<int>(GetLastError()),L"Could not load nvngx_dlssnr.dll from the selected runtime folder");
                shutdown();
                return false;
            }
            snippetInit_=reinterpret_cast<SnippetInitExtFn>(GetProcAddress(snippetModule_,"NVSDK_NGX_D3D12_Init_Ext"));
            snippetPopulate_=reinterpret_cast<SnippetPopulateFn>(GetProcAddress(snippetModule_,"NVSDK_NGX_D3D12_PopulateParameters_Impl"));
            snippetGetApplicationId_=reinterpret_cast<SnippetGetApplicationIdFn>(GetProcAddress(snippetModule_,"NVSDK_NGX_GetApplicationId"));
            snippetGetVersion_=reinterpret_cast<SnippetGetVersionFn>(GetProcAddress(snippetModule_,"NVSDK_NGX_GetSnippetVersion"));
            snippetCreate_=reinterpret_cast<SnippetCreateFn>(GetProcAddress(snippetModule_,"NVSDK_NGX_D3D12_CreateFeature"));
            snippetEvaluate_=reinterpret_cast<SnippetEvaluateFn>(GetProcAddress(snippetModule_,"NVSDK_NGX_D3D12_EvaluateFeature"));
            snippetRelease_=reinterpret_cast<SnippetReleaseFn>(GetProcAddress(snippetModule_,"NVSDK_NGX_D3D12_ReleaseFeature"));
            snippetShutdown_=reinterpret_cast<SnippetShutdownFn>(GetProcAddress(snippetModule_,"NVSDK_NGX_D3D12_Shutdown1"));
        }
        if(!snippetInit_ || !snippetPopulate_ || !snippetCreate_ || !snippetEvaluate_ || !snippetRelease_ || !snippetShutdown_) {
            setFailure(status,PipelineStage::SnippetLoadFailed,0,L"nvngx_dlssnr.dll is missing one or more required D3D12 NGX exports");
            shutdown();
            return false;
        }
        markPipelineStage(status.stageMask,PipelineStage::SnippetLoaded);

        snippetApplicationId_=kFallbackSnippetApplicationId;
        DWORD applicationIdException=0;
        const auto reportedApplicationId=safeSnippetApplicationId(snippetGetApplicationId_,applicationIdException);
        if(reportedApplicationId!=0) snippetApplicationId_=reportedApplicationId;
        snippetVersion_=safeSnippetVersion(snippetGetVersion_);

        DWORD initException=0;
        const auto snippetResult=safeSnippetInit(snippetInit_,snippetApplicationId_,logDir.c_str(),d12_.Get(),params_,initException);
        if(initException) {
            rememberNgxFailure(createFailure_,NgxFailureStage::Initialize,0,initException);
            setFailure(status,PipelineStage::SnippetInitFailed,static_cast<int>(initException),formatNgxFailure(createFailure_));
            shutdown();
            return false;
        }
        if(NVSDK_NGX_FAILED(snippetResult)) {
            rememberNgxFailure(createFailure_,NgxFailureStage::Initialize,static_cast<int>(snippetResult),0);
            auto message=formatNgxFailure(createFailure_);
            if(static_cast<std::uint32_t>(snippetResult)==0xBAD00002u)
                message+=usingForwarder_?L"; caller-compatible forwarder was loaded but the runtime still rejected initialization":L"; direct caller rejected; forwarder was unavailable";
            setFailure(status,PipelineStage::SnippetInitFailed,static_cast<int>(snippetResult),message);
            shutdown();
            return false;
        }
        // Init_Ext established snippet-owned state; remember that before population so shutdown can unwind it on any later failure.
        snippetInitialized_=true;
        DWORD populateException=0;
        const auto populateResult=safeSnippetPopulate(snippetPopulate_,params_,populateException);
        if(populateException) {
            rememberNgxFailure(createFailure_,NgxFailureStage::Initialize,0,populateException);
            setFailure(status,PipelineStage::SnippetInitFailed,static_cast<int>(populateException),L"DLSS-NR snippet parameter population raised an exception: "+formatNgxFailure(createFailure_));
            shutdown();
            return false;
        }
        if(NVSDK_NGX_FAILED(populateResult)) {
            rememberNgxFailure(createFailure_,NgxFailureStage::Initialize,static_cast<int>(populateResult),0);
            setFailure(status,PipelineStage::SnippetInitFailed,static_cast<int>(populateResult),L"DLSS-NR snippet parameter population failed: "+formatNgxFailure(createFailure_));
            shutdown();
            return false;
        }
        markPipelineStage(status.stageMask,PipelineStage::SnippetInitialized);

        initialized_=true;
        reset_=true;
        createAttempted_=false;
        clearNgxFailure(createFailure_);
        status.requiredTagCount=4;
        status.failureStage=PipelineStage::None;
        swprintf_s(status.message,
                   L"NGX core + nvngx_dlssnr D3D12 snippet initialized (appId=0x%08X, snippet=0x%X%s); feature 18 will be created on the first frame",
                   static_cast<unsigned>(snippetApplicationId_),snippetVersion_,
                   (reportedApplicationId==0 ? L", fallback app id" : L""));
        return true;
    }

    bool evaluate(ID3D11DeviceContext*,const FrameResources& frame,const Settings& settings,RuntimeStatus& status) override {
        status.neuralApi=NeuralExecutionApi::D3D12;
        if(!initialized_ || !params_ || !frame.input || !frame.output || !frame.motion || !frame.depth) return false;
        if(!ensureSharedResources(frame,status)) return false;

        FrameSlot& slot=slots_[frameIndex_%kFrameSlots];
        ++frameIndex_;
        if(slot.completionValue && fence12_->GetCompletedValue()<slot.completionValue) {
            setFailure(status,PipelineStage::FeatureEvaluateFailed,0,L"D3D12 neural command ring is busy; bypassing this frame instead of blocking the render thread");
            return false;
        }
        if(FAILED(slot.allocator->Reset()) || FAILED(slot.list->Reset(slot.allocator.Get(),nullptr))) {
            setFailure(status,PipelineStage::FeatureEvaluateFailed,0,L"D3D12 neural command allocator/list reset failed");
            return false;
        }

        // All guide pixels stay GPU-resident. D3D11 writes shared textures, then signals a GPU fence.
        ctx11_->CopyResource(input_.d11.Get(),frame.input);
        ctx11_->CopyResource(motion_.d11.Get(),frame.motion);
        ctx11_->CopyResource(depth_.d11.Get(),frame.depth);
        if(frame.controlMask && controlMask_.d11) ctx11_->CopyResource(controlMask_.d11.Get(),frame.controlMask);
        const std::uint64_t readyValue=++fenceValue_;
        if(FAILED(ctx11v4_->Signal(fence11_.Get(),readyValue))) {
            setFailure(status,PipelineStage::FeatureEvaluateFailed,0,L"D3D11 shared-fence signal failed");
            return false;
        }
        // ID3D11DeviceContext4::Signal is ordered on the immediate context,
        // but explicitly flush the producer batch before making another API
        // queue wait on that fence. This mirrors the known-good Feature-18
        // D3D11->D3D12 handoff and avoids a wait on commands still buffered
        // in the D3D11 runtime.
        ctx11_->Flush();
        if(FAILED(queue12_->Wait(fence12_.Get(),readyValue))) {
            setFailure(status,PipelineStage::FeatureEvaluateFailed,0,L"D3D12 queue wait on D3D11 guide fence failed");
            return false;
        }

        transitionForNgx(slot.list.Get(),true);
        const bool creatingFeature = feature_ == nullptr;
        if(!ensureFeature(slot.list.Get(),frame,settings,status)) {
            slot.list->Close();
            return false;
        }
        if(creatingFeature) {
            // CreateFeature records one-time initialization work. A known-good
            // Feature-18 integration submits and completes that work before the
            // first EvaluateFeature call rather than recording both into the
            // same never-yet-executed command list. The resources remain in
            // their NGX read/UAV states across this one-time boundary.
            if(!submitFeatureInitialization(slot,status)) return false;
            if(FAILED(slot.allocator->Reset()) || FAILED(slot.list->Reset(slot.allocator.Get(),nullptr))) {
                setFailure(status,PipelineStage::FeatureEvaluateFailed,0,L"D3D12 command list reset after feature initialization failed");
                return false;
            }
        }

        NVSDK_NGX_Parameter_SetD3d12Resource(params_,kColor,input_.d12.Get());
        NVSDK_NGX_Parameter_SetD3d12Resource(params_,kOutput,output_.d12.Get());
        NVSDK_NGX_Parameter_SetD3d12Resource(params_,kMVec,motion_.d12.Get());
        NVSDK_NGX_Parameter_SetD3d12Resource(params_,kDepth,depth_.d12.Get());
        NVSDK_NGX_Parameter_SetD3d12Resource(params_,kControlMask,(settings.useControlMask&&frame.controlMask)?controlMask_.d12.Get():nullptr);
        NVSDK_NGX_Parameter_SetF(params_,kMVecScaleX,frame.motionScaleX);
        NVSDK_NGX_Parameter_SetF(params_,kMVecScaleY,frame.motionScaleY);
        NVSDK_NGX_Parameter_SetUI(params_,kDepthInverted,frame.depthInverted?1u:0u);
        NVSDK_NGX_Parameter_SetUI(params_,kEnabled,1u);
        NVSDK_NGX_Parameter_SetUI(params_,kReset,(reset_||frame.resetHistory)?1u:0u);
        NVSDK_NGX_Parameter_SetUI(params_,kStyle,settings.nrStyle);
        NVSDK_NGX_Parameter_SetF(params_,kIntensity,settings.nrIntensity);
        NVSDK_NGX_Parameter_SetF(params_,kLocalTone,settings.nrTone);
        NVSDK_NGX_Parameter_SetF(params_,kLocalStructure,settings.nrStructure);
        NVSDK_NGX_Parameter_SetF(params_,kSkinStructure,settings.nrSkinStructure);
        NVSDK_NGX_Parameter_SetUI(params_,kUseAutoMask,settings.nrAutoMask?1u:0u);
        NVSDK_NGX_Parameter_SetUI(params_,kUiCorrection,settings.nrUiCorrection?1u:0u);
        NVSDK_NGX_Parameter_SetF(params_,kPaperWhite,settings.nrPaperWhite);
        NVSDK_NGX_Parameter_SetF(params_,kTransferStrength,settings.nrTransferStrength);
        NVSDK_NGX_Parameter_SetF(params_,kColorStrength,settings.nrColorStrength);
        setSubrectParameters(frame.width,frame.height);

        DWORD exceptionCode=0;
        const auto result=safeNgxEvaluateFeature(snippetEvaluate_,slot.list.Get(),feature_,params_,exceptionCode);
        if(exceptionCode) {
            rememberNgxFailure(createFailure_,NgxFailureStage::EvaluateFeature,0,exceptionCode);
            setFailure(status,PipelineStage::FeatureEvaluateFailed,static_cast<int>(exceptionCode),formatNgxFailure(createFailure_));
            slot.list->Close();
            return false;
        }
        if(NVSDK_NGX_FAILED(result)) {
            rememberNgxFailure(createFailure_,NgxFailureStage::EvaluateFeature,static_cast<int>(result),0);
            setFailure(status,PipelineStage::FeatureEvaluateFailed,static_cast<int>(result),formatNgxFailure(createFailure_));
            slot.list->Close();
            return false;
        }
        markPipelineStage(status.stageMask,PipelineStage::FeatureEvaluated);
        reset_=false;

        transitionForNgx(slot.list.Get(),false);
        if(FAILED(slot.list->Close())) {
            setFailure(status,PipelineStage::FeatureEvaluateFailed,0,L"D3D12 neural command list close failed");
            return false;
        }
        ID3D12CommandList* lists[]={slot.list.Get()};
        queue12_->ExecuteCommandLists(1,lists);
        const std::uint64_t doneValue=++fenceValue_;
        if(FAILED(queue12_->Signal(fence12_.Get(),doneValue))) {
            setFailure(status,PipelineStage::FeatureEvaluateFailed,0,L"D3D12 neural completion signal failed");
            return false;
        }
        slot.completionValue=doneValue;
        if(FAILED(ctx11v4_->Wait(fence11_.Get(),doneValue))) {
            setFailure(status,PipelineStage::FeatureEvaluateFailed,0,L"D3D11 wait for D3D12 neural output failed");
            return false;
        }
        ctx11_->CopyResource(frame.output,output_.d11.Get());

        status.failureStage=PipelineStage::None;
        status.lastResult=static_cast<int>(result);
        status.requiredTagCount=4;
        wcscpy_s(status.message,usingForwarder_?L"Direct in-game DLSS 5 active through signed feature 18 forwarder":L"Direct in-game DLSS 5 active through direct feature 18");
        return true;
    }

    void reset() override { reset_=true; }
    const wchar_t* name() const override { return L"NGX DLSS 5 NR (D3D12)"; }

private:
    bool submitFeatureInitialization(FrameSlot& slot,RuntimeStatus& status) {
        if(FAILED(slot.list->Close())) {
            setFailure(status,PipelineStage::FeatureCreateFailed,0,L"D3D12 feature-initialization command list close failed");
            return false;
        }
        ID3D12CommandList* lists[]={slot.list.Get()};
        queue12_->ExecuteCommandLists(1,lists);
        const std::uint64_t value=++fenceValue_;
        if(FAILED(queue12_->Signal(fence12_.Get(),value))) {
            setFailure(status,PipelineStage::FeatureCreateFailed,0,L"D3D12 feature-initialization completion signal failed");
            return false;
        }
        slot.completionValue=value;
        if(fence12_->GetCompletedValue()<value) {
            HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
            if(!event) {
                setFailure(status,PipelineStage::FeatureCreateFailed,static_cast<int>(GetLastError()),L"Could not create feature-initialization fence event");
                return false;
            }
            const HRESULT hr=fence12_->SetEventOnCompletion(value,event);
            const DWORD wait=SUCCEEDED(hr)?WaitForSingleObject(event,5000):WAIT_FAILED;
            CloseHandle(event);
            if(FAILED(hr) || wait!=WAIT_OBJECT_0) {
                setFailure(status,PipelineStage::FeatureCreateFailed,FAILED(hr)?static_cast<int>(hr):static_cast<int>(wait),L"Timed out waiting for Feature-18 initialization commands");
                return false;
            }
        }
        return true;
    }

    static std::wstring hex8(std::int32_t value) {
        wchar_t b[16]{};
        swprintf_s(b,L"%08X",static_cast<unsigned>(value));
        return b;
    }

    bool createSameAdapterD3D12(RuntimeStatus& status) {
        ComPtr<IDXGIDevice> dxgiDevice;
        ComPtr<IDXGIAdapter> adapter;
        if(FAILED(d11_.As(&dxgiDevice)) || FAILED(dxgiDevice->GetAdapter(&adapter))) {
            setFailure(status,PipelineStage::NeuralD3D12InitFailed,0,L"Could not resolve the D3D11 adapter for D3D12 neural interop");
            return false;
        }
        if(FAILED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&d12_)))) {
            setFailure(status,PipelineStage::NeuralD3D12InitFailed,0,L"Could not create a same-adapter D3D12 device for a D3D11 source application");
            return false;
        }
        D3D12_COMMAND_QUEUE_DESC q{};
        q.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
        q.Priority=D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
        if(FAILED(d12_->CreateCommandQueue(&q,IID_PPV_ARGS(&queue12_)))) {
            setFailure(status,PipelineStage::NeuralD3D12InitFailed,0,L"Could not create the private D3D12 neural command queue");
            return false;
        }
        ownsD3D12_=true;
        return true;
    }

    bool createSharedFence(RuntimeStatus& status) {
        if(FAILED(d12_->CreateFence(0,D3D12_FENCE_FLAG_SHARED,IID_PPV_ARGS(&fence12_)))) {
            setFailure(status,PipelineStage::NeuralD3D12InitFailed,0,L"D3D12 shared fence creation failed");
            return false;
        }
        HANDLE shared=nullptr;
        if(FAILED(d12_->CreateSharedHandle(fence12_.Get(),nullptr,GENERIC_ALL,nullptr,&shared)) || !shared) {
            setFailure(status,PipelineStage::NeuralD3D12InitFailed,0,L"D3D12 shared fence handle creation failed");
            return false;
        }
        const HRESULT hr=d11v5_->OpenSharedFence(shared,IID_PPV_ARGS(&fence11_));
        CloseHandle(shared);
        if(FAILED(hr)) {
            setFailure(status,PipelineStage::NeuralD3D12InitFailed,0,L"D3D11 OpenSharedFence failed");
            return false;
        }
        return true;
    }

    bool createCommandSlots(RuntimeStatus& status) {
        for(auto& slot:slots_) {
            if(FAILED(d12_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&slot.allocator))) ||
               FAILED(d12_->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,slot.allocator.Get(),nullptr,IID_PPV_ARGS(&slot.list))) ||
               FAILED(slot.list->Close())) {
                setFailure(status,PipelineStage::NeuralD3D12InitFailed,0,L"D3D12 neural command ring creation failed");
                return false;
            }
        }
        return true;
    }

    bool createSharedTexture(UINT width,UINT height,DXGI_FORMAT format,SharedTexture& out,RuntimeStatus& status) {
        D3D11_TEXTURE2D_DESC d{};
        d.Width=width; d.Height=height; d.MipLevels=1; d.ArraySize=1; d.Format=format;
        d.SampleDesc.Count=1; d.Usage=D3D11_USAGE_DEFAULT;
        d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;
        d.MiscFlags=D3D11_RESOURCE_MISC_SHARED_NTHANDLE|D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
        if(FAILED(d11_->CreateTexture2D(&d,nullptr,&out.d11))) {
            setFailure(status,PipelineStage::GuideResourcesFailed,0,L"Could not create a GPU-shareable D3D11 neural texture");
            return false;
        }
        ComPtr<IDXGIResource1> dxgiResource;
        if(FAILED(out.d11.As(&dxgiResource))) {
            setFailure(status,PipelineStage::GuideResourcesFailed,0,L"Shared neural texture does not expose IDXGIResource1");
            return false;
        }
        HANDLE shared=nullptr;
        if(FAILED(dxgiResource->CreateSharedHandle(nullptr,DXGI_SHARED_RESOURCE_READ|DXGI_SHARED_RESOURCE_WRITE,nullptr,&shared)) || !shared) {
            setFailure(status,PipelineStage::GuideResourcesFailed,0,L"CreateSharedHandle failed for a neural texture");
            return false;
        }
        const HRESULT hr=d12_->OpenSharedHandle(shared,IID_PPV_ARGS(&out.d12));
        CloseHandle(shared);
        if(FAILED(hr)) {
            setFailure(status,PipelineStage::GuideResourcesFailed,0,L"D3D12 OpenSharedHandle failed for a D3D11 neural texture");
            return false;
        }
        out.width=width; out.height=height; out.format=format;
        return true;
    }

    void releaseFeatureAndShared() {
        waitForSubmittedWork();
        if(feature_) { safeSnippetRelease(snippetRelease_,feature_); feature_=nullptr; }
        input_.reset(); output_.reset(); motion_.reset(); depth_.reset(); controlMask_.reset();
        width_=height_=0; createAttempted_=false; clearNgxFailure(createFailure_);
    }

    bool ensureSharedResources(const FrameResources& frame,RuntimeStatus& status) {
        if(width_==frame.width && height_==frame.height && input_.d12 && output_.d12 && motion_.d12 && depth_.d12 && controlMask_.d12) {
            markPipelineStage(status.stageMask,PipelineStage::GuideResourcesReady);
            return true;
        }
        releaseFeatureAndShared();
        width_=frame.width; height_=frame.height;
        if(!createSharedTexture(frame.width,frame.height,frame.inputFormat,input_,status) ||
           !createSharedTexture(frame.width,frame.height,frame.inputFormat,output_,status) ||
           !createSharedTexture(frame.width,frame.height,frame.motionFormat,motion_,status) ||
           !createSharedTexture(frame.width,frame.height,frame.depthFormat,depth_,status) ||
           !createSharedTexture(frame.width,frame.height,frame.controlMaskFormat,controlMask_,status)) {
            return false;
        }
        markPipelineStage(status.stageMask,PipelineStage::GuideResourcesReady);
        return true;
    }

    void fillCreateParameters(UINT width,UINT height,std::uint32_t renderPreset) {
        NVSDK_NGX_Parameter_SetUI(params_,kWidth,width);
        NVSDK_NGX_Parameter_SetUI(params_,kHeight,height);
        NVSDK_NGX_Parameter_SetUI(params_,kInputWidth,width);
        NVSDK_NGX_Parameter_SetUI(params_,kInputHeight,height);
        NVSDK_NGX_Parameter_SetUI(params_,kOutputWidth,width);
        NVSDK_NGX_Parameter_SetUI(params_,kOutputHeight,height);
        NVSDK_NGX_Parameter_SetUI(params_,kOutputWidth2,width);
        NVSDK_NGX_Parameter_SetUI(params_,kOutputHeight2,height);
        NVSDK_NGX_Parameter_SetUI(params_,kUpscaling,0u);
        NVSDK_NGX_Parameter_SetF(params_,kScale,1.0f);
        NVSDK_NGX_Parameter_SetF(params_,kScalingRatio,1.0f);
        NVSDK_NGX_Parameter_SetULL(
            params_,kScalingRatioCallbackParameter,
            static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(&scalingRatioCallback)));
        NVSDK_NGX_Parameter_SetUI(params_,kRenderPreset,renderPreset);
        NVSDK_NGX_Parameter_SetI(params_,NVSDK_NGX_Parameter_PerfQualityValue,
                                static_cast<int>(NVSDK_NGX_PerfQuality_Value_Balanced));
        NVSDK_NGX_Parameter_SetUI(params_,NVSDK_NGX_Parameter_Width,width);
        NVSDK_NGX_Parameter_SetUI(params_,NVSDK_NGX_Parameter_Height,height);
        NVSDK_NGX_Parameter_SetUI(params_,NVSDK_NGX_Parameter_CreationNodeMask,1u);
        NVSDK_NGX_Parameter_SetUI(params_,NVSDK_NGX_Parameter_VisibilityNodeMask,1u);
    }

    void setSubrectParameters(UINT width,UINT height) {
        NVSDK_NGX_Parameter_SetUI(params_,kColorSubrectX,0u);
        NVSDK_NGX_Parameter_SetUI(params_,kColorSubrectY,0u);
        NVSDK_NGX_Parameter_SetUI(params_,kColorSubrectW,width);
        NVSDK_NGX_Parameter_SetUI(params_,kColorSubrectH,height);
        NVSDK_NGX_Parameter_SetUI(params_,kOutputSubrectX,0u);
        NVSDK_NGX_Parameter_SetUI(params_,kOutputSubrectY,0u);
        NVSDK_NGX_Parameter_SetUI(params_,kOutputSubrectW,width);
        NVSDK_NGX_Parameter_SetUI(params_,kOutputSubrectH,height);
        NVSDK_NGX_Parameter_SetUI(params_,kMVecSubrectX,0u);
        NVSDK_NGX_Parameter_SetUI(params_,kMVecSubrectY,0u);
        NVSDK_NGX_Parameter_SetUI(params_,kMVecSubrectW,width);
        NVSDK_NGX_Parameter_SetUI(params_,kMVecSubrectH,height);
        NVSDK_NGX_Parameter_SetUI(params_,kDepthSubrectX,0u);
        NVSDK_NGX_Parameter_SetUI(params_,kDepthSubrectY,0u);
        NVSDK_NGX_Parameter_SetUI(params_,kDepthSubrectW,width);
        NVSDK_NGX_Parameter_SetUI(params_,kDepthSubrectH,height);
        NVSDK_NGX_Parameter_SetUI(params_,kControlSubrectW,width);
        NVSDK_NGX_Parameter_SetUI(params_,kControlSubrectH,height);
    }

    bool ensureFeature(ID3D12GraphicsCommandList* list,const FrameResources& frame,const Settings& settings,RuntimeStatus& status) {
        if(feature_) { markPipelineStage(status.stageMask,PipelineStage::FeatureCreated); return true; }
        if(createAttempted_) {
            status.lastResult=createFailure_.result;
            status.failureStage=PipelineStage::FeatureCreateFailed;
            const auto text=formatNgxFailure(createFailure_)+L"; using passthrough until Retry neural initialization";
            wcsncpy_s(status.message,text.c_str(),_TRUNCATE);
            return false;
        }
        createAttempted_=true;
        fillCreateParameters(frame.width,frame.height,settings.nrPreset);
        DWORD exceptionCode=0;
        const auto result=safeNgxCreateFeature(snippetCreate_,list,params_,&feature_,exceptionCode);
        if(exceptionCode) {
            feature_=nullptr;
            rememberNgxFailure(createFailure_,NgxFailureStage::CreateFeature,0,exceptionCode);
            setFailure(status,PipelineStage::FeatureCreateFailed,static_cast<int>(exceptionCode),formatNgxFailure(createFailure_));
            return false;
        }
        if(NVSDK_NGX_FAILED(result) || !feature_) {
            feature_=nullptr;
            rememberNgxFailure(createFailure_,NgxFailureStage::CreateFeature,static_cast<int>(result),0);
            setFailure(status,PipelineStage::FeatureCreateFailed,static_cast<int>(result),formatNgxFailure(createFailure_));
            return false;
        }
        clearNgxFailure(createFailure_);
        markPipelineStage(status.stageMask,PipelineStage::FeatureCreated);
        return true;
    }

    static D3D12_RESOURCE_BARRIER transition(ID3D12Resource* resource,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after) {
        D3D12_RESOURCE_BARRIER b{};
        b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource=resource;
        b.Transition.StateBefore=before;
        b.Transition.StateAfter=after;
        b.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        return b;
    }

    void transitionForNgx(ID3D12GraphicsCommandList* list,bool intoNgx) {
        const auto readState=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        std::array<D3D12_RESOURCE_BARRIER,5> b{};
        if(intoNgx) {
            b[0]=transition(input_.d12.Get(),D3D12_RESOURCE_STATE_COMMON,readState);
            b[1]=transition(motion_.d12.Get(),D3D12_RESOURCE_STATE_COMMON,readState);
            b[2]=transition(depth_.d12.Get(),D3D12_RESOURCE_STATE_COMMON,readState);
            b[3]=transition(controlMask_.d12.Get(),D3D12_RESOURCE_STATE_COMMON,readState);
            b[4]=transition(output_.d12.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        } else {
            b[0]=transition(input_.d12.Get(),readState,D3D12_RESOURCE_STATE_COMMON);
            b[1]=transition(motion_.d12.Get(),readState,D3D12_RESOURCE_STATE_COMMON);
            b[2]=transition(depth_.d12.Get(),readState,D3D12_RESOURCE_STATE_COMMON);
            b[3]=transition(controlMask_.d12.Get(),readState,D3D12_RESOURCE_STATE_COMMON);
            b[4]=transition(output_.d12.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COMMON);
        }
        list->ResourceBarrier(static_cast<UINT>(b.size()),b.data());
    }

    void waitForSubmittedWork() {
        std::uint64_t target=0;
        for(const auto& slot:slots_) target=std::max(target,slot.completionValue);
        if(!target || !fence12_ || fence12_->GetCompletedValue()>=target) return;
        HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
        if(!event) return;
        if(SUCCEEDED(fence12_->SetEventOnCompletion(target,event))) WaitForSingleObject(event,2000);
        CloseHandle(event);
    }

    void shutdown() {
        waitForSubmittedWork();
        if(feature_) { safeSnippetRelease(snippetRelease_,feature_); feature_=nullptr; }
        if(params_) { NVSDK_NGX_D3D12_DestroyParameters(params_); params_=nullptr; }
        if(snippetInitialized_ && d12_) safeSnippetShutdown(snippetShutdown_,d12_.Get());
        snippetInitialized_=false;
        snippetInit_=nullptr; snippetPopulate_=nullptr; snippetCreate_=nullptr; snippetEvaluate_=nullptr; snippetRelease_=nullptr; snippetShutdown_=nullptr;
        snippetGetApplicationId_=nullptr; snippetGetVersion_=nullptr;
        snippetApplicationId_=kFallbackSnippetApplicationId; snippetVersion_=0;
        if(usingForwarder_ && forwarderUnload_) forwarderUnload_();
        forwarderUnload_=nullptr; usingForwarder_=false;
        if(snippetModule_) { FreeLibrary(snippetModule_); snippetModule_=nullptr; }
        if(ngxInitialized_ && d12_) NVSDK_NGX_D3D12_Shutdown1(d12_.Get());
        ngxInitialized_=false;
        input_.reset(); output_.reset(); motion_.reset(); depth_.reset(); controlMask_.reset();
        for(auto& slot:slots_) { slot.list.Reset(); slot.allocator.Reset(); slot.completionValue=0; }
        fence11_.Reset(); fence12_.Reset(); ctx11v4_.Reset(); d11v5_.Reset();
        queue12_.Reset(); d12_.Reset(); ctx11_.Reset(); d11_.Reset();
        initialized_=false; ownsD3D12_=false; reset_=true; createAttempted_=false;
        width_=height_=0; fenceValue_=0; frameIndex_=0; runtime_.clear(); clearNgxFailure(createFailure_);
    }

    ComPtr<ID3D11Device> d11_;
    ComPtr<ID3D11DeviceContext> ctx11_;
    ComPtr<ID3D11Device5> d11v5_;
    ComPtr<ID3D11DeviceContext4> ctx11v4_;
    ComPtr<ID3D12Device> d12_;
    ComPtr<ID3D12CommandQueue> queue12_;
    ComPtr<ID3D12Fence> fence12_;
    ComPtr<ID3D11Fence> fence11_;
    std::array<FrameSlot,kFrameSlots> slots_{};
    SharedTexture input_,output_,motion_,depth_,controlMask_;
    NVSDK_NGX_Parameter* params_{};
    NVSDK_NGX_Handle* feature_{};
    HMODULE snippetModule_{};
    SnippetInitExtFn snippetInit_{};
    SnippetPopulateFn snippetPopulate_{};
    SnippetCreateFn snippetCreate_{};
    SnippetEvaluateFn snippetEvaluate_{};
    SnippetReleaseFn snippetRelease_{};
    SnippetShutdownFn snippetShutdown_{};
    SnippetGetApplicationIdFn snippetGetApplicationId_{};
    SnippetGetVersionFn snippetGetVersion_{};
    ForwarderUnloadFn forwarderUnload_{};
    bool usingForwarder_{};
    unsigned long long snippetApplicationId_{kFallbackSnippetApplicationId};
    std::uint32_t snippetVersion_{};
    std::wstring runtime_;
    UINT width_{},height_{};
    std::uint64_t fenceValue_{},frameIndex_{};
    bool initialized_{};
    bool ngxInitialized_{};
    bool snippetInitialized_{};
    bool ownsD3D12_{};
    bool reset_{true};
    bool createAttempted_{};
    NgxFailureState createFailure_{};
};

Backend* createNgxNR(){return new NgxNR;}

#else
class NgxUnavailable final : public Backend {
public:
    bool initialize(const BackendInitContext&,const std::wstring&,const Settings&,RuntimeStatus& status) override {
        status.neuralApi=NeuralExecutionApi::D3D12;
        status.failureStage=PipelineStage::NgxCoreInitFailed;
        wcscpy_s(status.message,L"Direct NGX DLSS-NR backend was not compiled for this bridge architecture");
        return false;
    }
    bool evaluate(ID3D11DeviceContext*,const FrameResources&,const Settings&,RuntimeStatus&) override { return false; }
    void reset() override {}
    const wchar_t* name() const override { return L"NGX DLSS 5 NR unavailable"; }
};
Backend* createNgxNR(){return new NgxUnavailable;}
#endif

} // namespace udlss::neural
