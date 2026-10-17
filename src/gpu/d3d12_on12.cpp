#include "d3d12_on12.hpp"
#include "shared_guide_texture.hpp"
#include "d3d12_resource_tracker.hpp"
#include "game_temporal_guides.hpp"
#include "../bridge/attach_logger.hpp"
#include "udlss/guide_candidate_policy.hpp"
#include "udlss/provider_snapshot_policy.hpp"
#include "udlss/ngxnr_guide_contract.hpp"
#include <array>
#include <algorithm>
#include <cstring>

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
std::uintptr_t queueIdentity(ID3D12CommandQueue* q){
    if(!q)return 0;
    ComPtr<IUnknown> id;
    if(FAILED(q->QueryInterface(IID_PPV_ARGS(&id))))return reinterpret_cast<std::uintptr_t>(q);
    return reinterpret_cast<std::uintptr_t>(id.Get());
}


// The vendor NGX NR contract names are documented by the independent
// reconstruction in research/ngxnr. These are *not* NVIDIA public ABI claims.
static_assert(udlss::ngxnr::isProviderDepthFormat(39));
static_assert(udlss::ngxnr::isProviderMotionFormat(10));
D3D12_RESOURCE_BARRIER transition(ID3D12Resource* r,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after){
    D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition.pResource=r;b.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;b.Transition.StateBefore=before;b.Transition.StateAfter=after;return b;
}

DXGI_FORMAT recoveryGuideResourceFormat(DXGI_FORMAT viewFormat,bool depth){
    if(!depth){
        switch(viewFormat){
        case DXGI_FORMAT_R16G16_FLOAT:
        case DXGI_FORMAT_R16G16_SNORM:
        case DXGI_FORMAT_R32G32_FLOAT:
        case DXGI_FORMAT_R16G16B16A16_FLOAT:
        case DXGI_FORMAT_R16G16B16A16_SNORM:
        case DXGI_FORMAT_R32G32B32A32_FLOAT:return viewFormat;
        default:return DXGI_FORMAT_UNKNOWN;
        }
    }
    switch(viewFormat){
    case DXGI_FORMAT_D32_FLOAT: case DXGI_FORMAT_R32_FLOAT: case DXGI_FORMAT_R32_TYPELESS:return DXGI_FORMAT_R32_TYPELESS;
    case DXGI_FORMAT_D24_UNORM_S8_UINT: case DXGI_FORMAT_R24G8_TYPELESS: case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:return DXGI_FORMAT_R24G8_TYPELESS;
    case DXGI_FORMAT_D16_UNORM: case DXGI_FORMAT_R16_TYPELESS: case DXGI_FORMAT_R16_UNORM:return DXGI_FORMAT_R16_TYPELESS;
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT: case DXGI_FORMAT_R32G8X24_TYPELESS: case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:return DXGI_FORMAT_R32G8X24_TYPELESS;
    default:return DXGI_FORMAT_UNKNOWN;
    }
}

DXGI_FORMAT recoveryGuideInteropFormat(DXGI_FORMAT viewFormat,bool depth){
    if(!depth)return recoveryGuideResourceFormat(viewFormat,false);
    // Prefer a typed D3D11-friendly representative for depth families.
    // Cyberpunk tags R32_TYPELESS depth; standalone D3D11 opening of a
    // D3D12-created R32_TYPELESS shared texture is driver-dependent, while
    // R32_FLOAT is copy-compatible with the same typeless family.
    switch(viewFormat){
    case DXGI_FORMAT_D32_FLOAT: case DXGI_FORMAT_R32_FLOAT: case DXGI_FORMAT_R32_TYPELESS:return DXGI_FORMAT_R32_FLOAT;
    case DXGI_FORMAT_D16_UNORM: case DXGI_FORMAT_R16_TYPELESS: case DXGI_FORMAT_R16_UNORM:return DXGI_FORMAT_R16_UNORM;
    case DXGI_FORMAT_D24_UNORM_S8_UINT: case DXGI_FORMAT_R24G8_TYPELESS: case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:return DXGI_FORMAT_R24G8_TYPELESS;
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT: case DXGI_FORMAT_R32G8X24_TYPELESS: case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:return DXGI_FORMAT_R32G8X24_TYPELESS;
    default:return DXGI_FORMAT_UNKNOWN;
    }
}
}

D3D12On12Pipeline::~D3D12On12Pipeline(){
    reset();
    if(copyFenceEvent_){CloseHandle(copyFenceEvent_);copyFenceEvent_=nullptr;}
    if(recoveryD11FenceEvent_){CloseHandle(recoveryD11FenceEvent_);recoveryD11FenceEvent_=nullptr;}
}

