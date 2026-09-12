#include "backend.hpp"
#include "external_host_protocol.hpp"
#include "udlss/external_host_policy.hpp"
#include <windows.h>
#include <d3d11_4.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <filesystem>
#include <string>
#include <atomic>
#include <algorithm>
#include <new>

using Microsoft::WRL::ComPtr;

namespace udlss::neural {
namespace {
std::atomic_uint32_t gSessionSequence{1};

std::filesystem::path moduleDirectory() {
    HMODULE module{};
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&moduleDirectory),&module);
    wchar_t path[32768]{};
    const DWORD n=GetModuleFileNameW(module,path,_countof(path));
    return (n && n<_countof(path))?std::filesystem::path(path).parent_path():std::filesystem::current_path();
}

void setFailure(RuntimeStatus& st,PipelineStage stage,std::int32_t result,const std::wstring& text) {
    st.failureStage=stage;st.lastResult=result;wcsncpy_s(st.message,text.c_str(),_TRUNCATE);
}

bool waitForState(hostipc::Shared* s,HANDLE hostProcess,hostipc::State wanted,DWORD timeoutMs) {
    const auto end=GetTickCount64()+timeoutMs;
    while(GetTickCount64()<end) {
        const auto state=static_cast<hostipc::State>(InterlockedCompareExchange(&s->state,0,0));
        if(state==wanted) return true;
        if(state==hostipc::State::Error || state==hostipc::State::Stopping) return false;
        if(hostProcess && WaitForSingleObject(hostProcess,0)==WAIT_OBJECT_0) return false;
        Sleep(5);
    }
    return false;
}

std::wstring genName(const std::wstring& base,std::uint32_t generation) {
    return base+L"."+std::to_wstring(generation);
}

bool duplicateIntoProcess(HANDLE source,HANDLE targetProcess,std::uint64_t& wireValue) {
    wireValue=0;
    if(!source || !targetProcess) return false;
    HANDLE remote{};
    if(!DuplicateHandle(GetCurrentProcess(),source,targetProcess,&remote,0,FALSE,DUPLICATE_SAME_ACCESS) || !remote) return false;
    wireValue=static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(remote));
    return true;
}

enum class EnqueueWaitResult { Ready, Timeout, HostExited, HostError, WaitFailed };

EnqueueWaitResult waitForEnqueuedSequence(hostipc::Shared* shared,HANDLE doneEvent,HANDLE hostProcess,LONG requestedSeq,DWORD timeoutMs) {
    const ULONGLONG deadline=GetTickCount64()+timeoutMs;
    for(;;) {
        const LONG published=InterlockedCompareExchange(&shared->enqueuedSeq,0,0);
        if(nrHostSequenceEnqueued(published,requestedSeq)) return EnqueueWaitResult::Ready;
        if(hostProcess && WaitForSingleObject(hostProcess,0)==WAIT_OBJECT_0) return EnqueueWaitResult::HostExited;
        const auto state=static_cast<hostipc::State>(InterlockedCompareExchange(&shared->state,0,0));
        if(state==hostipc::State::Error || state==hostipc::State::Stopping) return EnqueueWaitResult::HostError;
        const ULONGLONG now=GetTickCount64();
        if(now>=deadline) return EnqueueWaitResult::Timeout;
        const DWORD remaining=static_cast<DWORD>(std::min<ULONGLONG>(deadline-now,timeoutMs));
        HANDLE waits[]={doneEvent,hostProcess};
        const DWORD count=hostProcess?2u:1u;
        const DWORD w=WaitForMultipleObjects(count,waits,FALSE,remaining);
        if(w==WAIT_TIMEOUT) return EnqueueWaitResult::Timeout;
        if(w==WAIT_FAILED) return EnqueueWaitResult::WaitFailed;
        if(hostProcess && w==WAIT_OBJECT_0+1) return EnqueueWaitResult::HostExited;
        // A Done event can belong to a frame that previously timed out. The
        // sequence number, not the event wake, is the authoritative publication.
    }
}
}

class ExternalHostNR final : public Backend {
public:
    ~ExternalHostNR() override { shutdown(); }

