#include "backend.hpp"
#include <windows.h>
#include <dxgi1_2.h>
#include <d3d11_4.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <array>
#include <vector>
#include <algorithm>
#include <filesystem>
#include "../gpu/game_temporal_guides.hpp"

#ifdef UDLSS_WITH_STREAMLINE
#include <sl.h>
#endif

using Microsoft::WRL::ComPtr;

namespace udlss::neural {
#ifdef UDLSS_WITH_STREAMLINE
namespace {
constexpr std::size_t kFrameSlots=3;

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

void setFailure(RuntimeStatus& status,PipelineStage stage,std::int32_t result,const wchar_t* message) {
    status.failureStage=stage;
    status.lastResult=result;
    wcsncpy_s(status.message,message,_TRUNCATE);
}
}

class StreamlineNR final : public Backend {
public:
    ~StreamlineNR() override { shutdown(); }

    bool initialize(const BackendInitContext& init,const std::wstring& runtime,const Settings& settings,RuntimeStatus& status) override {
        shutdown();
        status.neuralApi=NeuralExecutionApi::D3D12;
        status.neuralLocation=NeuralExecutionLocation::InGame;
        status.neuralBackendKind=NeuralBackendKind::Streamline1004;
        if(!init.d3d11Device || !init.d3d11Context) {
            setFailure(status,PipelineStage::NeuralD3D12InitFailed,0,L"Streamline in-game mount requires the D3D11 guide device/context");
            return false;
        }
        d11_=init.d3d11Device;
        ctx11_=init.d3d11Context;
        runtime_=runtime;
        if(FAILED(d11_.As(&d11v5_)) || FAILED(ctx11_.As(&ctx11v4_))) {
            setFailure(status,PipelineStage::NeuralD3D12InitFailed,0,L"Streamline D3D12 mount requires D3D11 fence interop");
            return false;
        }

        if(init.d3d12Device && init.d3d12Queue) {
            d12_=init.d3d12Device;
            queue12_=init.d3d12Queue;
            if(queue12_->GetDesc().Type!=D3D12_COMMAND_LIST_TYPE_DIRECT) {
                setFailure(status,PipelineStage::NeuralD3D12InitFailed,0,L"Streamline mount requires a DIRECT D3D12 queue");
                return false;
            }
        } else if(!createSameAdapterD3D12(status)) return false;

        if(!createSharedFence(status) || !createCommandSlots(status)) return false;
        markPipelineStage(status.stageMask,PipelineStage::NeuralD3D12Ready);

        const std::wstring dll=runtime_+L"\\sl.interposer.dll";
        module_=LoadLibraryExW(dll.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if(!module_) {
            setFailure(status,PipelineStage::NgxCoreInitFailed,(int)GetLastError(),L"sl.interposer.dll could not be loaded from runtime folder");
            return false;
        }
        if(!loadSymbol("slInit",init_) || !loadSymbol("slShutdown",shutdown_) ||
           !loadSymbol("slSetD3DDevice",setDevice_) || !loadSymbol("slIsFeatureSupported",supported_) ||
           !loadSymbol("slGetFeatureRequirements",requirements_) || !loadSymbol("slGetFeatureVersion",featureVersion_) ||
           !loadSymbol("slGetNewFrameToken",token_) || !loadSymbol("slSetConstants",constants_) ||
           !loadSymbol("slEvaluateFeature",evaluate_) || !loadSymbol("slFreeResources",free_)) {
            setFailure(status,PipelineStage::NgxCoreInitFailed,0,L"Streamline core API export missing");
            return false;
        }

        const wchar_t* paths[]={runtime_.c_str()};
        const sl::Feature features[]={sl::kFeatureDLSS_NR};
        sl::Preferences preferences{};
        preferences.showConsole=false;
        preferences.logLevel=sl::LogLevel::eDefault;
        preferences.pathsToPlugins=paths;
        preferences.numPathsToPlugins=1;
        preferences.featuresToLoad=features;
        preferences.numFeaturesToLoad=1;
        preferences.engine=sl::EngineType::eCustom;
        preferences.engineVersion="UniversalDLSS5-0.4.3";
        preferences.renderAPI=sl::RenderAPI::eD3D12;
        preferences.flags=sl::PreferenceFlags::eUseManualHooking |
                          sl::PreferenceFlags::eUseFrameBasedResourceTagging |
                          sl::PreferenceFlags::eDisableCLStateTracking;
        auto result=init_(preferences,sl::kSDKVersion);
        if(result!=sl::Result::eOk) {
            setFailure(status,PipelineStage::NgxCoreInitFailed,(int)result,L"slInit(D3D12 DLSS-NR) failed");
            return false;
        }
        slInitialized_=true;
        result=setDevice_(d12_.Get());
        if(result!=sl::Result::eOk) {
            setFailure(status,PipelineStage::NgxCoreInitFailed,(int)result,L"slSetD3DDevice(D3D12) failed");
            return false;
        }
        markPipelineStage(status.stageMask,PipelineStage::NgxCoreInitialized);

        sl::AdapterInfo adapterInfo{};
        DXGI_ADAPTER_DESC desc{};
        ComPtr<IDXGIDevice> dxgiDevice;
        ComPtr<IDXGIAdapter> adapter;
        if(SUCCEEDED(d11_.As(&dxgiDevice)) && SUCCEEDED(dxgiDevice->GetAdapter(&adapter)) && SUCCEEDED(adapter->GetDesc(&desc))) {
            adapterInfo.deviceLUID=(uint8_t*)&desc.AdapterLuid;
            adapterInfo.deviceLUIDSizeInBytes=sizeof(desc.AdapterLuid);
        }
        result=supported_(sl::kFeatureDLSS_NR,adapterInfo);
        if(result!=sl::Result::eOk && !settings.attemptUnsupportedHardware) {
            setFailure(status,PipelineStage::FeatureCreateFailed,(int)result,L"Streamline DLSS-NR reports unsupported on this adapter");
            return false;
        }

        sl::FeatureRequirements req{};
        result=requirements_(sl::kFeatureDLSS_NR,req);
        if(result!=sl::Result::eOk) {
            setFailure(status,PipelineStage::FeatureCreateFailed,(int)result,L"slGetFeatureRequirements(DLSS_NR) failed");
            return false;
        }
        if(!(req.flags & sl::FeatureRequirementFlags::eD3D12Supported)) {
            setFailure(status,PipelineStage::FeatureCreateFailed,0,L"Installed sl.dlss_nr plugin does not advertise D3D12 support");
            return false;
        }
        required_.clear();
        if(req.numRequiredTags && req.requiredTags) required_.assign(req.requiredTags,req.requiredTags+req.numRequiredTags);
        for(auto tag:required_) {
            if(!canSupply(tag)) {
                status.missingRequiredTag=tag;
                swprintf_s(status.message,L"DLSS-NR plugin requires unavailable resource tag %u",(unsigned)tag);
                status.failureStage=PipelineStage::FeatureCreateFailed;
                return false;
            }
        }
        status.requiredTagCount=req.numRequiredTags;
        diagnostics_.requiredTags=req.numRequiredTags;
        sl::FeatureVersion fv{};
        if(featureVersion_(sl::kFeatureDLSS_NR,fv)==sl::Result::eOk) {
            diagnostics_.slMajor=fv.versionSL.major; diagnostics_.slMinor=fv.versionSL.minor; diagnostics_.slBuild=fv.versionSL.build;
            diagnostics_.ngxMajor=fv.versionNGX.major; diagnostics_.ngxMinor=fv.versionNGX.minor; diagnostics_.ngxBuild=fv.versionNGX.build;
        }
        status.feature=diagnostics_;
        ready_=true;
        status.failureStage=PipelineStage::None;
        wcscpy_s(status.message,L"Direct in-game Streamline DLSS-NR mount initialized on D3D12");
        return true;
    }

    bool evaluate(ID3D11DeviceContext*,const FrameResources& frame,const Settings& settings,RuntimeStatus& status) override {
        gpu::GameGuideCaptureGuard captureGuard;
        status.neuralApi=NeuralExecutionApi::D3D12;
        status.neuralLocation=NeuralExecutionLocation::InGame;
        status.neuralBackendKind=NeuralBackendKind::Streamline1004;
        if(!ready_ || !frame.input || !frame.output || !frame.motion || !frame.depth) return false;
        if(!ensureSharedResources(frame,status)) return false;

        FrameSlot& slot=slots_[frameIndex_%kFrameSlots];
        const std::uint32_t tokenIndex=frameIndex_++;
        if(slot.completionValue && fence12_->GetCompletedValue()<slot.completionValue) {
            setFailure(status,PipelineStage::FeatureEvaluateFailed,0,L"Streamline D3D12 command ring busy; bypassing instead of blocking Present");
            return false;
        }
        if(FAILED(slot.allocator->Reset()) || FAILED(slot.list->Reset(slot.allocator.Get(),nullptr))) {
            setFailure(status,PipelineStage::FeatureEvaluateFailed,0,L"Streamline D3D12 command list reset failed");
            return false;
        }

        ctx11_->CopyResource(input_.d11.Get(),frame.input);
        ctx11_->CopyResource(motion_.d11.Get(),frame.motion);
        ctx11_->CopyResource(depth_.d11.Get(),frame.depth);
        if(settings.useControlMask && frame.controlMask) ctx11_->CopyResource(mask_.d11.Get(),frame.controlMask);
        const std::uint64_t readyValue=++fenceValue_;
        if(FAILED(ctx11v4_->Signal(fence11_.Get(),readyValue)) || FAILED(queue12_->Wait(fence12_.Get(),readyValue))) {
            setFailure(status,PipelineStage::FeatureEvaluateFailed,0,L"Streamline guide fence handoff failed");
            slot.list->Close();
            return false;
        }

        sl::FrameToken* frameToken=nullptr;
        auto result=token_(frameToken,&tokenIndex);
        if(result!=sl::Result::eOk || !frameToken) {
            setFailure(status,PipelineStage::FeatureEvaluateFailed,(int)result,L"slGetNewFrameToken failed");
            slot.list->Close();
            return false;
        }
        sl::Constants common{};
        common.cameraViewToClip=identity();
        common.clipToCameraView=identity();
        common.clipToLensClip=identity();
        common.clipToPrevClip=identity();
        common.prevClipToClip=identity();
        common.jitterOffset=sl::float2(0,0);
        // Our vectors are already current->previous in pixel units. SL's common scale converts the pixel field to normalized screen units.
        common.mvecScale=sl::float2(frame.motionScaleX/std::max(1.0f,(float)frame.width),frame.motionScaleY/std::max(1.0f,(float)frame.height));
        common.cameraPinholeOffset=sl::float2(0,0);
        common.cameraPos=sl::float3(0,0,0); common.cameraUp=sl::float3(0,1,0);
        common.cameraRight=sl::float3(1,0,0); common.cameraFwd=sl::float3(0,0,1);
        common.cameraNear=0.0f; common.cameraFar=1.0f; common.cameraFOV=1.0f;
        common.cameraAspectRatio=(float)frame.width/(float)std::max(1u,frame.height);
        common.depthInverted=frame.depthInverted?sl::Boolean::eTrue:sl::Boolean::eFalse;
        common.cameraMotionIncluded=sl::Boolean::eTrue;
        common.motionVectors3D=sl::Boolean::eFalse;
        common.reset=(reset_||frame.resetHistory)?sl::Boolean::eTrue:sl::Boolean::eFalse;
        common.orthographicProjection=sl::Boolean::eFalse;
        common.motionVectorsDilated=sl::Boolean::eTrue;
        common.motionVectorsJittered=sl::Boolean::eFalse;
        const sl::ViewportHandle viewport{1};
        result=constants_(common,*frameToken,viewport);
        if(result!=sl::Result::eOk) {
            setFailure(status,PipelineStage::FeatureEvaluateFailed,(int)result,L"slSetConstants(DLSS-NR) failed");
            slot.list->Close();
            return false;
        }

        sl::Extent extent{0,0,frame.width,frame.height};
        sl::Resource input{sl::ResourceType::eTex2d,input_.d12.Get(),(uint32_t)D3D12_RESOURCE_STATE_COMMON};
        sl::Resource output{sl::ResourceType::eTex2d,output_.d12.Get(),(uint32_t)D3D12_RESOURCE_STATE_COMMON};
        sl::Resource motion{sl::ResourceType::eTex2d,motion_.d12.Get(),(uint32_t)D3D12_RESOURCE_STATE_COMMON};
        sl::Resource depth{sl::ResourceType::eTex2d,depth_.d12.Get(),(uint32_t)D3D12_RESOURCE_STATE_COMMON};
        sl::Resource mask{sl::ResourceType::eTex2d,mask_.d12.Get(),(uint32_t)D3D12_RESOURCE_STATE_COMMON};
        initializeResource(input,frame.width,frame.height,frame.inputFormat);
        initializeResource(output,frame.width,frame.height,frame.inputFormat);
        initializeResource(motion,frame.width,frame.height,frame.motionFormat);
        initializeResource(depth,frame.width,frame.height,frame.depthFormat);
        initializeResource(mask,frame.width,frame.height,frame.controlMaskFormat);

        std::vector<sl::ResourceTag> tags;
        auto addDefault=[&](){
            tags.emplace_back(&input,sl::kBufferTypeUpliftInputColor,sl::ResourceLifecycle::eValidUntilEvaluate,&extent);
            tags.emplace_back(&output,sl::kBufferTypeUpliftOutputColor,sl::ResourceLifecycle::eValidUntilEvaluate,&extent);
            tags.emplace_back(&motion,sl::kBufferTypeMotionVectors,sl::ResourceLifecycle::eValidUntilEvaluate,&extent);
            tags.emplace_back(&depth,sl::kBufferTypeDepth,sl::ResourceLifecycle::eValidUntilEvaluate,&extent);
            if(settings.useControlMask && frame.controlMask)
                tags.emplace_back(&mask,sl::kBufferTypeUpliftControlMask,sl::ResourceLifecycle::eValidUntilEvaluate,&extent);
        };
        if(required_.empty()) addDefault();
        else {
            for(auto type:required_) {
                sl::Resource* r=resourceFor(type,input,output,motion,depth,mask,settings.useControlMask&&frame.controlMask);
                if(!r) {
                    status.missingRequiredTag=type;
                    setFailure(status,PipelineStage::FeatureEvaluateFailed,0,L"A required Streamline DLSS-NR resource became unavailable");
                    slot.list->Close();
                    return false;
                }
                tags.emplace_back(r,type,sl::ResourceLifecycle::eValidUntilEvaluate,&extent);
            }
            if(settings.useControlMask && frame.controlMask &&
               std::find(required_.begin(),required_.end(),sl::kBufferTypeUpliftControlMask)==required_.end())
                tags.emplace_back(&mask,sl::kBufferTypeUpliftControlMask,sl::ResourceLifecycle::eValidUntilEvaluate,&extent);
        }
        std::vector<const sl::BaseStructure*> inputs;
        inputs.reserve(tags.size()+1);
        inputs.push_back(&viewport);
        for(auto& tag:tags) inputs.push_back(&tag);

        result=evaluate_(sl::kFeatureDLSS_NR,*frameToken,inputs.data(),(uint32_t)inputs.size(),reinterpret_cast<sl::CommandBuffer*>(slot.list.Get()));
        if(result!=sl::Result::eOk) {
            setFailure(status,PipelineStage::FeatureEvaluateFailed,(int)result,L"slEvaluateFeature(DLSS_NR/1004) failed");
            slot.list->Close();
            return false;
        }
        markPipelineStage(status.stageMask,PipelineStage::FeatureCreated);
        markPipelineStage(status.stageMask,PipelineStage::FeatureEvaluated);
        if(FAILED(slot.list->Close())) {
            setFailure(status,PipelineStage::FeatureEvaluateFailed,0,L"Streamline D3D12 command list close failed");
            return false;
        }
        ID3D12CommandList* lists[]={slot.list.Get()};
        queue12_->ExecuteCommandLists(1,lists);
        const std::uint64_t doneValue=++fenceValue_;
        if(FAILED(queue12_->Signal(fence12_.Get(),doneValue))) {
            setFailure(status,PipelineStage::FeatureEvaluateFailed,0,L"Streamline D3D12 completion signal failed");
            return false;
        }
        slot.completionValue=doneValue;
        if(FAILED(ctx11v4_->Wait(fence11_.Get(),doneValue))) {
            setFailure(status,PipelineStage::FeatureEvaluateFailed,0,L"D3D11 wait for Streamline D3D12 output failed");
            return false;
        }
        ctx11_->CopyResource(frame.output,output_.d11.Get());
        reset_=false;
        status.failureStage=PipelineStage::None;
        status.lastResult=(int)result;
        wcscpy_s(status.message,L"Direct in-game Streamline DLSS-NR feature 1004 active");
        return true;
    }

    void reset() override { reset_=true; }
    const wchar_t* name() const override { return L"Streamline DLSS 5 NR (D3D12 in-game mount)"; }

private:
    template<class T> bool loadSymbol(const char* name,T*& function) {
        function=reinterpret_cast<T*>(GetProcAddress(module_,name));
        return function!=nullptr;
    }
    static sl::float4x4 identity() {
        sl::float4x4 m{};
        m.row[0]=sl::float4(1,0,0,0); m.row[1]=sl::float4(0,1,0,0);
        m.row[2]=sl::float4(0,0,1,0); m.row[3]=sl::float4(0,0,0,1);
        return m;
    }
    static void initializeResource(sl::Resource& r,uint32_t w,uint32_t h,DXGI_FORMAT fmt) {
        r.width=w; r.height=h; r.nativeFormat=(uint32_t)fmt; r.mipLevels=1; r.arrayLayers=1;
    }
    static bool isInputColor(sl::BufferType t) { return t==sl::kBufferTypeUpliftInputColor || t==sl::kBufferTypeHUDLessColor || t==sl::kBufferTypeScalingInputColor || t==sl::kBufferTypeOpaqueColor; }
    static bool isOutputColor(sl::BufferType t) { return t==sl::kBufferTypeUpliftOutputColor || t==sl::kBufferTypeScalingOutputColor || t==sl::kBufferTypeBackbuffer; }
    static bool isDepth(sl::BufferType t) { return t==sl::kBufferTypeDepth || t==sl::kBufferTypeLinearDepth || t==sl::kBufferTypeHiResDepth; }
    static bool canSupply(sl::BufferType t) { return isInputColor(t)||isOutputColor(t)||isDepth(t)||t==sl::kBufferTypeMotionVectors||t==sl::kBufferTypeUpliftControlMask; }
    static sl::Resource* resourceFor(sl::BufferType t,sl::Resource& input,sl::Resource& output,sl::Resource& motion,sl::Resource& depth,sl::Resource& mask,bool hasMask) {
        if(isInputColor(t)) return &input; if(isOutputColor(t)) return &output; if(isDepth(t)) return &depth;
        if(t==sl::kBufferTypeMotionVectors) return &motion; if(t==sl::kBufferTypeUpliftControlMask&&hasMask) return &mask; return nullptr;
    }

    bool createSameAdapterD3D12(RuntimeStatus& status) {
        ComPtr<IDXGIDevice> dxgi; ComPtr<IDXGIAdapter> adapter;
        if(FAILED(d11_.As(&dxgi)) || FAILED(dxgi->GetAdapter(&adapter)) ||
           FAILED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&d12_)))) {
            setFailure(status,PipelineStage::NeuralD3D12InitFailed,0,L"Could not create same-adapter D3D12 device for Streamline mount"); return false;
        }
        D3D12_COMMAND_QUEUE_DESC q{}; q.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
        if(FAILED(d12_->CreateCommandQueue(&q,IID_PPV_ARGS(&queue12_)))) {
            setFailure(status,PipelineStage::NeuralD3D12InitFailed,0,L"Could not create D3D12 queue for Streamline mount"); return false;
        }
        return true;
    }
    bool createSharedFence(RuntimeStatus& status) {
        if(FAILED(d12_->CreateFence(0,D3D12_FENCE_FLAG_SHARED,IID_PPV_ARGS(&fence12_)))) { setFailure(status,PipelineStage::NeuralD3D12InitFailed,0,L"Streamline shared fence creation failed"); return false; }
        HANDLE h=nullptr;
        if(FAILED(d12_->CreateSharedHandle(fence12_.Get(),nullptr,GENERIC_ALL,nullptr,&h))||!h) { setFailure(status,PipelineStage::NeuralD3D12InitFailed,0,L"Streamline shared fence handle creation failed"); return false; }
        HRESULT hr=d11v5_->OpenSharedFence(h,IID_PPV_ARGS(&fence11_)); CloseHandle(h);
        if(FAILED(hr)) { setFailure(status,PipelineStage::NeuralD3D12InitFailed,0,L"Streamline D3D11 OpenSharedFence failed"); return false; }
        return true;
    }
    bool createCommandSlots(RuntimeStatus& status) {
        for(auto& s:slots_) {
            if(FAILED(d12_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&s.allocator))) ||
               FAILED(d12_->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,s.allocator.Get(),nullptr,IID_PPV_ARGS(&s.list))) || FAILED(s.list->Close())) {
                setFailure(status,PipelineStage::NeuralD3D12InitFailed,0,L"Streamline D3D12 command ring creation failed"); return false;
            }
        }
        return true;
    }
    bool createSharedTexture(UINT w,UINT h,DXGI_FORMAT fmt,SharedTexture& out,RuntimeStatus& status) {
        D3D11_TEXTURE2D_DESC d{}; d.Width=w; d.Height=h; d.MipLevels=1; d.ArraySize=1; d.Format=fmt; d.SampleDesc.Count=1;
        d.Usage=D3D11_USAGE_DEFAULT; d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS; d.MiscFlags=D3D11_RESOURCE_MISC_SHARED_NTHANDLE|D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
        if(FAILED(d11_->CreateTexture2D(&d,nullptr,&out.d11))) { setFailure(status,PipelineStage::GuideResourcesFailed,0,L"Could not create Streamline shared D3D11 texture"); return false; }
        ComPtr<IDXGIResource1> dxgi; if(FAILED(out.d11.As(&dxgi))) return false;
        HANDLE shared=nullptr;
        if(FAILED(dxgi->CreateSharedHandle(nullptr,DXGI_SHARED_RESOURCE_READ|DXGI_SHARED_RESOURCE_WRITE,nullptr,&shared))||!shared) { setFailure(status,PipelineStage::GuideResourcesFailed,0,L"CreateSharedHandle failed for Streamline texture"); return false; }
        HRESULT hr=d12_->OpenSharedHandle(shared,IID_PPV_ARGS(&out.d12)); CloseHandle(shared);
        if(FAILED(hr)) { setFailure(status,PipelineStage::GuideResourcesFailed,0,L"D3D12 OpenSharedHandle failed for Streamline texture"); return false; }
        out.width=w; out.height=h; out.format=fmt; return true;
    }
    bool ensureSharedResources(const FrameResources& f,RuntimeStatus& status) {
        if(width_==f.width&&height_==f.height&&input_.d12&&output_.d12&&motion_.d12&&depth_.d12&&mask_.d12) { markPipelineStage(status.stageMask,PipelineStage::GuideResourcesReady); return true; }
        waitForWork(); input_.reset(); output_.reset(); motion_.reset(); depth_.reset(); mask_.reset(); width_=f.width; height_=f.height; reset_=true;
        if(!createSharedTexture(f.width,f.height,f.inputFormat,input_,status)||!createSharedTexture(f.width,f.height,f.inputFormat,output_,status)||
           !createSharedTexture(f.width,f.height,f.motionFormat,motion_,status)||!createSharedTexture(f.width,f.height,f.depthFormat,depth_,status)||
           !createSharedTexture(f.width,f.height,f.controlMaskFormat,mask_,status)) return false;
        markPipelineStage(status.stageMask,PipelineStage::GuideResourcesReady); return true;
    }
    void waitForWork() {
        if(!fence12_) return;
        std::uint64_t max=0; for(auto& s:slots_) max=std::max(max,s.completionValue);
        if(!max||fence12_->GetCompletedValue()>=max) return;
        HANDLE e=CreateEventW(nullptr,FALSE,FALSE,nullptr); if(!e) return;
        if(SUCCEEDED(fence12_->SetEventOnCompletion(max,e))) WaitForSingleObject(e,2000); CloseHandle(e);
    }
    void shutdown() {
        waitForWork();
        if(ready_&&free_) { sl::ViewportHandle vp{1}; free_(sl::kFeatureDLSS_NR,vp); }
        ready_=false; required_.clear(); input_.reset(); output_.reset(); motion_.reset(); depth_.reset(); mask_.reset();
        if(slInitialized_&&shutdown_) shutdown_(); slInitialized_=false;
        if(module_) FreeLibrary(module_); module_=nullptr;
        for(auto& s:slots_) { s.list.Reset(); s.allocator.Reset(); s.completionValue=0; }
        fence11_.Reset(); fence12_.Reset(); queue12_.Reset(); d12_.Reset(); ctx11v4_.Reset(); d11v5_.Reset(); ctx11_.Reset(); d11_.Reset();
        init_=nullptr; shutdown_=nullptr; setDevice_=nullptr; supported_=nullptr; requirements_=nullptr; featureVersion_=nullptr; token_=nullptr; constants_=nullptr; evaluate_=nullptr; free_=nullptr;
        width_=height_=0; frameIndex_=0; fenceValue_=0; reset_=true;
    }

    HMODULE module_{}; std::wstring runtime_; bool ready_{}; bool slInitialized_{}; bool reset_{true};
    UINT width_{},height_{}; uint32_t frameIndex_{}; uint64_t fenceValue_{};
    ComPtr<ID3D11Device> d11_; ComPtr<ID3D11DeviceContext> ctx11_; ComPtr<ID3D11Device5> d11v5_; ComPtr<ID3D11DeviceContext4> ctx11v4_;
    ComPtr<ID3D12Device> d12_; ComPtr<ID3D12CommandQueue> queue12_; ComPtr<ID3D12Fence> fence12_; ComPtr<ID3D11Fence> fence11_;
    std::array<FrameSlot,kFrameSlots> slots_{}; SharedTexture input_,output_,motion_,depth_,mask_;
    std::vector<sl::BufferType> required_; RuntimeFeatureDiagnostics diagnostics_{};
    PFun_slInit* init_{}; PFun_slShutdown* shutdown_{}; PFun_slSetD3DDevice* setDevice_{}; PFun_slIsFeatureSupported* supported_{};
    PFun_slGetFeatureRequirements* requirements_{}; PFun_slGetFeatureVersion* featureVersion_{}; PFun_slGetNewFrameToken* token_{};
    PFun_slSetConstants* constants_{}; PFun_slEvaluateFeature* evaluate_{}; PFun_slFreeResources* free_{};
};

Backend* createStreamlineNR(){ return new StreamlineNR; }
#else
class StreamlineNR final : public Backend {
public:
 bool initialize(const BackendInitContext&,const std::wstring&,const Settings&,RuntimeStatus& status) override { wcscpy_s(status.message,L"Bridge was built without Streamline headers"); return false; }
 bool evaluate(ID3D11DeviceContext*,const FrameResources&,const Settings&,RuntimeStatus&) override { return false; }
 void reset() override {}
 const wchar_t* name() const override { return L"DLSS5 unavailable"; }
};
Backend* createStreamlineNR(){ return new StreamlineNR; }
#endif
} // namespace udlss::neural
