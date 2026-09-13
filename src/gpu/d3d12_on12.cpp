#include "d3d12_on12.hpp"
#include "d3d12_resource_tracker.hpp"
#include "game_temporal_guides.hpp"
#include "../bridge/attach_logger.hpp"
#include "udlss/guide_candidate_policy.hpp"
#include <vector>
#include <algorithm>

using Microsoft::WRL::ComPtr;
namespace udlss::gpu {
namespace {
struct FrameWrappedGuide {
    ComPtr<ID3D12Resource> native;
    ComPtr<ID3D11Texture2D> wrapped;
};

bool wrapReadOnlyGuide(ID3D11On12Device* on12,ID3D12Resource* resource,D3D12_RESOURCE_STATES state,FrameWrappedGuide& out){
    if(!on12||!resource)return false;
    out.native=resource;
    D3D11_RESOURCE_FLAGS flags{};flags.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    const HRESULT hr=on12->CreateWrappedResource(resource,&flags,state,state,IID_PPV_ARGS(&out.wrapped));
    return SUCCEEDED(hr)&&out.wrapped;
}
D3D12_RESOURCE_BARRIER transition(ID3D12Resource* r,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after){
    D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition.pResource=r;b.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;b.Transition.StateBefore=before;b.Transition.StateAfter=after;return b;
}
}

D3D12On12Pipeline::~D3D12On12Pipeline(){
    reset();
    if(copyFenceEvent_){CloseHandle(copyFenceEvent_);copyFenceEvent_=nullptr;}
}

bool D3D12On12Pipeline::ensureCopyInfrastructure(RuntimeStatus& st){
    if(copyFence_&&preCopy_.allocator&&preCopy_.list&&postCopy_.allocator&&postCopy_.list&&copyFenceEvent_)return true;
    if(!d12_)return false;
    auto makeContext=[&](CopyContext& c){
        if(FAILED(d12_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&c.allocator))))return false;
        if(FAILED(d12_->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,c.allocator.Get(),nullptr,IID_PPV_ARGS(&c.list))))return false;
        return SUCCEEDED(c.list->Close());
    };
    if(!makeContext(preCopy_)||!makeContext(postCopy_)||FAILED(d12_->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&copyFence_)))){
        wcscpy_s(st.message,L"Could not create D3D12 staging-copy command context");return false;
    }
    copyFenceEvent_=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    if(!copyFenceEvent_){wcscpy_s(st.message,L"Could not create D3D12 staging-copy fence event");return false;}
    return true;
}

bool D3D12On12Pipeline::initialize(ID3D12CommandQueue*q,const std::wstring&dir,RuntimeStatus&st){
    if(!q||q->GetDesc().Type!=D3D12_COMMAND_LIST_TYPE_DIRECT)return false;queue_=q;moduleDir_=dir;
    if(FAILED(q->GetDevice(IID_PPV_ARGS(&d12_)))){wcscpy_s(st.message,L"Could not obtain D3D12 device from proven presentation queue");return false;}
    IUnknown* queues[]={q};D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_12_1,D3D_FEATURE_LEVEL_12_0,D3D_FEATURE_LEVEL_11_1,D3D_FEATURE_LEVEL_11_0};D3D_FEATURE_LEVEL chosen{};
    HRESULT hr=D3D11On12CreateDevice(d12_.Get(),D3D11_CREATE_DEVICE_BGRA_SUPPORT,levels,_countof(levels),queues,1,0,&d11_,&ctx_,&chosen);
    if(FAILED(hr)||FAILED(d11_.As(&on12_))){wcscpy_s(st.message,L"D3D11On12CreateDevice failed on proven presentation queue");return false;}
    if(!ensureCopyInfrastructure(st))return false;
    pipeline_.setNativeD3D12(d12_.Get(),queue_.Get());return pipeline_.initialize(d11_.Get(),ctx_.Get(),dir,st);
}