    bool initialize(const BackendInitContext& init,const std::wstring& runtime,const Settings& settings,RuntimeStatus& st) override {
        shutdown();
        st.neuralApi=NeuralExecutionApi::D3D12;
        if(!init.d3d11Device || !init.d3d11Context) {
            setFailure(st,PipelineStage::HostConnectFailed,0,L"External NR host requires the D3D11 guide device/context");
            return false;
        }
        const auto runtimeDll=std::filesystem::path(runtime)/L"nvngx_dlssnr.dll";
        std::error_code ec;
        if(!std::filesystem::is_regular_file(runtimeDll,ec)) {
            setFailure(st,PipelineStage::HostConnectFailed,0,L"nvngx_dlssnr.dll not found in the selected runtime folder");
            return false;
        }
        d11_=init.d3d11Device;ctx11_=init.d3d11Context;runtime_=runtime;lastPreset_=settings.nrPreset;
        if(FAILED(d11_.As(&d11v5_)) || FAILED(ctx11_.As(&ctx11v4_))) {
            setFailure(st,PipelineStage::HostConnectFailed,0,L"External NR host requires ID3D11Device5/ID3D11DeviceContext4 shared-fence support");
            return false;
        }
        ComPtr<IDXGIDevice> dxgiDevice;ComPtr<IDXGIAdapter> adapter;DXGI_ADAPTER_DESC desc{};
        if(FAILED(d11_.As(&dxgiDevice)) || FAILED(dxgiDevice->GetAdapter(&adapter)) || FAILED(adapter->GetDesc(&desc))) {
            setFailure(st,PipelineStage::HostConnectFailed,0,L"Could not resolve source adapter LUID for the external NR host");
            return false;
        }
        adapterLuid_=desc.AdapterLuid;
        const auto sequence=gSessionSequence.fetch_add(1);
        names_=makeNrHostSessionNames(GetCurrentProcessId(),sequence);
        mapping_=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(hostipc::Shared),names_.map.c_str());
        if(!mapping_) { setFailure(st,PipelineStage::HostLaunchFailed,(int)GetLastError(),L"Could not create external NR host control mapping");return false; }
        shared_=static_cast<hostipc::Shared*>(MapViewOfFile(mapping_,FILE_MAP_ALL_ACCESS,0,0,sizeof(hostipc::Shared)));
        if(!shared_) { setFailure(st,PipelineStage::HostLaunchFailed,(int)GetLastError(),L"Could not map external NR host control block");shutdown();return false; }
        new(shared_) hostipc::Shared{};
        shared_->bridgePid=GetCurrentProcessId();shared_->adapterHigh=adapterLuid_.HighPart;shared_->adapterLow=adapterLuid_.LowPart;
        shared_->allowUnsupportedHardware=settings.attemptUnsupportedHardware?1u:0u;
        st.attemptUnsupportedHardware=shared_->allowUnsupportedHardware;
        wcsncpy_s(shared_->runtimePath,runtime.c_str(),_TRUNCATE);
        wcsncpy_s(shared_->fenceName,names_.fence.c_str(),_TRUNCATE);
        frameEvent_=CreateEventW(nullptr,FALSE,FALSE,names_.frameEvent.c_str());
        stopEvent_=CreateEventW(nullptr,TRUE,FALSE,names_.stopEvent.c_str());
        doneEvent_=CreateEventW(nullptr,FALSE,FALSE,names_.doneEvent.c_str());
        if(!frameEvent_ || !stopEvent_ || !doneEvent_) { setFailure(st,PipelineStage::HostLaunchFailed,(int)GetLastError(),L"Could not create external NR host events");shutdown();return false; }
        if(FAILED(d11v5_->CreateFence(0,D3D11_FENCE_FLAG_SHARED,IID_PPV_ARGS(&fence11_)))) {
            setFailure(st,PipelineStage::HostResourceShareFailed,0,L"Could not create bridge/host shared GPU fence");shutdown();return false;
        }
        HANDLE fenceHandle{};
        if(FAILED(fence11_->CreateSharedHandle(nullptr,GENERIC_ALL,nullptr,&fenceHandle)) || !fenceHandle) {
            setFailure(st,PipelineStage::HostResourceShareFailed,0,L"Could not create bridge/host GPU fence NT handle");shutdown();return false;
        }

