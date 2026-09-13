#include "d3d12_resource_tracker.hpp"
#include "../bridge/hook_lifecycle.hpp"
#include "udlss/guide_candidate_policy.hpp"
#include <MinHook.h>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <vector>

using Microsoft::WRL::ComPtr;
namespace udlss::gpu {
namespace {
struct Entry {
    ComPtr<ID3D12Resource> resource;
    ComPtr<ID3D12Device> device;
    NativeMotionCandidateMeta motion{};
    DXGI_FORMAT resourceFormat{DXGI_FORMAT_UNKNOWN};
    D3D12_RESOURCE_FLAGS flags{D3D12_RESOURCE_FLAG_NONE};
    D3D12_RESOURCE_STATES state{D3D12_RESOURCE_STATE_COMMON};
    std::uint64_t id{};
    std::uint64_t lastSeenFrame{};
    std::uint32_t consecutiveFrames{};
    std::uint32_t frameWrites{};
    std::uint32_t frameReads{};
    std::uint32_t frameReadAfterWrite{};
    bool frameSawWrite{};
    bool frameDepthWriteToRead{};
    bool depthLike{};
};
struct TrackerState {
    mutable std::mutex mutex;
    std::unordered_map<ID3D12Resource*,Entry> entries;
    std::unordered_map<ID3D12GraphicsCommandList*,std::vector<ID3D12Resource*>> commandListTouches;
    std::uint64_t frame{1};
    std::uint64_t nextId{1};
} g;
thread_local bool g_suppressed=false;
std::atomic_bool queueTouchCaptureEnabled{true};

using ResourceBarrierFn=void (STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,UINT,const D3D12_RESOURCE_BARRIER*);
ResourceBarrierFn origResourceBarrier{};

bool isDepthFormat(DXGI_FORMAT f){
    switch(f){
    case DXGI_FORMAT_D16_UNORM:case DXGI_FORMAT_D24_UNORM_S8_UINT:case DXGI_FORMAT_D32_FLOAT:case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
    case DXGI_FORMAT_R16_TYPELESS:case DXGI_FORMAT_R24G8_TYPELESS:case DXGI_FORMAT_R32_TYPELESS:case DXGI_FORMAT_R32G8X24_TYPELESS:
        return true;
    default:return false;
    }
}
MotionFormatClass classifyMotionFormat(DXGI_FORMAT f){
    switch(f){
    case DXGI_FORMAT_R16G16_FLOAT:return MotionFormatClass::Rg16Float;
    case DXGI_FORMAT_R16G16_SNORM:return MotionFormatClass::Rg16Snorm;
    case DXGI_FORMAT_R32G32_FLOAT:return MotionFormatClass::Rg32Float;
    default:return isDepthFormat(f)?MotionFormatClass::DepthLike:MotionFormatClass::ColorLike;
    }
}
DXGI_FORMAT depthViewFormat(DXGI_FORMAT f){
    switch(f){
    case DXGI_FORMAT_R16_TYPELESS:return DXGI_FORMAT_D16_UNORM;
    case DXGI_FORMAT_R24G8_TYPELESS:return DXGI_FORMAT_D24_UNORM_S8_UINT;
    case DXGI_FORMAT_R32_TYPELESS:return DXGI_FORMAT_D32_FLOAT;
    case DXGI_FORMAT_R32G8X24_TYPELESS:return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
    default:return f;
    }
}
bool writeState(D3D12_RESOURCE_STATES s){return (s&(D3D12_RESOURCE_STATE_RENDER_TARGET|D3D12_RESOURCE_STATE_UNORDERED_ACCESS|D3D12_RESOURCE_STATE_DEPTH_WRITE|D3D12_RESOURCE_STATE_COPY_DEST))!=0;}
bool readState(D3D12_RESOURCE_STATES s){return (s&(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_DEPTH_READ|D3D12_RESOURCE_STATE_COPY_SOURCE))!=0 || s==D3D12_RESOURCE_STATE_GENERIC_READ;}
bool sameDevice(ID3D12Device* a,ID3D12Device* b){
    if(!a||!b)return false;ComPtr<IUnknown> ua,ub;if(FAILED(a->QueryInterface(IID_PPV_ARGS(&ua)))||FAILED(b->QueryInterface(IID_PPV_ARGS(&ub))))return a==b;return ua.Get()==ub.Get();
}
Entry* getOrCreate(ID3D12Resource* r){
    if(!r)return nullptr;auto it=g.entries.find(r);if(it!=g.entries.end())return &it->second;
    const auto d=r->GetDesc();if(d.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||d.DepthOrArraySize!=1)return nullptr;
    const bool depth=(d.Flags&D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)!=0||isDepthFormat(d.Format);
    const auto motionClass=classifyMotionFormat(d.Format);
    if(!depth && !nativeMotionFormatPlausible(motionClass))return nullptr;
    Entry e{};e.resource=r;r->GetDevice(IID_PPV_ARGS(&e.device));e.id=g.nextId++;e.resourceFormat=d.Format;e.flags=d.Flags;e.depthLike=depth;
    e.motion.width=(std::uint32_t)d.Width;e.motion.height=d.Height;e.motion.format=motionClass;e.motion.msaa=d.SampleDesc.Count>1;e.motion.depthBound=depth;e.motion.encoding=NativeMotionEncoding::Unknown;
    auto [pos,_]=g.entries.emplace(r,std::move(e));return &pos->second;
}
void STDMETHODCALLTYPE hkResourceBarrier(ID3D12GraphicsCommandList* list,UINT count,const D3D12_RESOURCE_BARRIER* barriers){
    udlss::bridge::HookCallScope call;
    if(call.customWorkAllowed()&&!g_suppressed)globalD3D12ResourceTracker().onResourceBarriers(list,count,barriers);
    origResourceBarrier(list,count,barriers);
}
}

D3D12ResourceTracker& globalD3D12ResourceTracker(){static D3D12ResourceTracker t;return t;}
void setD3D12TrackingSuppressed(bool v){g_suppressed=v;}
bool d3d12TrackingSuppressed(){return g_suppressed;}
void setD3D12QueueTouchCaptureEnabled(bool value){const bool previous=queueTouchCaptureEnabled.exchange(value,std::memory_order_relaxed);if(previous!=value){std::scoped_lock lock(g.mutex);g.commandListTouches.clear();}}
bool d3d12QueueTouchCaptureEnabled(){return queueTouchCaptureEnabled.load(std::memory_order_relaxed);}

void D3D12ResourceTracker::onResourceBarriers(ID3D12GraphicsCommandList* list,UINT count,const D3D12_RESOURCE_BARRIER* barriers){
    if(!barriers)return;const bool captureQueueTouches=list&&queueTouchCaptureEnabled.load(std::memory_order_relaxed);std::scoped_lock lock(g.mutex);
    for(UINT i=0;i<count;i++){
        const auto& b=barriers[i];if(b.Type!=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION||!b.Transition.pResource)continue;
        if(captureQueueTouches){auto& touched=g.commandListTouches[list];if(std::find(touched.begin(),touched.end(),b.Transition.pResource)==touched.end())touched.push_back(b.Transition.pResource);}
        auto* e=getOrCreate(b.Transition.pResource);if(!e)continue;
        e->lastSeenFrame=g.frame;e->state=b.Transition.StateAfter;
        const bool wasWrite=writeState(b.Transition.StateBefore),nowWrite=writeState(b.Transition.StateAfter),nowRead=readState(b.Transition.StateAfter);
        if(wasWrite||nowWrite){++e->frameWrites;e->frameSawWrite=true;}
        if(nowRead){++e->frameReads;if(e->frameSawWrite){++e->frameReadAfterWrite;if(e->depthLike)e->frameDepthWriteToRead=true;}}
    }
}
void D3D12ResourceTracker::finalizeFrame(ID3D12Device* device,UINT,UINT){
    std::scoped_lock lock(g.mutex);
    for(auto it=g.entries.begin();it!=g.entries.end();){auto& e=it->second;
        if(sameDevice(e.device.Get(),device)){
            if(e.lastSeenFrame==g.frame){e.motion.renderTargetWrites=e.frameWrites;e.motion.shaderResourceBinds=e.frameReads;e.motion.sampledAfterWrite=e.frameReadAfterWrite;++e.motion.framesObserved;++e.consecutiveFrames;}
            else if(e.lastSeenFrame+1<g.frame)e.consecutiveFrames=0;
            e.frameWrites=e.frameReads=e.frameReadAfterWrite=0;e.frameSawWrite=false;e.frameDepthWriteToRead=false;
        }
        if(e.lastSeenFrame+180<g.frame)it=g.entries.erase(it);else ++it;
    }
    ++g.frame;
}
D3D12MotionCandidate D3D12ResourceTracker::bestMotionCandidate(ID3D12Device* device,UINT width,UINT height) const{
    std::scoped_lock lock(g.mutex);D3D12MotionCandidate best{};
    for(const auto&[_,e]:g.entries){if(!sameDevice(e.device.Get(),device)||e.depthLike)continue;
        const bool transition=e.motion.sampledAfterWrite>0;const auto score=scoreTemporalMotionCandidate(e.motion,width,height,transition,e.consecutiveFrames);
        if(score>best.score){best.resource=e.resource;best.meta=e.motion;best.state=e.state;best.stableId=e.id;best.score=score;best.confidence=score;best.conventionKnown=e.motion.encoding!=NativeMotionEncoding::Unknown;}}
    // Do not auto-consume an unknown encoding. The candidate remains visible in
    // diagnostics, while Streamline/NGX captures can supply an exact convention.
    if(!best.conventionKnown)best.resource.Reset();
    return best;
}
D3D12DepthCandidate D3D12ResourceTracker::bestDepthCandidate(ID3D12Device* device,UINT width,UINT height) const{
    std::scoped_lock lock(g.mutex);D3D12DepthCandidate best{};
    for(const auto&[_,e]:g.entries){if(!sameDevice(e.device.Get(),device)||!e.depthLike)continue;const auto d=e.resource->GetDesc();
        DepthCandidateMeta meta{(std::uint32_t)d.Width,d.Height,d.SampleDesc.Count,true,true};const bool transitioned=e.motion.sampledAfterWrite>0;
        const int score=scoreTemporalDepthCandidate(meta,width,height,transitioned,e.consecutiveFrames);if(score<0)continue;
        if((std::uint32_t)score>best.score){best.resource=e.resource;best.viewFormat=depthViewFormat(e.resourceFormat);best.state=e.state;best.stableId=e.id;best.score=(std::uint32_t)score;best.confidence=std::min<std::uint32_t>(100u,(std::uint32_t)std::max(0,score-8000)/20u+50u);best.inverted=true;best.conventionKnown=false;}}
    if(best.score<9000)best.resource.Reset();return best;
}
D3D12_RESOURCE_STATES D3D12ResourceTracker::currentState(ID3D12Resource* resource,D3D12_RESOURCE_STATES fallback) const{
    if(!resource)return fallback;std::scoped_lock lock(g.mutex);auto it=g.entries.find(resource);return it==g.entries.end()?fallback:it->second.state;
}
std::vector<ID3D12Resource*> D3D12ResourceTracker::takeCommandListTouches(ID3D12CommandList* commandList){
    if(!commandList)return {};
    ComPtr<ID3D12GraphicsCommandList> graphics;
    if(FAILED(commandList->QueryInterface(IID_PPV_ARGS(&graphics))))return {};
    std::scoped_lock lock(g.mutex);
    auto it=g.commandListTouches.find(graphics.Get());if(it==g.commandListTouches.end())return {};
    auto out=std::move(it->second);g.commandListTouches.erase(it);return out;
}
std::vector<ID3D12Resource*> takeD3D12CommandListTouches(ID3D12CommandList* commandList){return globalD3D12ResourceTracker().takeCommandListTouches(commandList);}
void D3D12ResourceTracker::reset(){std::scoped_lock lock(g.mutex);g.entries.clear();g.commandListTouches.clear();g.frame=1;g.nextId=1;}

bool installD3D12ResourceTrackingHooks(ID3D12Device* device){
    if(!device)return false;ComPtr<ID3D12CommandAllocator> alloc;ComPtr<ID3D12GraphicsCommandList> list;
    if(FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&alloc))))return false;
    if(FAILED(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,alloc.Get(),nullptr,IID_PPV_ARGS(&list))))return false;
    void** v=*(void***)list.Get();if(!v||!v[26])return false;
    const auto r=MH_CreateHook(v[26],(void*)hkResourceBarrier,(void**)&origResourceBarrier);
    if(r!=MH_OK && r!=MH_ERROR_ALREADY_CREATED)return false;
    const auto enabled=MH_EnableHook(v[26]);
    return enabled==MH_OK || enabled==MH_ERROR_ENABLED;
}

} // namespace udlss::gpu
