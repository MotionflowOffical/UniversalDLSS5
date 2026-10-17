#include "backend.hpp"
#include "external_host_protocol.hpp"
#include "external_host_wait.hpp"
#include "udlss/external_host_policy.hpp"
#include <windows.h>
#include <d3d11_4.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <filesystem>
#include <string>
#include <atomic>
#include <algorithm>
#include <new>
#include <array>

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

struct TransferBatch {
    HANDLE process;hostipc::Shared* shared;bool published{};
    ~TransferBatch(){if(published)return;std::uint64_t* fields[]={&shared->colorHandle,&shared->outputHandle,&shared->motionHandle,&shared->depthHandle,&shared->controlMaskHandle};
        for(auto* value:fields){if(!*value)continue;HANDLE local{};
            if(DuplicateHandle(process,reinterpret_cast<HANDLE>((uintptr_t)*value),GetCurrentProcess(),&local,0,FALSE,DUPLICATE_CLOSE_SOURCE|DUPLICATE_SAME_ACCESS))CloseHandle(local);
            *value=0;
        }
    }
};

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
        native12_=!init.d3d11Device && init.d3d12Device && init.d3d12Queue;
        if(!native12_ && (!init.d3d11Device || !init.d3d11Context)) {
            setFailure(st,PipelineStage::HostConnectFailed,0,L"External NR host requires the D3D11 guide device/context");
            return false;
        }
        const auto runtimeDll=std::filesystem::path(runtime)/L"nvngx_dlssnr.dll";
        std::error_code ec;
        if(!std::filesystem::is_regular_file(runtimeDll,ec)) {
            setFailure(st,PipelineStage::HostConnectFailed,0,L"nvngx_dlssnr.dll not found in the selected runtime folder");
            return false;
        }
        runtime_=runtime;lastPreset_=settings.nrPreset;
        d12_=init.d3d12Device;queue12_=init.d3d12Queue;
        if(native12_){
            d12_=init.d3d12Device;queue12_=init.d3d12Queue;adapterLuid_=d12_->GetAdapterLuid();
        }else{
            d11_=init.d3d11Device;ctx11_=init.d3d11Context;
            if(FAILED(d11_.As(&d11v5_)) || FAILED(ctx11_.As(&ctx11v4_))){
                setFailure(st,PipelineStage::HostConnectFailed,0,L"External NR host requires D3D11 shared-fence support");return false;
            }
            ComPtr<IDXGIDevice> dxgiDevice;ComPtr<IDXGIAdapter> adapter;DXGI_ADAPTER_DESC desc{};
            if(FAILED(d11_.As(&dxgiDevice)) || FAILED(dxgiDevice->GetAdapter(&adapter)) || FAILED(adapter->GetDesc(&desc))){
                setFailure(st,PipelineStage::HostConnectFailed,0,L"Could not resolve source adapter LUID");return false;
            }
            adapterLuid_=desc.AdapterLuid;
        }
        const auto sequence=gSessionSequence.fetch_add(1);
        names_=makeNrHostSessionNames(GetCurrentProcessId(),sequence);
        mapping_=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(hostipc::Shared),names_.map.c_str());
        if(!mapping_) { setFailure(st,PipelineStage::HostLaunchFailed,(int)GetLastError(),L"Could not create external NR host control mapping");return false; }
        shared_=static_cast<hostipc::Shared*>(MapViewOfFile(mapping_,FILE_MAP_ALL_ACCESS,0,0,sizeof(hostipc::Shared)));
        if(!shared_) { setFailure(st,PipelineStage::HostLaunchFailed,(int)GetLastError(),L"Could not map external NR host control block");shutdown();return false; }
        new(shared_) hostipc::Shared{};
        shared_->bridgePid=GetCurrentProcessId();shared_->sourceApi=(std::uint32_t)st.api;shared_->rendererRoute=(std::uint32_t)st.rendererRoute;shared_->bridgeArchitectureBits=sizeof(void*)*8u;shared_->canonicalColorSpace=st.swapchainColorSpace;shared_->adapterHigh=adapterLuid_.HighPart;shared_->adapterLow=adapterLuid_.LowPart;
        shared_->allowUnsupportedHardware=settings.attemptUnsupportedHardware?1u:0u;
        st.attemptUnsupportedHardware=shared_->allowUnsupportedHardware;
        wcsncpy_s(shared_->runtimePath,runtime.c_str(),_TRUNCATE);
        wcsncpy_s(shared_->fenceName,names_.fence.c_str(),_TRUNCATE);
        frameEvent_=CreateEventW(nullptr,FALSE,FALSE,names_.frameEvent.c_str());
        stopEvent_=CreateEventW(nullptr,TRUE,FALSE,names_.stopEvent.c_str());
        doneEvent_=CreateEventW(nullptr,FALSE,FALSE,names_.doneEvent.c_str());
        outputFenceEvent_=CreateEventW(nullptr,FALSE,FALSE,nullptr);
        if(!frameEvent_ || !stopEvent_ || !doneEvent_ || !outputFenceEvent_) { setFailure(st,PipelineStage::HostLaunchFailed,(int)GetLastError(),L"Could not create external NR host events");shutdown();return false; }
        const HRESULT fenceHr=native12_?d12_->CreateFence(0,D3D12_FENCE_FLAG_SHARED,IID_PPV_ARGS(&fence12_)):d11v5_->CreateFence(0,D3D11_FENCE_FLAG_SHARED,IID_PPV_ARGS(&fence11_));
        if(FAILED(fenceHr)) {
            setFailure(st,PipelineStage::HostResourceShareFailed,0,L"Could not create bridge/host shared GPU fence");shutdown();return false;
        }
        HANDLE fenceHandle{};
        const HRESULT handleHr=native12_?d12_->CreateSharedHandle(fence12_.Get(),nullptr,GENERIC_ALL,nullptr,&fenceHandle):fence11_->CreateSharedHandle(nullptr,GENERIC_ALL,nullptr,&fenceHandle);
        if(FAILED(handleHr) || !fenceHandle) {
            setFailure(st,PipelineStage::HostResourceShareFailed,0,L"Could not create bridge/host GPU fence NT handle");shutdown();return false;
        }

        if(!native12_&&d12_&&queue12_){
            if(FAILED(d12_->OpenSharedHandle(fenceHandle,IID_PPV_ARGS(&fence12_)))){
                CloseHandle(fenceHandle);setFailure(st,PipelineStage::HostResourceShareFailed,0,L"Could not open the common D3D11/D3D12 host fence");shutdown();return false;
            }
        }

        const auto hostExe=moduleDirectory()/L"UniversalDLSS5.NRHost.exe";
        if(!std::filesystem::is_regular_file(hostExe,ec)) {
            CloseHandle(fenceHandle);
            setFailure(st,PipelineStage::HostLaunchFailed,0,L"UniversalDLSS5.NRHost.exe is missing beside the bridge");shutdown();return false;
        }
        std::wstring command=L"\""+hostExe.wstring()+L"\" --session \""+names_.map+L"\"";
        STARTUPINFOW si{};si.cb=sizeof(si);si.dwFlags=STARTF_USESHOWWINDOW;si.wShowWindow=SW_HIDE;PROCESS_INFORMATION pi{};
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
        st.framePacingMode=static_cast<std::uint32_t>(settings.framePacing);
        st.neuralOutputAgeFrames=0;st.neuralOutputAgeMs=0.0f;st.pacingWaitMs=0.0f;
        st.neuralPassesRequested=std::clamp<std::uint32_t>(settings.nrPasses,1,4);
        if(!initialized_ || !shared_) return false;
        const bool requestedNative=frame.input12!=nullptr;
        if((requestedNative&&(!d12_||!queue12_||!fence12_))||(!requestedNative&&(!d11_||!ctx11_))){setFailure(st,PipelineStage::HostConnectFailed,0,L"Requested producer API is unavailable in this host session");return false;}
        native12_=requestedNative;
        const auto evaluationStart=GetTickCount64();
        mergeHostStatus(st);
        // Keep published transfers owned by an in-flight configuration even
        // when our CPU wait expired before the host's queue-drain deadline.
        // A failed/stopping host cannot accept new batches. Explicit retry
        // creates a fresh session after local GPU retirement.
        const auto entryHostState=static_cast<hostipc::State>(InterlockedCompareExchange(&shared_->state,0,0));
        if(entryHostState==hostipc::State::Configuring){
            st.neuralActive=0;wcscpy_s(st.message,L"NRHost configuration still in flight; preserving published transfers");return false;
        }
        if(entryHostState==hostipc::State::Error||entryHostState==hostipc::State::Stopping){
            st.neuralActive=0;return false;
        }

        // The model/preset hint participates in feature creation. Rebuild the
        // host feature when it changes instead of silently applying it only to
        // the next resolution/resource rebuild.
        if(lastPreset_!=settings.nrPreset){
            lastPreset_=settings.nrPreset;configDirty_=true;forceResetNext_=true;
        }

        // Never overwrite shared textures while an older exact-sequence request
        // is still using them. In Synchronized mode, however, actively drain that
        // request instead of turning every subsequent Present into passthrough.
        // This matters when the foreground game temporarily receives more WDDM
        // scheduling time than the separate NRHost process.
        if(pendingSeq_!=0) {
            if(hostProcess_ && WaitForSingleObject(hostProcess_,0)==WAIT_OBJECT_0) {
                setFailure(st,PipelineStage::HostRuntimeFailed,0,L"External NR host exited while a neural frame was outstanding");
                return false;
            }
            LONG completed=InterlockedCompareExchange(&shared_->completedSeq,0,0);
            UINT64 gpuCompleted=fence11_?completedFence():0;
            ULONGLONG age=GetTickCount64()-pendingSince_;
            bool pendingReady=nrHostOutputReady(completed,pendingSeq_,gpuCompleted,pendingOutputValue_);
            if(!pendingReady && settings.framePacing==FramePacingMode::Synchronized){
                const auto drainStart=GetTickCount64();
                const auto drainBudget=std::min(nrHostPendingWaitBudgetMs(settings.framePacing,age),nrHostRemainingWaitBudgetMs(settings.framePacing,GetTickCount64()-evaluationStart));
                const auto drain=waitOutput(pendingSeq_,pendingOutputValue_,drainBudget);
                st.pacingWaitMs=static_cast<float>(GetTickCount64()-drainStart);
                pendingReady=drain==OutputWaitResult::Ready;
                if(!pendingReady && drain==OutputWaitResult::HostExited){setFailure(st,PipelineStage::HostRuntimeFailed,0,L"External NR host exited while draining the outstanding neural frame");return false;}
                if(!pendingReady && drain==OutputWaitResult::WaitFailed){setFailure(st,PipelineStage::HostRuntimeFailed,(int)GetLastError(),L"Waiting for the outstanding NRHost frame failed");return false;}
                age=GetTickCount64()-pendingSince_;
            }
            if(!pendingReady) {
                if(age>=kPendingWatchdogMs) {
                    setFailure(st,PipelineStage::HostRuntimeFailed,WAIT_TIMEOUT,L"External NR host did not retire the outstanding neural frame within 2 seconds");
                } else {
                    if(st.failureStage==PipelineStage::HostRuntimeFailed) st.failureStage=PipelineStage::None;
                    st.neuralActive=0;
                    wchar_t busy[384]{};swprintf_s(busy,L"NRHost busy: age=%llums seq=%ld done=%ld enq=%ld GPU=%llu need=%llu; host: %ls",age,pendingSeq_,completed,shared_->enqueuedSeq,gpuCompleted,pendingOutputValue_,shared_->message);wcsncpy_s(st.message,busy,_TRUNCATE);
                }
                return false;
            }
            pendingSeq_=0;pendingOutputValue_=0;pendingSince_=0;
            ResetEvent(doneEvent_); // discard any late wake associated with the retired frame
            if(st.failureStage==PipelineStage::HostRuntimeFailed) st.failureStage=PipelineStage::None;
        }

        shared_->temporal=frame.temporal;
        shared_->frameReset=(frame.resetHistory||forceResetNext_)?1u:0u;
        shared_->depthInverted=frame.depthInverted?1u:0u;
        shared_->useControlMask=(settings.useControlMask&&(frame.controlMask||frame.controlMask12))?1u:0u;
        shared_->nrAutoMask=settings.nrAutoMask?1u:0u;
        shared_->nrUiCorrection=settings.nrUiCorrection?1u:0u;
        shared_->nrStyle=settings.nrStyle;
        shared_->nrPreset=settings.nrPreset;
        shared_->nrPassesRequested=st.neuralPassesRequested;
        shared_->nrPassesExecuted=0;
        shared_->nrIntensity=settings.nrIntensity;
        shared_->nrTone=settings.nrTone;
        shared_->nrStructure=settings.nrStructure;
        shared_->nrSkinStructure=settings.nrSkinStructure;
        shared_->nrPaperWhite=settings.nrPaperWhite;
        shared_->nrTransferStrength=settings.nrTransferStrength;
        shared_->nrColorStrength=settings.nrColorStrength;
        shared_->motionScaleX=frame.motionScaleX;
        shared_->motionScaleY=frame.motionScaleY;

        if(!ensureResources(frame,st)) return false;
        const auto hostState=static_cast<hostipc::State>(InterlockedCompareExchange(&shared_->state,0,0));
        if(hostState!=hostipc::State::Ready && hostState!=hostipc::State::Active) {
            mergeHostStatus(st);
            if(hostState==hostipc::State::Error && st.failureStage==PipelineStage::None) st.failureStage=PipelineStage::HostRuntimeFailed;
            return false;
        }

        if(!native12_){
        ctx11_->CopyResource(color_.Get(),frame.input);
        ctx11_->CopyResource(motion_.Get(),frame.motion);
        ctx11_->CopyResource(depth_.Get(),frame.depth);
        if(control_ && frame.controlMask) ctx11_->CopyResource(control_.Get(),frame.controlMask);
        }
        const auto inputValue=fenceValue_+1;
        const auto outputValue=fenceValue_+2;
        fenceValue_=outputValue;
        const HRESULT signalHr=native12_?queue12_->Signal(fence12_.Get(),inputValue):ctx11v4_->Signal(fence11_.Get(),inputValue);
        if(FAILED(signalHr)) {
            setFailure(st,PipelineStage::HostRuntimeFailed,0,L"D3D11 signal to external NR host failed");return false;
        }
        // The host queue waits on this fence from another D3D API/process.  As
        // with the proven in-process path, explicitly flush the D3D11 producer
        // so the fence signal is submitted before NRHost begins waiting on it.
        if(!native12_)ctx11_->Flush();
        InterlockedExchange64(&shared_->inputFenceValue,(LONGLONG)inputValue);
        InterlockedExchange64(&shared_->outputFenceValue,(LONGLONG)outputValue);
        ResetEvent(doneEvent_);
        const LONG seq=InterlockedIncrement(&shared_->frameSeq);
        SetEvent(frameEvent_);

        // Treat enqueue + exact-output completion as one pacing operation. The
        // old hard 12 ms enqueue timeout fired before Synchronized mode's total
        // current-frame budget and produced a visible NR/passthrough alternation
        // on games such as Cyberpunk even when the result arrived shortly after.
        const auto pacingWaitStart=GetTickCount64();
        const auto frameWaitBudget=nrHostRemainingWaitBudgetMs(settings.framePacing,GetTickCount64()-evaluationStart);
        const auto dispatch=waitForEnqueuedSequence(shared_,doneEvent_,hostProcess_,seq,frameWaitBudget);
        if(dispatch==EnqueueWaitResult::Timeout) {
            pendingSeq_=seq;pendingOutputValue_=outputValue;pendingSince_=pacingWaitStart;forceResetNext_=settings.resetOnTemporalGap;
            if(st.failureStage==PipelineStage::HostRuntimeFailed) st.failureStage=PipelineStage::None;
            st.pacingWaitMs+=static_cast<float>(GetTickCount64()-pacingWaitStart);
            st.neuralActive=0;
            wcscpy_s(st.message,L"NRHost warm-up/busy: enqueue exceeded the frame-pacing budget; current frame passed through");
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

        const auto elapsedBeforeOutput=GetTickCount64()-evaluationStart;
        const auto remainingBudget=nrHostRemainingWaitBudgetMs(settings.framePacing,elapsedBeforeOutput);
        const auto outputWait=waitOutput(seq,outputValue,remainingBudget);
        st.pacingWaitMs+=static_cast<float>(GetTickCount64()-pacingWaitStart);
        if(outputWait!=OutputWaitResult::Ready){
            pendingSeq_=seq;pendingOutputValue_=outputValue;pendingSince_=pacingWaitStart;forceResetNext_=settings.resetOnTemporalGap;
            st.neuralActive=0;
            if(outputWait==OutputWaitResult::HostExited)setFailure(st,PipelineStage::HostRuntimeFailed,0,L"External NR host exited before the requested frame completed");
            else if(outputWait==OutputWaitResult::WaitFailed)setFailure(st,PipelineStage::HostRuntimeFailed,(int)GetLastError(),L"Waiting for the exact NRHost output fence failed");
            else if(outputWait==OutputWaitResult::HostError){mergeHostStatus(st);if(st.failureStage==PipelineStage::None)setFailure(st,PipelineStage::HostRuntimeFailed,shared_->lastResult,L"External NR host entered an error state before output completion");}
            else if(outputWait==OutputWaitResult::SequenceMismatch)setFailure(st,PipelineStage::HostRuntimeFailed,0,L"NRHost completed a different frame sequence; stale output rejected");
            else {if(st.failureStage==PipelineStage::HostRuntimeFailed)st.failureStage=PipelineStage::None;swprintf_s(st.message,L"NRHost budget missed: seq=%ld done=%ld enq=%ld GPU=%llu in=%llu need=%llu; host: %ls",seq,shared_->completedSeq,shared_->enqueuedSeq,completedFence(),inputValue,outputValue,shared_->message);}
            return false;
        }
        // The shared fence now proves that output_ contains this exact request.
        // No queued cross-API wait is needed, and a late/older result can never
        // enter the current-frame post composite.
        if(!native12_)ctx11_->CopyResource(frame.output,output_.Get());
        mergeHostStatus(st);
        if(st.failureStage==PipelineStage::HostRuntimeFailed && shared_->failureStage==(UINT)PipelineStage::None)
            st.failureStage=PipelineStage::None;
        const LONG completed=InterlockedCompareExchange(&shared_->completedSeq,0,0);
        const UINT64 gpuCompleted=completedFence();
        st.neuralActive=(nrHostOutputReady(completed,seq,gpuCompleted,outputValue) && shared_->lastResult==1 && static_cast<hostipc::Route>(shared_->route)!=hostipc::Route::None)?1u:0u;
        if(st.neuralActive) ++st.externalHostFrames;
        if(st.neuralActive) forceResetNext_=false;
        return st.neuralActive!=0;
    }

    bool canWriteInputs() const override {
        if(!pendingSeq_)return true;
        return shared_&&nrHostOutputReady(InterlockedCompareExchange(&shared_->completedSeq,0,0),pendingSeq_,completedFence(),pendingOutputValue_);
    }
    void reset() override { forceResetNext_=true; }
    const wchar_t* name() const override { return L"External DLSS 5 NR Host"; }