void D3D12On12Pipeline::reset(){
    if(copyFence_&&copyFenceEvent_){const auto pending=std::max(preCopy_.fenceValue,postCopy_.fenceValue);if(pending&&copyFence_->GetCompletedValue()<pending&&SUCCEEDED(copyFence_->SetEventOnCompletion(pending,copyFenceEvent_)))WaitForSingleObject(copyFenceEvent_,5000);}
    pipeline_.reset();ownedColor11_.Reset();ownedColor12_.Reset();width_=height_=0;format_=DXGI_FORMAT_UNKNOWN;preCopy_.fenceValue=postCopy_.fenceValue=0;
}
void D3D12On12Pipeline::retryNeural(){pipeline_.retryNeural();}

bool D3D12On12Pipeline::waitCopyContext(CopyContext& c,RuntimeStatus& st){
    if(!c.fenceValue||copyFence_->GetCompletedValue()>=c.fenceValue)return true;
    if(FAILED(copyFence_->SetEventOnCompletion(c.fenceValue,copyFenceEvent_))){wcscpy_s(st.message,L"D3D12 staging-copy fence wait setup failed");return false;}
    const DWORD wait=WaitForSingleObject(copyFenceEvent_,2000);
    if(wait!=WAIT_OBJECT_0){wcscpy_s(st.message,L"D3D12 staging-copy fence timed out; bypassing frame");return false;}
    return true;
}

bool D3D12On12Pipeline::ensureOwnedColor(const D3D12_RESOURCE_DESC& bbDesc,RuntimeStatus& st){
    if(ownedColor12_&&width_==bbDesc.Width&&height_==bbDesc.Height&&format_==bbDesc.Format)return true;
    ownedColor11_.Reset();ownedColor12_.Reset();pipeline_.reset();
    D3D12_RESOURCE_DESC d=bbDesc;d.Flags|=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;d.Flags&=~D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;heap.CPUPageProperty=D3D12_CPU_PAGE_PROPERTY_UNKNOWN;heap.MemoryPoolPreference=D3D12_MEMORY_POOL_UNKNOWN;heap.CreationNodeMask=1;heap.VisibleNodeMask=1;
    if(FAILED(d12_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&ownedColor12_)))){
        wcscpy_s(st.message,L"Could not create injector-owned D3D12 staging color");return false;
    }
    D3D11_RESOURCE_FLAGS flags{};flags.BindFlags=D3D11_BIND_RENDER_TARGET;
    if(FAILED(on12_->CreateWrappedResource(ownedColor12_.Get(),&flags,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COMMON,IID_PPV_ARGS(&ownedColor11_)))){
        ownedColor12_.Reset();wcscpy_s(st.message,L"Could not wrap injector-owned D3D12 staging color");return false;
    }
    width_=(UINT)bbDesc.Width;height_=bbDesc.Height;format_=bbDesc.Format;
    return true;
}

bool D3D12On12Pipeline::submitBackbufferCopy(ID3D12Resource* bb,bool intoOwned,RuntimeStatus& st){
    if(!bb||!ownedColor12_||!queue_||!copyFence_)return false;
    CopyContext& c=intoOwned?preCopy_:postCopy_;
    if(!waitCopyContext(c,st))return false;
    if(FAILED(c.allocator->Reset())||FAILED(c.list->Reset(c.allocator.Get(),nullptr))){wcscpy_s(st.message,L"Could not reset D3D12 staging-copy command list");return false;}
    if(intoOwned){
        D3D12_RESOURCE_BARRIER b[]={transition(bb,D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_COPY_SOURCE),transition(ownedColor12_.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST)};
        c.list->ResourceBarrier(2,b);c.list->CopyResource(ownedColor12_.Get(),bb);
        D3D12_RESOURCE_BARRIER r[]={transition(bb,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_PRESENT),transition(ownedColor12_.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COMMON)};c.list->ResourceBarrier(2,r);
    }else{
        D3D12_RESOURCE_BARRIER b[]={transition(ownedColor12_.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_SOURCE),transition(bb,D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_COPY_DEST)};
        c.list->ResourceBarrier(2,b);c.list->CopyResource(bb,ownedColor12_.Get());
        D3D12_RESOURCE_BARRIER r[]={transition(ownedColor12_.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COMMON),transition(bb,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PRESENT)};c.list->ResourceBarrier(2,r);
    }
    if(FAILED(c.list->Close())){wcscpy_s(st.message,L"Could not close D3D12 staging-copy command list");return false;}
    ID3D12CommandList* lists[]={c.list.Get()};queue_->ExecuteCommandLists(1,lists);c.fenceValue=nextCopyFenceValue_++;
    if(FAILED(queue_->Signal(copyFence_.Get(),c.fenceValue))){wcscpy_s(st.message,L"Could not signal D3D12 staging-copy fence");return false;}
    return true;
}