bool D3D12On12Pipeline::ensureCopyInfrastructure(RuntimeStatus& st){
    if(copyFence_&&preCopy_.allocator&&preCopy_.list&&postCopy_.allocator&&postCopy_.list&&guideCopy_.allocator&&guideCopy_.list&&copyFenceEvent_)return true;
    if(!d12_)return false;
    auto makeContext=[&](CopyContext& c){
        if(FAILED(d12_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&c.allocator))))return false;
        if(FAILED(d12_->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,c.allocator.Get(),nullptr,IID_PPV_ARGS(&c.list))))return false;
        return SUCCEEDED(c.list->Close());
    };
    if(!makeContext(preCopy_)||!makeContext(postCopy_)||!makeContext(guideCopy_)||FAILED(d12_->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&copyFence_)))){
        wcscpy_s(st.message,L"Could not create D3D12 staging-copy command context");return false;
    }
    copyFenceEvent_=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    if(!copyFenceEvent_){wcscpy_s(st.message,L"Could not create D3D12 staging-copy fence event");return false;}
    return true;
}

bool D3D12On12Pipeline::initializeRecoveryD3D11(RuntimeStatus& st){
    if(!d12_)return false;
    ComPtr<IDXGIFactory4> factory;
    if(FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))){wcscpy_s(st.message,L"Could not create DXGI factory for D3D12 recovery interop");return false;}
    const auto wanted=d12_->GetAdapterLuid();
    ComPtr<IDXGIAdapter1> adapter;
    for(UINT i=0;;++i){
        ComPtr<IDXGIAdapter1> candidate;
        if(factory->EnumAdapters1(i,&candidate)==DXGI_ERROR_NOT_FOUND)break;
        DXGI_ADAPTER_DESC1 desc{};candidate->GetDesc1(&desc);
        if(desc.AdapterLuid.HighPart==wanted.HighPart&&desc.AdapterLuid.LowPart==wanted.LowPart){adapter=candidate;break;}
    }
    if(!adapter){wcscpy_s(st.message,L"Could not resolve the D3D12 adapter for recovery D3D11 interop");return false;}
    D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_11_1,D3D_FEATURE_LEVEL_11_0};D3D_FEATURE_LEVEL chosen{};
    const UINT flags=D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    if(FAILED(D3D11CreateDevice(adapter.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,flags,levels,_countof(levels),D3D11_SDK_VERSION,&d11_,&chosen,&ctx_))){
        wcscpy_s(st.message,L"Could not create same-adapter D3D11 recovery device");return false;
    }
    if(FAILED(d11_.As(&recoveryD11Device1_))||FAILED(d11_.As(&recoveryD11Device5_))||FAILED(ctx_.As(&recoveryD11Context4_))){
        wcscpy_s(st.message,L"Recovery D3D11 device lacks NT-handle/fence interop support");return false;
    }
    if(FAILED(recoveryD11Device5_->CreateFence(0,D3D11_FENCE_FLAG_NONE,IID_PPV_ARGS(&recoveryD11Fence_)))){
        wcscpy_s(st.message,L"Could not create recovery D3D11 completion fence");return false;
    }
    recoveryD11FenceEvent_=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    if(!recoveryD11FenceEvent_){wcscpy_s(st.message,L"Could not create recovery D3D11 fence event");return false;}
    return true;
}

bool D3D12On12Pipeline::initialize(ID3D12CommandQueue*q,const std::wstring&dir,RuntimeStatus&st,bool recoveryMode){
    if(!q||q->GetDesc().Type!=D3D12_COMMAND_LIST_TYPE_DIRECT)return false;moduleDir_=dir;recoveryMode_=recoveryMode;queue_=q;recoveryQueueId_=queueIdentity(q);
    if(FAILED(q->GetDevice(IID_PPV_ARGS(&d12_)))){wcscpy_s(st.message,L"Could not obtain D3D12 device from renderer queue");return false;}
    if(recoveryMode_){
        if(!initializeRecoveryD3D11(st))return false;
        wcsncpy_s(st.interopName,L"same-queue shared D3D12/D3D11 recovery + x64 NRHost",_TRUNCATE);
    }else{
        IUnknown* queues[]={queue_.Get()};D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_12_1,D3D_FEATURE_LEVEL_12_0,D3D_FEATURE_LEVEL_11_1,D3D_FEATURE_LEVEL_11_0};D3D_FEATURE_LEVEL chosen{};
        const HRESULT hr=D3D11On12CreateDevice(d12_.Get(),D3D11_CREATE_DEVICE_BGRA_SUPPORT,levels,_countof(levels),queues,1,0,&d11_,&ctx_,&chosen);
        if(FAILED(hr)||FAILED(d11_.As(&on12_))){wcscpy_s(st.message,L"D3D11On12CreateDevice failed on proven presentation queue");return false;}
    }
    if(!ensureCopyInfrastructure(st))return false;
    pipeline_.setForceExternalHost(recoveryMode_);
    pipeline_.setExternalHostD3D12(d12_.Get(),queue_.Get());
    if(!recoveryMode_)pipeline_.setNativeD3D12(d12_.Get(),queue_.Get());
    return pipeline_.initialize(d11_.Get(),ctx_.Get(),dir,st);
}