        const auto hostExe=moduleDirectory()/L"UniversalDLSS5.NRHost.exe";
        if(!std::filesystem::is_regular_file(hostExe,ec)) {
            CloseHandle(fenceHandle);
            setFailure(st,PipelineStage::HostLaunchFailed,0,L"UniversalDLSS5.NRHost.exe is missing beside the bridge");shutdown();return false;
        }
        std::wstring command=L"\""+hostExe.wstring()+L"\" --session \""+names_.map+L"\"";
        STARTUPINFOW si{};si.cb=sizeof(si);PROCESS_INFORMATION pi{};
        if(!CreateProcessW(hostExe.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,moduleDirectory().c_str(),&si,&pi)) {
            CloseHandle(fenceHandle);
            setFailure(st,PipelineStage::HostLaunchFailed,(int)GetLastError(),L"Could not start UniversalDLSS5.NRHost.exe");shutdown();return false;
        }
        hostProcess_=pi.hProcess;hostThread_=pi.hThread;hostPid_=pi.dwProcessId;
        if(!duplicateIntoProcess(fenceHandle,hostProcess_,shared_->fenceHandle)) {
            const auto err=GetLastError();
            CloseHandle(fenceHandle);
            setFailure(st,PipelineStage::HostResourceShareFailed,(int)err,L"Could not duplicate the shared GPU fence handle into NRHost");shutdown();return false;
        }
        CloseHandle(fenceHandle);
        markPipelineStage(st.stageMask,PipelineStage::HostStarted);
        if(!waitForState(shared_,hostProcess_,hostipc::State::WaitingConfig,3000)) {
            const std::wstring msg=shared_->message[0]?shared_->message:L"External NR host did not complete startup handshake";
            setFailure(st,PipelineStage::HostConnectFailed,shared_->lastResult,msg);shutdown();return false;
        }
        markPipelineStage(st.stageMask,PipelineStage::HostConnected);
        initialized_=true;
        wcscpy_s(st.message,L"External NR host connected; GPU resources will be shared on the first frame");
        return true;
    }

    bool evaluate(ID3D11DeviceContext*,const FrameResources& frame,const Settings& settings,RuntimeStatus& st) override {
        st.neuralApi=NeuralExecutionApi::D3D12;
        if(!initialized_ || !shared_) return false;
        mergeHostStatus(st);
        shared_->frameReset=(frame.resetHistory||forceResetNext_)?1u:0u;
        shared_->depthInverted=frame.depthInverted?1u:0u;
        shared_->useControlMask=(settings.useControlMask&&frame.controlMask)?1u:0u;
        shared_->nrAutoMask=settings.nrAutoMask?1u:0u;
        shared_->nrUiCorrection=settings.nrUiCorrection?1u:0u;
        shared_->nrStyle=settings.nrStyle;
        shared_->nrPreset=settings.nrPreset;
        shared_->nrIntensity=settings.nrIntensity;
        shared_->nrTone=settings.nrTone;
        shared_->nrStructure=settings.nrStructure;
        shared_->nrSkinStructure=settings.nrSkinStructure;
        shared_->nrPaperWhite=settings.nrPaperWhite;
        shared_->nrTransferStrength=settings.nrTransferStrength;
        shared_->nrColorStrength=settings.nrColorStrength;
        shared_->motionScaleX=settings.motionScaleX*frame.motionScaleX;
        shared_->motionScaleY=settings.motionScaleY*frame.motionScaleY;

        // The model/preset hint participates in feature creation. Rebuild the
        // host feature when it changes instead of silently applying it only to
        // the next resolution/resource rebuild.
        if(lastPreset_!=settings.nrPreset){
            lastPreset_=settings.nrPreset;width_=height_=0;forceResetNext_=true;
        }

        // If a previous frame exceeded the short CPU enqueue budget, do not
        // overwrite its shared textures. Let NRHost finish in the background
        // while the game uses its local passthrough path. This caps Present-side
        // hitching and also prevents late Done events from desynchronizing frames.
        if(pendingSeq_!=0) {
            if(hostProcess_ && WaitForSingleObject(hostProcess_,0)==WAIT_OBJECT_0) {
                setFailure(st,PipelineStage::HostRuntimeFailed,0,L"External NR host exited while a neural frame was outstanding");
                return false;
            }
            const LONG published=InterlockedCompareExchange(&shared_->enqueuedSeq,0,0);
            const UINT64 gpuCompleted=fence11_?fence11_->GetCompletedValue():0;
            const ULONGLONG age=GetTickCount64()-pendingSince_;
            if(!nrHostSequenceEnqueued(published,pendingSeq_) || gpuCompleted<pendingOutputValue_) {
                if(age>kPendingWatchdogMs) {
                    setFailure(st,PipelineStage::HostRuntimeFailed,WAIT_TIMEOUT,L"External NR host did not retire the outstanding neural frame within 2 seconds");
                } else {
                    if(st.failureStage==PipelineStage::HostRuntimeFailed) st.failureStage=PipelineStage::None;
                    st.neuralActive=0;
                    wcscpy_s(st.message,L"NRHost warm-up/busy: using passthrough while the outstanding neural frame finishes");
                }
                return false;
            }
            pendingSeq_=0;pendingOutputValue_=0;pendingSince_=0;
            ResetEvent(doneEvent_); // discard any late wake associated with the retired frame
            if(st.failureStage==PipelineStage::HostRuntimeFailed) st.failureStage=PipelineStage::None;
        }

        if(!ensureResources(frame,st)) return false;
        const auto hostState=static_cast<hostipc::State>(InterlockedCompareExchange(&shared_->state,0,0));
        if(hostState!=hostipc::State::Ready && hostState!=hostipc::State::Active) {
            mergeHostStatus(st);
            if(hostState==hostipc::State::Error && st.failureStage==PipelineStage::None) st.failureStage=PipelineStage::HostRuntimeFailed;
            return false;
        }

        ctx11_->CopyResource(color_.Get(),frame.input);
        ctx11_->CopyResource(motion_.Get(),frame.motion);
        ctx11_->CopyResource(depth_.Get(),frame.depth);
        if(control_ && frame.controlMask) ctx11_->CopyResource(control_.Get(),frame.controlMask);
        const auto inputValue=fenceValue_+1;
        const auto outputValue=fenceValue_+2;
        fenceValue_=outputValue;
        if(FAILED(ctx11v4_->Signal(fence11_.Get(),inputValue))) {
            setFailure(st,PipelineStage::HostRuntimeFailed,0,L"D3D11 signal to external NR host failed");return false;
        }
        InterlockedExchange64(&shared_->inputFenceValue,(LONGLONG)inputValue);
        InterlockedExchange64(&shared_->outputFenceValue,(LONGLONG)outputValue);
        ResetEvent(doneEvent_);
        const LONG seq=InterlockedIncrement(&shared_->frameSeq);
        SetEvent(frameEvent_);

        const auto dispatch=waitForEnqueuedSequence(shared_,doneEvent_,hostProcess_,seq,kEnqueueBudgetMs);
        if(dispatch==EnqueueWaitResult::Timeout) {
            pendingSeq_=seq;pendingOutputValue_=outputValue;pendingSince_=GetTickCount64();forceResetNext_=settings.resetOnTemporalGap;
            if(st.failureStage==PipelineStage::HostRuntimeFailed) st.failureStage=PipelineStage::None;
            st.neuralActive=0;
            wcscpy_s(st.message,L"NRHost warm-up/busy: enqueue exceeded 12 ms; using passthrough until the host catches up");
            return false;
        }
        if(dispatch!=EnqueueWaitResult::Ready) {
            forceResetNext_=settings.resetOnTemporalGap;
            mergeHostStatus(st);
            if(dispatch==EnqueueWaitResult::HostExited)
                setFailure(st,PipelineStage::HostRuntimeFailed,0,L"External NR host exited before enqueueing the frame");
            else if(dispatch==EnqueueWaitResult::WaitFailed)
                setFailure(st,PipelineStage::HostRuntimeFailed,(int)GetLastError(),L"Waiting for external NR host enqueue publication failed");
            else if(st.failureStage==PipelineStage::None)
                setFailure(st,PipelineStage::HostRuntimeFailed,shared_->lastResult,L"External NR host entered an error state before enqueueing the frame");
            return false;
        }

        if(FAILED(ctx11v4_->Wait(fence11_.Get(),outputValue))) {
            setFailure(st,PipelineStage::HostRuntimeFailed,0,L"D3D11 GPU wait for external NR host failed");return false;
        }
        ctx11_->CopyResource(frame.output,output_.Get());
        mergeHostStatus(st);
        if(st.failureStage==PipelineStage::HostRuntimeFailed && shared_->failureStage==(UINT)PipelineStage::None)
            st.failureStage=PipelineStage::None;
        const LONG published=InterlockedCompareExchange(&shared_->enqueuedSeq,0,0);
        st.neuralActive=(nrHostSequenceEnqueued(published,seq) && shared_->lastResult==1 && static_cast<hostipc::Route>(shared_->route)!=hostipc::Route::None)?1u:0u;
        if(st.neuralActive) forceResetNext_=false;
        return true; // frame is GPU-queued; host guarantees passthrough output even if NR evaluation fails
    }

    void reset() override { forceResetNext_=true; }
    const wchar_t* name() const override { return L"External DLSS 5 NR Host"; }