bool D3D12On12Pipeline::process(IDXGISwapChain*swap,const Settings&s,const std::wstring&runtime,RuntimeStatus&st,const SwapchainColorContext* colorContext){
    if(!swap||!on12_||!s.allowD3D11On12)return false;
    bridge::setCrashStage(bridge::CrashStage::BackbufferAcquire);
    ComPtr<IDXGISwapChain3> s3;if(FAILED(swap->QueryInterface(IID_PPV_ARGS(&s3))))return false;
    const UINT i=s3->GetCurrentBackBufferIndex();
    ComPtr<ID3D12Resource> bb;if(FAILED(swap->GetBuffer(i,IID_PPV_ARGS(&bb))))return false;
    const auto desc=bb->GetDesc();
    const bool lifecycleChanged=width_ && (width_!=desc.Width || height_!=desc.Height || format_!=desc.Format);
    if(lifecycleChanged)reset();
    if(!ensureOwnedColor(desc,st))return false;

    bridge::setCrashStage(bridge::CrashStage::PreCopy);
    if(!submitBackbufferCopy(bb.Get(),true,st))return false;

    // Prefer exact game-supplied temporal inputs (Streamline/NGX) over
    // heuristic D3D12 tracking. The generic tracker is currently auto-used for
    // depth only; motion remains diagnostics-only until its convention is known.
    const auto diagnosticMotionCandidate=globalD3D12ResourceTracker().bestMotionCandidate(d12_.Get(),width_,height_);
    GuideProbeResult externalGuide{};
    FrameWrappedGuide depthWrap{},motionWrap{};
    const auto captured=snapshotGameTemporalGuides(d12_.Get(),GetTickCount64());
    // Never import a transient game-owned D3D12 temporal resource at Present
    // unless its provider explicitly guarantees that the resource remains valid
    // through Present. NGX Evaluate inputs are often only valid around the
    // evaluation call and engines may alias/reuse them immediately afterwards.
    // Observing them is still useful for diagnostics, but wrapping such a
    // resource later through D3D11On12 can corrupt the game's resource lifetime.
    const bool capturedPresentSafe=captured&&captured.validUntilPresent;
    if(captured&&!capturedPresentSafe){
        std::wstring provider=captured.provider.empty()?L"Game temporal guide":captured.provider;
        provider+=L" (observed, lifetime not Present-safe)";
        wcsncpy_s(st.guideAdapterName,provider.c_str(),_TRUNCATE);
        if(captured.motion12){st.guideMotionWidth=captured.motionWidth;st.guideMotionHeight=captured.motionHeight;st.guideMotionConfidence=captured.confidence;}
        if(captured.depth12){st.guideDepthWidth=captured.depthWidth;st.guideDepthHeight=captured.depthHeight;st.guideDepthConfidence=captured.confidence;}
        wcsncpy_s(st.guideFields,L"D3D12 temporal inputs detected; deferred because provider lifetime ends before Present",_TRUNCATE);
    }
    if(capturedPresentSafe){
        externalGuide.source=captured.source;externalGuide.provider=captured.provider;externalGuide.cameraCut=captured.cameraCut;
        if(s.useGameDepth&&captured.depth12&&guideExtentAspectCompatible(captured.depthWidth,captured.depthHeight,width_,height_)){
            auto state=globalD3D12ResourceTracker().currentState(captured.depth12.Get(),captured.depthState);
            if(wrapReadOnlyGuide(on12_.Get(),captured.depth12.Get(),state,depthWrap)){
                externalGuide.depth=depthWrap.wrapped;externalGuide.depthViewFormat=captured.depthFormat;externalGuide.depthInverted=captured.depthInverted;externalGuide.depthConventionKnown=captured.depthConventionKnown;externalGuide.depthWidth=captured.depthWidth;externalGuide.depthHeight=captured.depthHeight;externalGuide.depthConfidence=captured.confidence;
            }
        }
        if(s.motionSource==MotionSource::Auto&&captured.motion12&&captured.motionConventionKnown&&guideExtentAspectCompatible(captured.motionWidth,captured.motionHeight,width_,height_)){
            auto state=globalD3D12ResourceTracker().currentState(captured.motion12.Get(),captured.motionState);
            if(wrapReadOnlyGuide(on12_.Get(),captured.motion12.Get(),state,motionWrap)){
                externalGuide.motion=motionWrap.wrapped;externalGuide.motionConventionValid=true;externalGuide.motionEncoding=NativeMotionEncoding::PixelCurrentToPrevious;externalGuide.motionScaleX=captured.motionToPixelScaleX;externalGuide.motionScaleY=captured.motionToPixelScaleY;externalGuide.motionWidth=captured.motionWidth;externalGuide.motionHeight=captured.motionHeight;externalGuide.motionConfidence=captured.confidence;
            }
        }
    }
    if(!externalGuide.depth&&s.useGameDepth){
        const auto tracked=globalD3D12ResourceTracker().bestDepthCandidate(d12_.Get(),width_,height_);
        if(tracked.resource&&wrapReadOnlyGuide(on12_.Get(),tracked.resource.Get(),tracked.state,depthWrap)){
            externalGuide.depth=depthWrap.wrapped;externalGuide.depthViewFormat=tracked.viewFormat;externalGuide.depthInverted=tracked.inverted;externalGuide.depthConventionKnown=tracked.conventionKnown;externalGuide.depthWidth=(UINT)tracked.resource->GetDesc().Width;externalGuide.depthHeight=tracked.resource->GetDesc().Height;externalGuide.depthConfidence=tracked.confidence;externalGuide.source=GameGuideSource::D3D12Tracker;externalGuide.provider=L"D3D12 temporal resource tracker";
        }
    }

    std::vector<ID3D11Resource*> acquired;acquired.reserve(3);acquired.push_back(ownedColor11_.Get());
    if(depthWrap.wrapped)acquired.push_back(depthWrap.wrapped.Get());
    if(motionWrap.wrapped&&motionWrap.wrapped.Get()!=depthWrap.wrapped.Get())acquired.push_back(motionWrap.wrapped.Get());
    bridge::setCrashStage(bridge::CrashStage::On12Acquire);
    on12_->AcquireWrappedResources(acquired.data(),(UINT)acquired.size());
    bridge::setCrashStage(bridge::CrashStage::NeuralProcess);
    const bool ok=pipeline_.process(ownedColor11_.Get(),s,runtime,st,(externalGuide.depth||externalGuide.motion)?&externalGuide:nullptr,colorContext);
    if(diagnosticMotionCandidate.score>st.nativeMotionCandidateScore){st.nativeMotionCandidateId=diagnosticMotionCandidate.stableId;st.nativeMotionCandidateScore=diagnosticMotionCandidate.score;}
    bridge::setCrashStage(bridge::CrashStage::On12Release);
    on12_->ReleaseWrappedResources(acquired.data(),(UINT)acquired.size());
    ctx_->Flush();
    bridge::setCrashStage(bridge::CrashStage::PostCopy);
    if(!submitBackbufferCopy(bb.Get(),false,st))return false;
    bridge::setCrashStage(bridge::CrashStage::ProcessComplete);
    return ok;
}
}