void D3D12On12Pipeline::reset(){
    nativePipeline_.reset();
    if(copyFence_&&copyFenceEvent_){const auto pending=std::max({preCopy_.fenceValue,postCopy_.fenceValue,guideCopy_.fenceValue});if(pending&&copyFence_->GetCompletedValue()<pending&&SUCCEEDED(copyFence_->SetEventOnCompletion(pending,copyFenceEvent_)))WaitForSingleObject(copyFenceEvent_,5000);}
    pipeline_.reset();ownedColor11_.Reset();ownedColor12_.Reset();
    recoveryDepthGuide_.texture11.Reset();recoveryDepthGuide_.resource12.Reset();recoveryDepthGuide_={};
    recoveryMotionGuide_.texture11.Reset();recoveryMotionGuide_.resource12.Reset();recoveryMotionGuide_={};
    width_=height_=0;format_=DXGI_FORMAT_UNKNOWN;preCopy_.fenceValue=postCopy_.fenceValue=guideCopy_.fenceValue=0;recoveryD11PendingValue_=0;
}
void D3D12On12Pipeline::retryNeural(){pipeline_.retryNeural();nativePipeline_.retryNeural();}

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
    D3D12_RESOURCE_DESC d=bbDesc;d.Flags|=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;d.Flags&=~(D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL|D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE);
    const UINT nodeMask=queue_&&queue_->GetDesc().NodeMask?queue_->GetDesc().NodeMask:1u;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;heap.CPUPageProperty=D3D12_CPU_PAGE_PROPERTY_UNKNOWN;heap.MemoryPoolPreference=D3D12_MEMORY_POOL_UNKNOWN;heap.CreationNodeMask=nodeMask;heap.VisibleNodeMask=nodeMask;
    const auto heapFlags=recoveryMode_?D3D12_HEAP_FLAG_SHARED:D3D12_HEAP_FLAG_NONE;
    if(FAILED(d12_->CreateCommittedResource(&heap,heapFlags,&d,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&ownedColor12_)))){
        wcscpy_s(st.message,L"Could not create injector-owned D3D12 staging color");return false;
    }
    if(recoveryMode_){
        HANDLE shared{};
        const HRESULT shareHr=d12_->CreateSharedHandle(ownedColor12_.Get(),nullptr,GENERIC_ALL,nullptr,&shared);
        if(FAILED(shareHr)||!shared){ownedColor12_.Reset();wcscpy_s(st.message,L"Could not create shared D3D12 recovery-color handle");return false;}
        const HRESULT openHr=recoveryD11Device1_->OpenSharedResource1(shared,IID_PPV_ARGS(&ownedColor11_));
        CloseHandle(shared);
        if(FAILED(openHr)||!ownedColor11_){ownedColor12_.Reset();wcscpy_s(st.message,L"Could not open D3D12 recovery color on the standard D3D11 device");return false;}
    }else{
        D3D11_RESOURCE_FLAGS flags{};flags.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        if(FAILED(on12_->CreateWrappedResource(ownedColor12_.Get(),&flags,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COMMON,IID_PPV_ARGS(&ownedColor11_)))){
            ownedColor12_.Reset();wcscpy_s(st.message,L"Could not wrap injector-owned D3D12 staging color");return false;
        }
    }
    width_=(UINT)bbDesc.Width;height_=bbDesc.Height;format_=bbDesc.Format;
    return true;
}