private:
    void mergeHostStatus(RuntimeStatus& st) {
        if(!shared_) return;
        st.stageMask|=shared_->stageMask;
        const auto hostFailure=static_cast<PipelineStage>(shared_->failureStage);
        if(hostFailure!=PipelineStage::None) st.failureStage=hostFailure;
        st.lastResult=shared_->lastResult;
        st.attemptUnsupportedHardware=shared_->allowUnsupportedHardware;
        st.realGpuArchitecture=shared_->realGpuArchitecture;
        st.reportedGpuArchitecture=shared_->reportedGpuArchitecture;
        st.architectureCompatibilityActive=shared_->architectureCompatibilityActive;
        if(shared_->message[0]) wcsncpy_s(st.message,shared_->message,_TRUNCATE);
        const auto route=static_cast<hostipc::Route>(shared_->route);
        if(route==hostipc::Route::CoreDispatch) wcscpy_s(st.backendName,L"External NR Host (NGX core dispatch)");
        else if(route==hostipc::Route::SignedSnippet) wcscpy_s(st.backendName,L"External NR Host (signed snippet)");
    }

    bool makeSharedTexture(UINT w,UINT h,DXGI_FORMAT format,ComPtr<ID3D11Texture2D>& out,std::uint64_t& hostHandle,RuntimeStatus& st) {
        D3D11_TEXTURE2D_DESC d{};d.Width=w;d.Height=h;d.MipLevels=1;d.ArraySize=1;d.Format=format;d.SampleDesc.Count=1;
        d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;d.MiscFlags=D3D11_RESOURCE_MISC_SHARED_NTHANDLE|D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
        if(FAILED(d11_->CreateTexture2D(&d,nullptr,&out))) { setFailure(st,PipelineStage::HostResourceShareFailed,0,L"Could not create GPU-shareable external-host texture");return false; }
        ComPtr<IDXGIResource1> r;if(FAILED(out.As(&r))) { setFailure(st,PipelineStage::HostResourceShareFailed,0,L"External-host texture does not expose IDXGIResource1");return false; }
        HANDLE hnd{};const HRESULT hr=r->CreateSharedHandle(nullptr,DXGI_SHARED_RESOURCE_READ|DXGI_SHARED_RESOURCE_WRITE,nullptr,&hnd);
        if(FAILED(hr)||!hnd) { setFailure(st,PipelineStage::HostResourceShareFailed,(int)hr,L"Could not create external-host texture NT handle");return false; }
        const bool duplicated=duplicateIntoProcess(hnd,hostProcess_,hostHandle);
        const auto duplicateError=duplicated?ERROR_SUCCESS:GetLastError();
        CloseHandle(hnd);
        if(!duplicated) { setFailure(st,PipelineStage::HostResourceShareFailed,(int)duplicateError,L"Could not duplicate an external-host texture handle into NRHost");return false; }
        return true;
    }

    bool ensureResources(const FrameResources& f,RuntimeStatus& st) {
        if(width_==f.width && height_==f.height && color_ && output_ && motion_ && depth_ && control_) {
            markPipelineStage(st.stageMask,PipelineStage::HostResourcesShared);return true;
        }
        color_.Reset();output_.Reset();motion_.Reset();depth_.Reset();control_.Reset();width_=f.width;height_=f.height;forceResetNext_=true;
        const auto gen=++resourceGeneration_;
        const auto colorName=genName(names_.color,gen), outputName=genName(names_.output,gen), motionName=genName(names_.motion,gen), depthName=genName(names_.depth,gen), controlName=genName(names_.map+L".Control",gen);
        shared_->colorHandle=shared_->outputHandle=shared_->motionHandle=shared_->depthHandle=shared_->controlMaskHandle=0;
        if(!makeSharedTexture(f.width,f.height,f.inputFormat,color_,shared_->colorHandle,st) ||
           !makeSharedTexture(f.width,f.height,f.inputFormat,output_,shared_->outputHandle,st) ||
           !makeSharedTexture(f.width,f.height,f.motionFormat,motion_,shared_->motionHandle,st) ||
           !makeSharedTexture(f.width,f.height,f.depthFormat,depth_,shared_->depthHandle,st) ||
           !makeSharedTexture(f.width,f.height,f.controlMaskFormat,control_,shared_->controlMaskHandle,st)) return false;
        shared_->width=f.width;shared_->height=f.height;shared_->colorFormat=(std::uint32_t)f.inputFormat;shared_->motionFormat=(std::uint32_t)f.motionFormat;shared_->depthFormat=(std::uint32_t)f.depthFormat;shared_->controlMaskFormat=(std::uint32_t)f.controlMaskFormat;
        wcsncpy_s(shared_->colorName,colorName.c_str(),_TRUNCATE);wcsncpy_s(shared_->outputName,outputName.c_str(),_TRUNCATE);wcsncpy_s(shared_->motionName,motionName.c_str(),_TRUNCATE);wcsncpy_s(shared_->depthName,depthName.c_str(),_TRUNCATE);wcsncpy_s(shared_->controlMaskName,controlName.c_str(),_TRUNCATE);
        InterlockedExchange(&shared_->state,(LONG)hostipc::State::Configuring);
        InterlockedIncrement(&shared_->configGeneration);SetEvent(frameEvent_);
        if(!waitForState(shared_,hostProcess_,hostipc::State::Ready,5000)) {
            mergeHostStatus(st);if(st.failureStage==PipelineStage::None)st.failureStage=PipelineStage::HostRuntimeFailed;return false;
        }
        markPipelineStage(st.stageMask,PipelineStage::HostResourcesShared);mergeHostStatus(st);return true;
    }

    void shutdown() {
        if(shared_) { InterlockedExchange(&shared_->stopRequested,1);SetEvent(stopEvent_);SetEvent(frameEvent_); }
        if(hostProcess_) { WaitForSingleObject(hostProcess_,1000);CloseHandle(hostProcess_);hostProcess_=nullptr; }
        if(hostThread_) { CloseHandle(hostThread_);hostThread_=nullptr; }
        if(shared_) { UnmapViewOfFile(shared_);shared_=nullptr; }
        if(mapping_) { CloseHandle(mapping_);mapping_=nullptr; }
        if(frameEvent_) { CloseHandle(frameEvent_);frameEvent_=nullptr; }
        if(stopEvent_) { CloseHandle(stopEvent_);stopEvent_=nullptr; }
        if(doneEvent_) { CloseHandle(doneEvent_);doneEvent_=nullptr; }
        fence11_.Reset();d11v5_.Reset();ctx11v4_.Reset();color_.Reset();output_.Reset();motion_.Reset();depth_.Reset();control_.Reset();ctx11_.Reset();d11_.Reset();
        initialized_=false;width_=height_=0;resourceGeneration_=0;fenceValue_=0;hostPid_=0;pendingSeq_=0;pendingOutputValue_=0;pendingSince_=0;lastPreset_=~0u;runtime_.clear();
    }

    ComPtr<ID3D11Device> d11_;ComPtr<ID3D11DeviceContext> ctx11_;ComPtr<ID3D11Device5> d11v5_;ComPtr<ID3D11DeviceContext4> ctx11v4_;ComPtr<ID3D11Fence> fence11_;
    ComPtr<ID3D11Texture2D> color_,output_,motion_,depth_,control_;
    hostipc::Shared* shared_{};HANDLE mapping_{},frameEvent_{},stopEvent_{},doneEvent_{},hostProcess_{},hostThread_{};
    static constexpr DWORD kEnqueueBudgetMs=12;
    static constexpr ULONGLONG kPendingWatchdogMs=2000;
    NrHostSessionNames names_{};LUID adapterLuid_{};std::wstring runtime_;UINT width_{},height_{};std::uint32_t resourceGeneration_{};std::uint64_t fenceValue_{};DWORD hostPid_{};bool initialized_{};
    LONG pendingSeq_{};UINT64 pendingOutputValue_{};ULONGLONG pendingSince_{};bool forceResetNext_{true};std::uint32_t lastPreset_{~0u};
};

Backend* createExternalHostNR(){ return new ExternalHostNR; }
}