private:
    UINT64 completedFence() const {return native12_?fence12_->GetCompletedValue():fence11_->GetCompletedValue();}
    OutputWaitResult waitOutput(LONG seq,UINT64 value,DWORD ms){
        return native12_?waitForExactOutput(shared_,fence12_.Get(),outputFenceEvent_,hostProcess_,seq,value,ms,doneEvent_):
            waitForExactOutput(shared_,fence11_.Get(),outputFenceEvent_,hostProcess_,seq,value,ms,doneEvent_);
    }
    void mergeHostStatus(RuntimeStatus& st) {
        if(!shared_) return;
        st.lastGpuMs=shared_->lastGpuMs;
        st.stageMask|=shared_->stageMask;
        const auto hostFailure=static_cast<PipelineStage>(shared_->failureStage);
        if(hostFailure!=PipelineStage::None) st.failureStage=hostFailure;
        st.lastResult=shared_->lastResult;
        st.attemptUnsupportedHardware=shared_->allowUnsupportedHardware;
        st.realGpuArchitecture=shared_->realGpuArchitecture;
        st.reportedGpuArchitecture=shared_->reportedGpuArchitecture;
        st.architectureCompatibilityActive=shared_->architectureCompatibilityActive;
        st.neuralPassesExecuted=shared_->nrPassesExecuted;
        if(shared_->message[0]) wcsncpy_s(st.message,shared_->message,_TRUNCATE);
        const auto route=static_cast<hostipc::Route>(shared_->route);
        if(route==hostipc::Route::CoreDispatch) wcscpy_s(st.backendName,L"External NR Host (NGX core dispatch)");
        else if(route==hostipc::Route::SignedSnippet) wcscpy_s(st.backendName,L"External NR Host (signed snippet)");
    }

    bool makeSharedTexture(UINT w,UINT h,DXGI_FORMAT format,ComPtr<ID3D11Texture2D>& out,std::uint64_t& hostHandle,RuntimeStatus& st) {
        D3D11_TEXTURE2D_DESC d{};d.Width=w;d.Height=h;d.MipLevels=1;d.ArraySize=1;d.Format=format;d.SampleDesc.Count=1;
        d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;d.MiscFlags=D3D11_RESOURCE_MISC_SHARED|D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
        if(FAILED(d11_->CreateTexture2D(&d,nullptr,&out))) { setFailure(st,PipelineStage::HostResourceShareFailed,0,L"Could not create GPU-shareable external-host texture");return false; }
        ComPtr<IDXGIResource1> r;if(FAILED(out.As(&r))) { setFailure(st,PipelineStage::HostResourceShareFailed,0,L"External-host texture does not expose IDXGIResource1");return false; }
        HANDLE hnd{};const HRESULT hr=r->CreateSharedHandle(nullptr,GENERIC_ALL,nullptr,&hnd);
        if(FAILED(hr)||!hnd) { setFailure(st,PipelineStage::HostResourceShareFailed,(int)hr,L"Could not create external-host texture NT handle");return false; }
        const bool duplicated=duplicateIntoProcess(hnd,hostProcess_,hostHandle);
        const auto duplicateError=duplicated?ERROR_SUCCESS:GetLastError();
        CloseHandle(hnd);
        if(!duplicated) { setFailure(st,PipelineStage::HostResourceShareFailed,(int)duplicateError,L"Could not duplicate an external-host texture handle into NRHost");return false; }
        return true;
    }

    bool ensureResources(const FrameResources& f,RuntimeStatus& st) {
        const bool controlNeeded=f.controlMask||f.controlMask12;
        const bool sameFormat=shared_->colorFormat==(UINT)f.inputFormat&&shared_->motionFormat==(UINT)f.motionFormat&&shared_->depthFormat==(UINT)f.depthFormat&&shared_->controlMaskFormat==(UINT)f.controlMaskFormat;
        bool resourcesMatch=resourcesReady_&&resourceNative12_==native12_&&width_==f.width&&height_==f.height&&sameFormat;
        if(native12_)resourcesMatch=resourcesMatch&&direct_[0].Get()==f.input12&&direct_[1].Get()==f.output12&&direct_[2].Get()==f.motion12&&direct_[3].Get()==f.depth12&&direct_[4].Get()==f.controlMask12;
        else resourcesMatch=resourcesMatch&&color_&&output_&&motion_&&depth_&&(bool(control_)==controlNeeded);
        if(resourcesMatch&&!configDirty_){markPipelineStage(st.stageMask,PipelineStage::HostResourcesShared);return true;}
        if(resourcesMatch && configDirty_){
            InterlockedExchange(&shared_->state,(LONG)hostipc::State::Configuring);
            InterlockedIncrement(&shared_->configGeneration);SetEvent(frameEvent_);
            if(!waitForState(shared_,hostProcess_,hostipc::State::Ready,5000)){mergeHostStatus(st);return false;}
            configDirty_=false;return true;
        }
        resourcesReady_=false;
        width_=f.width;height_=f.height;forceResetNext_=true;
        const auto gen=++resourceGeneration_;
        const auto colorName=genName(names_.color,gen),outputName=genName(names_.output,gen),motionName=genName(names_.motion,gen),depthName=genName(names_.depth,gen),controlName=genName(names_.map+L".Control",gen);
        shared_->colorHandle=shared_->outputHandle=shared_->motionHandle=shared_->depthHandle=shared_->controlMaskHandle=0;
        TransferBatch transfer{hostProcess_,shared_};
        if(native12_){
            ID3D12Resource* resources[]={f.input12,f.output12,f.motion12,f.depth12,f.controlMask12};
            std::uint64_t* handles[]={&shared_->colorHandle,&shared_->outputHandle,&shared_->motionHandle,&shared_->depthHandle,&shared_->controlMaskHandle};
            for(unsigned i=0;i<5;i++){
                direct_[i]=resources[i];if(i==4&&!resources[i])continue;
                HANDLE h{};if(!resources[i]||FAILED(d12_->CreateSharedHandle(resources[i],nullptr,GENERIC_ALL,nullptr,&h))){setFailure(st,PipelineStage::HostResourceShareFailed,0,L"Native D3D12 host resources must be injector-owned shared textures");return false;}
                bool ok=duplicateIntoProcess(h,hostProcess_,*handles[i]);CloseHandle(h);
                if(!ok){setFailure(st,PipelineStage::HostResourceShareFailed,GetLastError(),L"Could not transfer native D3D12 resource handle");return false;}
            }
        }else{
            color_.Reset();output_.Reset();motion_.Reset();depth_.Reset();control_.Reset();
            if(!makeSharedTexture(f.width,f.height,f.inputFormat,color_,shared_->colorHandle,st)||
               !makeSharedTexture(f.width,f.height,f.inputFormat,output_,shared_->outputHandle,st)||
               !makeSharedTexture(f.width,f.height,f.motionFormat,motion_,shared_->motionHandle,st)||
               !makeSharedTexture(f.width,f.height,f.depthFormat,depth_,shared_->depthHandle,st)||
               (controlNeeded&&!makeSharedTexture(f.width,f.height,f.controlMaskFormat,control_,shared_->controlMaskHandle,st)))return false;
        }
        shared_->width=f.width;shared_->height=f.height;shared_->colorFormat=(std::uint32_t)f.inputFormat;shared_->motionFormat=(std::uint32_t)f.motionFormat;shared_->depthFormat=(std::uint32_t)f.depthFormat;shared_->controlMaskFormat=(std::uint32_t)f.controlMaskFormat;
        wcsncpy_s(shared_->colorName,colorName.c_str(),_TRUNCATE);wcsncpy_s(shared_->outputName,outputName.c_str(),_TRUNCATE);wcsncpy_s(shared_->motionName,motionName.c_str(),_TRUNCATE);wcsncpy_s(shared_->depthName,depthName.c_str(),_TRUNCATE);wcsncpy_s(shared_->controlMaskName,controlName.c_str(),_TRUNCATE);
        InterlockedExchange(&shared_->state,(LONG)hostipc::State::Configuring);
        transfer.published=true;++shared_->resourceGeneration;InterlockedIncrement(&shared_->configGeneration);SetEvent(frameEvent_);
        if(!waitForState(shared_,hostProcess_,hostipc::State::Ready,5000)) {
            mergeHostStatus(st);if(st.failureStage==PipelineStage::None)st.failureStage=PipelineStage::HostRuntimeFailed;return false;
        }
        resourceNative12_=native12_;resourcesReady_=true;configDirty_=false;
        if(native12_){color_.Reset();output_.Reset();motion_.Reset();depth_.Reset();control_.Reset();}
        else for(auto&r:direct_)r.Reset();
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
        if(outputFenceEvent_) { CloseHandle(outputFenceEvent_);outputFenceEvent_=nullptr; }
        fence11_.Reset();d11v5_.Reset();ctx11v4_.Reset();color_.Reset();output_.Reset();motion_.Reset();depth_.Reset();control_.Reset();ctx11_.Reset();d11_.Reset();
        for(auto& r:direct_)r.Reset();fence12_.Reset();queue12_.Reset();d12_.Reset();native12_=false;configDirty_=false;resourcesReady_=false;
        initialized_=false;width_=height_=0;resourceGeneration_=0;fenceValue_=0;hostPid_=0;pendingSeq_=0;pendingOutputValue_=0;pendingSince_=0;lastPreset_=~0u;runtime_.clear();
    }

    bool native12_{},resourceNative12_{},configDirty_{},resourcesReady_{};ComPtr<ID3D12Device> d12_;ComPtr<ID3D12CommandQueue> queue12_;ComPtr<ID3D12Fence> fence12_;std::array<ComPtr<ID3D12Resource>,5> direct_{};
    ComPtr<ID3D11Device> d11_;ComPtr<ID3D11DeviceContext> ctx11_;ComPtr<ID3D11Device5> d11v5_;ComPtr<ID3D11DeviceContext4> ctx11v4_;ComPtr<ID3D11Fence> fence11_;
    ComPtr<ID3D11Texture2D> color_,output_,motion_,depth_,control_;
    hostipc::Shared* shared_{};HANDLE mapping_{},frameEvent_{},stopEvent_{},doneEvent_{},outputFenceEvent_{},hostProcess_{},hostThread_{};
    static constexpr ULONGLONG kPendingWatchdogMs=kNrHostWatchdogMs;
    NrHostSessionNames names_{};LUID adapterLuid_{};std::wstring runtime_;UINT width_{},height_{};std::uint32_t resourceGeneration_{};std::uint64_t fenceValue_{};DWORD hostPid_{};bool initialized_{};
    LONG pendingSeq_{};UINT64 pendingOutputValue_{};ULONGLONG pendingSince_{};bool forceResetNext_{true};std::uint32_t lastPreset_{~0u};
};

Backend* createExternalHostNR(){ return new ExternalHostNR; }
}