bool D3D12On12Pipeline::waitRecoveryD3D11(RuntimeStatus& st){
    if(!recoveryMode_)return true;
    if(!recoveryD11Context4_||!recoveryD11Fence_||!recoveryD11FenceEvent_)return false;
    if(recoveryD11PendingValue_ && recoveryD11Fence_->GetCompletedValue()<recoveryD11PendingValue_){
        if(FAILED(recoveryD11Fence_->SetEventOnCompletion(recoveryD11PendingValue_,recoveryD11FenceEvent_)) ||
           WaitForSingleObject(recoveryD11FenceEvent_,50)!=WAIT_OBJECT_0){
            st.failureStage=PipelineStage::None;st.neuralActive=0;
            wcscpy_s(st.message,L"Recovery D3D11 frame still in flight; bypassing this frame safely");
            return false;
        }
    }
    recoveryD11PendingValue_=0;
    const auto value=recoveryD11FenceValue_++;
    if(FAILED(recoveryD11Context4_->Signal(recoveryD11Fence_.Get(),value))){wcscpy_s(st.message,L"Could not signal recovery D3D11 completion fence");return false;}
    ctx_->Flush();
    recoveryD11PendingValue_=value;
    if(recoveryD11Fence_->GetCompletedValue()>=value){recoveryD11PendingValue_=0;return true;}
    if(FAILED(recoveryD11Fence_->SetEventOnCompletion(value,recoveryD11FenceEvent_))){wcscpy_s(st.message,L"Could not arm recovery D3D11 completion event");return false;}
    if(WaitForSingleObject(recoveryD11FenceEvent_,50)!=WAIT_OBJECT_0){
        st.failureStage=PipelineStage::None;st.neuralActive=0;
        wcscpy_s(st.message,L"Recovery D3D11 frame still in flight; bypassing this frame safely");
        return false;
    }
    recoveryD11PendingValue_=0;
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

bool D3D12On12Pipeline::openOwnedSnapshotDirect(ID3D12Resource* source,DXGI_FORMAT viewFormat,bool depth,RecoveryGuideSurface& surface,RuntimeStatus& st){
    if(!recoveryMode_||!source||!recoveryD11Device1_||!d12_)return false;
    const auto src=source->GetDesc();
    if(src.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||src.SampleDesc.Count!=1||src.MipLevels!=1)return false;
    const auto effectiveView=viewFormat!=DXGI_FORMAT_UNKNOWN?viewFormat:src.Format;
    if(recoveryGuideResourceFormat(effectiveView,depth)==DXGI_FORMAT_UNKNOWN){surface.lastInteropHr=E_INVALIDARG;return false;}
    surface.failureOperation=2;surface.candidateAttempts=1;surface.triedFormat=src.Format;surface.interopPath=1;
    HANDLE shared{};const auto shareHr=d12_->CreateSharedHandle(source,nullptr,GENERIC_ALL,nullptr,&shared);
    if(FAILED(shareHr)||!shared){surface.lastInteropHr=FAILED(shareHr)?shareHr:E_FAIL;return false;}
    surface.failureOperation=3;
    ComPtr<ID3D11Texture2D> opened;const auto openHr=recoveryD11Device1_->OpenSharedResource1(shared,IID_PPV_ARGS(&opened));CloseHandle(shared);
    if(FAILED(openHr)||!opened){surface.lastInteropHr=FAILED(openHr)?openHr:E_FAIL;return false;}
    D3D11_TEXTURE2D_DESC od{};opened->GetDesc(&od);
    if(od.Width!=(UINT)src.Width||od.Height!=src.Height||od.SampleDesc.Count!=1){surface.failureOperation=4;surface.lastInteropHr=E_FAIL;return false;}
    surface.failureOperation=0;surface.texture11=std::move(opened);surface.resource12=source;surface.width=od.Width;surface.height=od.Height;
    surface.resourceFormat=src.Format;surface.viewFormat=effectiveView;surface.directOwned=true;surface.lastInteropHr=S_OK;surface.interopPath=1;
    return true;
}

bool D3D12On12Pipeline::stageRecoveryGuide(ID3D12Resource* source,D3D12_RESOURCE_STATES sourceState,DXGI_FORMAT viewFormat,bool depth,RecoveryGuideSurface& surface,RuntimeStatus& st){
    if(!recoveryMode_||!source||!recoveryD11Device1_||!d12_||!queue_||!copyFence_)return false;
    const auto src=source->GetDesc();
    if(src.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||src.SampleDesc.Count!=1||src.MipLevels!=1)return false;
    const auto effectiveView=viewFormat!=DXGI_FORMAT_UNKNOWN?viewFormat:src.Format;
    const auto resourceFormat=recoveryGuideResourceFormat(effectiveView,depth);
    const auto interopFormat=recoveryGuideInteropFormat(effectiveView,depth);
    if(resourceFormat==DXGI_FORMAT_UNKNOWN||interopFormat==DXGI_FORMAT_UNKNOWN){surface.lastInteropHr=E_INVALIDARG;return false;}
    const UINT w=(UINT)src.Width,h=src.Height;
    const bool recreate=surface.directOwned||!surface.resource12||!surface.texture11||surface.width!=w||surface.height!=h||surface.resourceFormat!=interopFormat||surface.viewFormat!=effectiveView;
    if(recreate){
        if(!waitCopyContext(guideCopy_,st))return false;
        surface.texture11.Reset();surface.resource12.Reset();surface.width=surface.height=0;
        surface.resourceFormat=surface.viewFormat=DXGI_FORMAT_UNKNOWN;
        surface.directOwned=false;surface.interopPath=2;surface.lastInteropHr=S_OK;
        surface.failureOperation=0;surface.candidateAttempts=0;surface.triedFormat=DXGI_FORMAT_UNKNOWN;

        // D3D12-created, D3D11-opened textures are constrained by the
        // device's cross-API shared-resource tier. This is not the same as
        // the ordinary D3D12 typed-copy compatibility check. Try the typed
        // SRV family first, then a compatible typeless family, and try
        // simultaneous-access only as a last resort. No game-owned texture
        // state is changed during these *allocation* probes.
        DXGI_FORMAT options[2]{interopFormat,DXGI_FORMAT_UNKNOWN};
        if(depth && interopFormat!=resourceFormat) options[1]=resourceFormat;
        const UINT nodeMask=queue_->GetDesc().NodeMask?queue_->GetDesc().NodeMask:1u;
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
        heap.CreationNodeMask=nodeMask;heap.VisibleNodeMask=nodeMask;
        for(unsigned candidate=0;candidate<4 && !surface.texture11;++candidate){
            const DXGI_FORMAT format=options[candidate/2];
            if(format==DXGI_FORMAT_UNKNOWN)continue;
            D3D12_RESOURCE_DESC d=src;d.Format=format;
            d.Flags=sharedGuideTextureFlags(format,candidate%2!=0);
            d.MipLevels=1;d.SampleDesc.Count=1;d.SampleDesc.Quality=0;
            // Do not carry an engine-specific GPU layout into an NT-shared
            // texture; a standard UNKNOWN layout is required here.
            d.Layout=D3D12_TEXTURE_LAYOUT_UNKNOWN;
            ++surface.candidateAttempts;surface.triedFormat=format;
            ComPtr<ID3D12Resource> probe12;
            const HRESULT createHr=d12_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_SHARED,&d,
                D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&probe12));
            if(FAILED(createHr)||!probe12){surface.failureOperation=1;surface.lastInteropHr=FAILED(createHr)?createHr:E_FAIL;continue;}
            HANDLE shared{};
            const HRESULT shareHr=d12_->CreateSharedHandle(probe12.Get(),nullptr,GENERIC_ALL,nullptr,&shared);
            if(FAILED(shareHr)||!shared){surface.failureOperation=2;surface.lastInteropHr=FAILED(shareHr)?shareHr:E_FAIL;continue;}
            ComPtr<ID3D11Texture2D> probe11;
            const HRESULT openHr=recoveryD11Device1_->OpenSharedResource1(shared,IID_PPV_ARGS(&probe11));
            CloseHandle(shared);
            if(FAILED(openHr)||!probe11){surface.failureOperation=3;surface.lastInteropHr=FAILED(openHr)?openHr:E_FAIL;continue;}
            D3D11_TEXTURE2D_DESC od{};probe11->GetDesc(&od);
            if(od.Width!=w||od.Height!=h||od.SampleDesc.Count!=1){surface.failureOperation=4;surface.lastInteropHr=E_FAIL;continue;}
            surface.resource12=std::move(probe12);surface.texture11=std::move(probe11);
            surface.width=w;surface.height=h;surface.resourceFormat=format;
            surface.viewFormat=effectiveView;surface.lastInteropHr=S_OK;surface.failureOperation=0;
        }
        if(!surface.texture11)return false;
    }
    if(!waitCopyContext(guideCopy_,st)){surface.failureOperation=5;return false;}
    if(FAILED(guideCopy_.allocator->Reset())||FAILED(guideCopy_.list->Reset(guideCopy_.allocator.Get(),nullptr))){surface.failureOperation=5;surface.lastInteropHr=E_FAIL;return false;}
    std::array<D3D12_RESOURCE_BARRIER,4> barriers{};UINT count=0;
    if(sourceState!=D3D12_RESOURCE_STATE_COPY_SOURCE)barriers[count++]=transition(source,sourceState,D3D12_RESOURCE_STATE_COPY_SOURCE);
    barriers[count++]=transition(surface.resource12.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST);
    guideCopy_.list->ResourceBarrier(count,barriers.data());guideCopy_.list->CopyResource(surface.resource12.Get(),source);
    count=0;
    barriers[count++]=transition(surface.resource12.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COMMON);
    if(sourceState!=D3D12_RESOURCE_STATE_COPY_SOURCE)barriers[count++]=transition(source,D3D12_RESOURCE_STATE_COPY_SOURCE,sourceState);
    guideCopy_.list->ResourceBarrier(count,barriers.data());
    const HRESULT closeHr=guideCopy_.list->Close();if(FAILED(closeHr)){surface.failureOperation=5;surface.lastInteropHr=closeHr;return false;}
    ID3D12CommandList* lists[]={guideCopy_.list.Get()};queue_->ExecuteCommandLists(1,lists);guideCopy_.fenceValue=nextCopyFenceValue_++;
    const HRESULT signalHr=queue_->Signal(copyFence_.Get(),guideCopy_.fenceValue);if(FAILED(signalHr)){surface.failureOperation=5;surface.lastInteropHr=signalHr;return false;}
    const bool waited=waitCopyContext(guideCopy_,st);if(!waited){surface.failureOperation=5;surface.lastInteropHr=HRESULT_FROM_WIN32(WAIT_TIMEOUT);return false;}
    surface.failureOperation=0;surface.lastInteropHr=S_OK;return true;
}

bool D3D12On12Pipeline::process(IDXGISwapChain*swap,const Settings&s,const std::wstring&runtime,RuntimeStatus&st,const SwapchainColorContext* colorContext,bool synchronizeBeforePresent,const std::vector<D3D12QueueWaitPoint>* recoveryWaits){
    st.recoveryPostCopyCommitted=0;
    if(!swap||!s.allowD3D11On12||(!recoveryMode_&&!on12_))return false;
    bridge::setCrashStage(bridge::CrashStage::BackbufferAcquire);
    ComPtr<IDXGISwapChain3> s3;if(FAILED(swap->QueryInterface(IID_PPV_ARGS(&s3))))return false;
    const UINT i=s3->GetCurrentBackBufferIndex();
    ComPtr<ID3D12Resource> bb;if(FAILED(swap->GetBuffer(i,IID_PPV_ARGS(&bb))))return false;
    const auto desc=bb->GetDesc();
    nativePipeline_.releaseObsoleteHost(s,runtime);
    const auto nativeCapture=snapshotGameTemporalGuides(d12_.Get(),GetTickCount64());
    if(nativePipeline_.eligible(desc,nativeCapture,s)){
        GameGuideCaptureGuard guard;
        auto sharedHost=pipeline_.externalHostForNative(s,runtime,st);
        if(!sharedHost)return false;
        const bool processed=nativePipeline_.process(d12_.Get(),queue_.Get(),bb.Get(),nativeCapture,s,runtime,moduleDir_,st,colorContext,sharedHost);
        if(processed){
            // Eligibility requires immutable injector-owned snapshots. Retain
            // this rendered-frame generation across additional/FG Presents,
            // exactly as in recovery. Replacement or expiry retires its slot.
            pipeline_.suspendForNative();
            ownedColor11_.Reset();ownedColor12_.Reset();recoveryDepthGuide_={};recoveryMotionGuide_={};width_=height_=0;format_=DXGI_FORMAT_UNKNOWN;
        }
        return processed;
    }
    nativePipeline_.invalidateHistory();
    if(recoveryMode_ && recoveryD11PendingValue_ && recoveryD11Fence_ && recoveryD11Fence_->GetCompletedValue()<recoveryD11PendingValue_){
        st.failureStage=PipelineStage::None;st.neuralActive=0;
        wcscpy_s(st.message,L"Recovery D3D11 frame still in flight; bypassing this frame safely");
        return false;
    }
    if(recoveryMode_)recoveryD11PendingValue_=0;
    const bool lifecycleChanged=width_ && (width_!=desc.Width || height_!=desc.Height || format_!=desc.Format);
    if(lifecycleChanged)reset();
    if(!ensureOwnedColor(desc,st))return false;

    bridge::setCrashStage(bridge::CrashStage::PreCopy);
    if(!submitBackbufferCopy(bb.Get(),true,st))return false;
    // Recovery uses a normal same-adapter D3D11 device rather than D3D11On12.
    // The D3D12 pre-copy must therefore be complete before D3D11 touches the
    // shared resource.
    if(recoveryMode_&&!waitCopyContext(preCopy_,st))return false;

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
    const bool capturedPresentSafe=captured&&(captured.validUntilPresent||captured.snapshotOwned);
    if(captured&&!capturedPresentSafe){
        std::wstring provider=captured.provider.empty()?L"Game temporal guide":captured.provider;
        provider+=L" (observed, lifetime not Present-safe)";
        wcsncpy_s(st.guideAdapterName,provider.c_str(),_TRUNCATE);
        if(captured.motion12){st.guideMotionWidth=captured.motionWidth;st.guideMotionHeight=captured.motionHeight;st.guideMotionConfidence=captured.confidence;}
        if(captured.depth12){st.guideDepthWidth=captured.depthWidth;st.guideDepthHeight=captured.depthHeight;st.guideDepthConfidence=captured.confidence;}
        wcsncpy_s(st.guideFields,L"D3D12 temporal inputs detected; deferred because provider lifetime ends before Present and no owned snapshot was available",_TRUNCATE);
    }
    // Injector-owned snapshots are admitted after their post-submit copy fence
    // has completed (snapshotGameTemporalGuides only promotes completed slots),
    // so they no longer need to originate on the recovery/present queue.
    // Game-owned Present-safe resources retain the stricter same-queue rule.
    const bool recoveryGuideQueueSafe=recoveryGuideUsable(recoveryMode_,captured.snapshotOwned,captured.snapshotOwned,
                                                          captured.validUntilPresent,captured.producerQueueId,recoveryQueueId_);
    if(capturedPresentSafe && recoveryGuideQueueSafe){
        externalGuide.temporal=captured.temporal;externalGuide.source=captured.source;externalGuide.provider=captured.provider;externalGuide.cameraCut=captured.cameraCut;
        if(recoveryMode_){
            // Recovery never hands a game-owned resource to D3D11/NRHost.
            // Streamline Present-safe resources and volatile injector-owned snapshots are copied on the proven
            // D3D12 queue into injector-owned NT-handle textures first.
            const bool depthExtentOk=providerGuideExtentCompatible(captured.depthWidth,captured.depthHeight,width_,height_);
            const bool motionExtentOk=providerGuideExtentCompatible(captured.motionWidth,captured.motionHeight,width_,height_);
            const bool directDepth=s.useGameDepth&&captured.snapshotOwned&&captured.depthSnapshotOwned&&captured.depth12&&depthExtentOk&&openOwnedSnapshotDirect(captured.depth12.Get(),captured.depthFormat,true,recoveryDepthGuide_,st);
            const bool directMotion=s.motionSource==MotionSource::Auto&&captured.snapshotOwned&&captured.motionSnapshotOwned&&captured.motion12&&captured.motionConventionKnown&&motionExtentOk&&openOwnedSnapshotDirect(captured.motion12.Get(),captured.motionFormat,false,recoveryMotionGuide_,st);
            const bool stagedDepth=!directDepth&&s.useGameDepth&&captured.depth12&&depthExtentOk&&stageRecoveryGuide(captured.depth12.Get(),captured.depthState,captured.depthFormat,true,recoveryDepthGuide_,st);
            const bool stagedMotion=!directMotion&&s.motionSource==MotionSource::Auto&&captured.motion12&&captured.motionConventionKnown&&motionExtentOk&&stageRecoveryGuide(captured.motion12.Get(),captured.motionState,captured.motionFormat,false,recoveryMotionGuide_,st);
            if(s.useGameDepth&&captured.depth12&&(directDepth||stagedDepth)){
                externalGuide.depth=recoveryDepthGuide_.texture11;externalGuide.depthViewFormat=captured.depthFormat;externalGuide.depthInverted=captured.depthInverted;externalGuide.depthConventionKnown=captured.depthConventionKnown;externalGuide.depthWidth=captured.depthWidth;externalGuide.depthHeight=captured.depthHeight;externalGuide.depthConfidence=captured.confidence;
            }
            if(s.motionSource==MotionSource::Auto&&captured.motion12&&captured.motionConventionKnown&&(directMotion||stagedMotion)){
                externalGuide.motion=recoveryMotionGuide_.texture11;externalGuide.motionConventionValid=true;externalGuide.motionEncoding=NativeMotionEncoding::PixelCurrentToPrevious;externalGuide.motionScaleX=captured.motionToPixelScaleX;externalGuide.motionScaleY=captured.motionToPixelScaleY;externalGuide.motionWidth=captured.motionWidth;externalGuide.motionHeight=captured.motionHeight;externalGuide.motionConfidence=captured.confidence;
            }
            if(externalGuide.depth||externalGuide.motion){
                std::wstring provider=externalGuide.provider.empty()?L"Streamline Present-safe guides":externalGuide.provider;
                provider+=L" (staged copy)";externalGuide.provider=provider;
            }
        }else{
            if(s.useGameDepth&&captured.depth12&&providerGuideExtentCompatible(captured.depthWidth,captured.depthHeight,width_,height_)){
                auto state=globalD3D12ResourceTracker().currentState(captured.depth12.Get(),captured.depthState);
                if(wrapReadOnlyGuide(on12_.Get(),captured.depth12.Get(),state,depthWrap)){
                    externalGuide.depth=depthWrap.wrapped;externalGuide.depthViewFormat=captured.depthFormat;externalGuide.depthInverted=captured.depthInverted;externalGuide.depthConventionKnown=captured.depthConventionKnown;externalGuide.depthWidth=captured.depthWidth;externalGuide.depthHeight=captured.depthHeight;externalGuide.depthConfidence=captured.confidence;
                }
            }
            if(s.motionSource==MotionSource::Auto&&captured.motion12&&captured.motionConventionKnown&&providerGuideExtentCompatible(captured.motionWidth,captured.motionHeight,width_,height_)){
                auto state=globalD3D12ResourceTracker().currentState(captured.motion12.Get(),captured.motionState);
                if(wrapReadOnlyGuide(on12_.Get(),captured.motion12.Get(),state,motionWrap)){
                    externalGuide.motion=motionWrap.wrapped;externalGuide.motionConventionValid=true;externalGuide.motionEncoding=NativeMotionEncoding::PixelCurrentToPrevious;externalGuide.motionScaleX=captured.motionToPixelScaleX;externalGuide.motionScaleY=captured.motionToPixelScaleY;externalGuide.motionWidth=captured.motionWidth;externalGuide.motionHeight=captured.motionHeight;externalGuide.motionConfidence=captured.confidence;
                }
            }
        }
    }
    if(recoveryMode_&&capturedPresentSafe&&!recoveryGuideQueueSafe){
        std::wstring provider=captured.provider.empty()?L"Streamline game tags":captured.provider;
        wcsncpy_s(st.guideAdapterName,provider.c_str(),_TRUNCATE);
        wcsncpy_s(st.guideFields,L"game guides observed but deferred: provider command list did not execute on the proven recovery queue",_TRUNCATE);
    }

    // Recovery may overlap DXGI Present with the game's CPU preparation of the
    // next frame. Retire completed owned snapshots after this frame has handled
    // them, but keep a game-owned Present-safe capture that was rejected only
    // because its provider queue has not yet been proven.
    const bool usedCapturedGuide=externalGuide.depth||externalGuide.motion;
    const bool consumeCapturedAfterUse=captured&&shouldConsumeRecoveryGuide(recoveryMode_,captured.snapshotOwned,usedCapturedGuide,recoveryGuideQueueSafe);

    if(!externalGuide.depth&&s.useGameDepth&&!recoveryMode_){
        const auto tracked=globalD3D12ResourceTracker().bestDepthCandidate(d12_.Get(),width_,height_);
        if(tracked.resource&&wrapReadOnlyGuide(on12_.Get(),tracked.resource.Get(),tracked.state,depthWrap)){
            externalGuide.depth=depthWrap.wrapped;externalGuide.depthViewFormat=tracked.viewFormat;externalGuide.depthInverted=tracked.inverted;externalGuide.depthConventionKnown=tracked.conventionKnown;externalGuide.depthWidth=(UINT)tracked.resource->GetDesc().Width;externalGuide.depthHeight=tracked.resource->GetDesc().Height;externalGuide.depthConfidence=tracked.confidence;externalGuide.source=GameGuideSource::D3D12Tracker;externalGuide.provider=L"D3D12 temporal resource tracker";
        }
    }

    bool ok=false;
    if(recoveryMode_){
        bridge::setCrashStage(bridge::CrashStage::NeuralProcess);
        ok=pipeline_.process(ownedColor11_.Get(),s,runtime,st,(externalGuide.depth||externalGuide.motion)?&externalGuide:nullptr,colorContext);
        if(!waitRecoveryD3D11(st))return false;
        if(consumeCapturedAfterUse)consumeCapturedGameTemporalGuides(captured.captureSequence);
    }else{
        std::array<ID3D11Resource*,3> acquired{};UINT acquiredCount=0;acquired[acquiredCount++]=ownedColor11_.Get();
        if(depthWrap.wrapped)acquired[acquiredCount++]=depthWrap.wrapped.Get();
        if(motionWrap.wrapped&&motionWrap.wrapped.Get()!=depthWrap.wrapped.Get())acquired[acquiredCount++]=motionWrap.wrapped.Get();
        bridge::setCrashStage(bridge::CrashStage::On12Acquire);
        on12_->AcquireWrappedResources(acquired.data(),acquiredCount);
        bridge::setCrashStage(bridge::CrashStage::NeuralProcess);
        ok=pipeline_.process(ownedColor11_.Get(),s,runtime,st,(externalGuide.depth||externalGuide.motion)?&externalGuide:nullptr,colorContext);
        bridge::setCrashStage(bridge::CrashStage::On12Release);
        on12_->ReleaseWrappedResources(acquired.data(),acquiredCount);
        ctx_->Flush();
    }
    if(diagnosticMotionCandidate.score>st.nativeMotionCandidateScore){st.nativeMotionCandidateId=diagnosticMotionCandidate.stableId;st.nativeMotionCandidateScore=diagnosticMotionCandidate.score;}
    if(!externalGuide.depth&&!externalGuide.motion){
        if(captured.depth12){st.guideDepthWidth=captured.depthWidth;st.guideDepthHeight=captured.depthHeight;st.guideDepthConfidence=captured.confidence;}
        if(captured.motion12){st.guideMotionWidth=captured.motionWidth;st.guideMotionHeight=captured.motionHeight;st.guideMotionConfidence=captured.confidence;}
        if(captured){
            std::wstring provider=captured.provider.empty()?L"provider snapshot":captured.provider;wcsncpy_s(st.guideAdapterName,provider.c_str(),_TRUNCATE);
            wchar_t stage[160]{};
            const bool dExtent=captured.depth12&&providerGuideExtentCompatible(captured.depthWidth,captured.depthHeight,width_,height_);
            const bool mExtent=captured.motion12&&providerGuideExtentCompatible(captured.motionWidth,captured.motionHeight,width_,height_);
            // Compact error code must fit the 160-character shared diagnostic.
            // op: 1=CreateResource, 2=CreateSharedHandle, 3=OpenSharedResource1,
            //     4=unexpected desc, 5=copy submission or fence wait.
            swprintf_s(stage,L"stage:D f%u hr=%08X p%u op%u a%u f%u;M f%u hr=%08X p%u op%u a%u f%u",
                (unsigned)captured.depthFormat,(unsigned)recoveryDepthGuide_.lastInteropHr,
                recoveryDepthGuide_.interopPath,recoveryDepthGuide_.failureOperation,
                recoveryDepthGuide_.candidateAttempts,(unsigned)recoveryDepthGuide_.triedFormat,
                (unsigned)captured.motionFormat,(unsigned)recoveryMotionGuide_.lastInteropHr,
                recoveryMotionGuide_.interopPath,recoveryMotionGuide_.failureOperation,
                recoveryMotionGuide_.candidateAttempts,(unsigned)recoveryMotionGuide_.triedFormat);
            wcsncpy_s(st.guideFields,stage,_TRUNCATE);
        }else wcscpy_s(st.guideFields,L"synthetic guides only");
        const auto hookDiag=gameTemporalGuideHookDiagnostics();std::wstring fields=st.guideFields;fields+=L"; ";fields+=hookDiag;wcsncpy_s(st.guideFields,fields.c_str(),_TRUNCATE);
    }
    bridge::setCrashStage(bridge::CrashStage::PostCopy);
    if(!submitBackbufferCopy(bb.Get(),false,st))return false;
    // A late-attach recovery queue is inferred from actual PRESENT transitions.
    // Recovery keeps all D3D12 backbuffer work on that same game queue and uses
    // an independently created D3D11 device only for the shared staging surface.
    // Waiting for the final copy before Present proves the handoff is complete;
    // exact swapchain-creation queues retain the asynchronous native fast path.
    if(synchronizeBeforePresent && !waitCopyContext(postCopy_,st))return false;
    if(recoveryMode_)st.recoveryPostCopyCommitted=1;
    bridge::setCrashStage(bridge::CrashStage::ProcessComplete);
    return ok;
}
}
