#include "game_temporal_guides.hpp"
#include "shared_guide_texture.hpp"
#include "udlss/provider_snapshot_policy.hpp"
#include "../bridge/hook_lifecycle.hpp"
#include <MinHook.h>
#include <windows.h>
#include <TlHelp32.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>
#include <string_view>
#include <utility>
#include <limits>
#include <vector>
#include <unordered_map>
#include <array>
#include <cstdint>
#include <cstring>
#include <sstream>

#ifdef UDLSS_WITH_STREAMLINE
#include <sl.h>
#endif
#ifdef UDLSS_WITH_NGX_NR
#include <nvsdk_ngx.h>
#include <nvsdk_ngx_params.h>
#endif

using Microsoft::WRL::ComPtr;
namespace udlss::gpu {
namespace {
std::mutex g_mutex;
CapturedGameGuides g_capture;
std::uint64_t g_nextCaptureSequence{1};
thread_local bool g_suppressed=false;
std::atomic_bool g_captureEnabled{true};
std::atomic<GameTemporalGuideCaptureMode> g_captureMode{GameTemporalGuideCaptureMode::Full};
std::atomic_uint64_t g_streamlineHookHits{0};
std::atomic_uint64_t g_slSetTagForFrameCalls{0};
std::atomic_uint64_t g_slSetTagCalls{0};
std::atomic_uint64_t g_slSetConstantsCalls{0};
std::atomic_uint64_t g_slEvaluateCalls{0};
std::atomic_uint64_t g_slTagItems{0};
std::atomic_uint64_t g_slDepthTags{0};
std::atomic_uint64_t g_slMotionTags{0};
std::atomic_uint64_t g_slNullResourceTags{0};
std::atomic_bool g_slSetTagForFrameInstalled{false};
std::atomic_bool g_slSetTagInstalled{false};
std::atomic_bool g_slSetConstantsInstalled{false};
std::atomic_bool g_slEvaluateInstalled{false};
std::atomic_uint64_t g_ngx12HookHits{0};
std::atomic_uint64_t g_acceptedCaptures{0};
std::atomic_bool g_streamlineModuleSeen{false};
std::atomic_bool g_ngxModuleSeen{false};
std::atomic_bool g_streamlineHooksInstalled{false};
std::atomic_bool g_ngx12HookInstalled{false};
std::atomic_bool g_ngx11HookInstalled{false};
std::atomic_uint64_t g_streamlineVolatileTags{0};
std::atomic_uint64_t g_streamlinePostSubmitSnapshots{0};
std::atomic_uint64_t g_streamlineEvaluationBinds{0};
std::atomic_uint64_t g_streamlineBoundSnapshotQueues{0};
std::atomic_uint64_t g_ngxPostSubmitSnapshots{0};
std::atomic_uint64_t g_streamlineRejectedUnknownState{0};
std::atomic_uint64_t g_providerPendingEvictions{0};
std::atomic_uint64_t g_ownedSnapshotPromotions{0};
std::atomic_uint64_t g_ownedSnapshotReads{0};

struct PendingProviderGuides {
    GameGuideSource source{GameGuideSource::None};
    std::wstring provider;
    ComPtr<ID3D12Resource> depth;
    ComPtr<ID3D12Resource> motion;
    D3D12_RESOURCE_DESC depthDesc{};
    D3D12_RESOURCE_DESC motionDesc{};
    D3D12_RESOURCE_STATES depthState{D3D12_RESOURCE_STATE_COMMON};
    D3D12_RESOURCE_STATES motionState{D3D12_RESOURCE_STATE_COMMON};
    DXGI_FORMAT depthFormat{DXGI_FORMAT_UNKNOWN};
    DXGI_FORMAT motionFormat{DXGI_FORMAT_UNKNOWN};
    std::uint32_t depthWidth{},depthHeight{};
    std::uint32_t motionWidth{},motionHeight{};
    float motionToPixelScaleX{1.0f},motionToPixelScaleY{1.0f};
    bool depthInverted{true};
    bool depthConventionKnown{};
    bool motionConventionKnown{};
    bool cameraCut{};
    std::uint32_t confidence{};
    std::uint64_t capturedTickMs{};
    std::uintptr_t providerCommandListId{};
    std::uint32_t providerViewportId{};

    explicit operator bool() const { return depth||motion; }
};
std::unordered_map<std::uintptr_t,PendingProviderGuides> g_pendingProviders;

struct PostSubmitSnapshotSlot {
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Resource> depth;
    ComPtr<ID3D12Resource> motion;
    D3D12_RESOURCE_DESC depthDesc{};
    D3D12_RESOURCE_DESC motionDesc{};
    std::uint64_t fenceValue{};
    CapturedGameGuides stagedCapture{};
    bool captureConsumed{true};
};
struct PostSubmitSnapshotQueue {
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12Fence> fence;
    std::uint64_t nextFenceValue{1};
    std::array<PostSubmitSnapshotSlot,6> slots{};
};
std::unordered_map<std::uintptr_t,PostSubmitSnapshotQueue> g_postSubmitQueues;

bool sameComObject(IUnknown* a,IUnknown* b){
    if(!a||!b)return false;
    ComPtr<IUnknown> ua,ub;
    if(FAILED(a->QueryInterface(IID_PPV_ARGS(&ua)))||FAILED(b->QueryInterface(IID_PPV_ARGS(&ub))))return a==b;
    return ua.Get()==ub.Get();
}

void describe11(ID3D11Texture2D* tex,DXGI_FORMAT& format,std::uint32_t& width,std::uint32_t& height){
    if(!tex)return;D3D11_TEXTURE2D_DESC d{};tex->GetDesc(&d);format=d.Format;width=d.Width;height=d.Height;
}
void describe12(ID3D12Resource* tex,DXGI_FORMAT& format,std::uint32_t& width,std::uint32_t& height){
    if(!tex)return;const auto d=tex->GetDesc();format=d.Format;width=(std::uint32_t)d.Width;height=d.Height;
}

bool sameSnapshotDesc(const D3D12_RESOURCE_DESC& a,const D3D12_RESOURCE_DESC& b){
    return a.Dimension==b.Dimension&&a.Alignment==b.Alignment&&a.Width==b.Width&&a.Height==b.Height&&
           a.DepthOrArraySize==b.DepthOrArraySize&&a.MipLevels==b.MipLevels&&a.Format==b.Format&&
           a.SampleDesc.Count==b.SampleDesc.Count&&a.SampleDesc.Quality==b.SampleDesc.Quality&&a.Layout==b.Layout&&a.Flags==b.Flags;
}

void markSnapshotCaptureConsumedLocked(std::uint64_t sequence){
    if(!sequence)return;
    for(auto& [_,queue]:g_postSubmitQueues)for(auto& slot:queue.slots){
        if(slot.stagedCapture.captureSequence==sequence){slot.captureConsumed=true;slot.stagedCapture={};}
    }
}

bool captureCanReplaceLocked(const CapturedGameGuides& incoming){
    if(!incoming)return false;
    if(!g_capture)return true;
    const auto inPriority=guideSourcePriority(incoming.source),curPriority=guideSourcePriority(g_capture.source);
    if(inPriority<curPriority && guideCaptureFresh(g_capture.capturedTickMs,incoming.capturedTickMs,1000))return false;
    // SnapshotOnly recovery must not let a newer game-owned Present-safe tag
    // starve a completed injector-owned snapshot. The owned generation has
    // already passed its copy fence and is therefore the safer consumable
    // representation even when it is one frame older than the live tag.
    const bool snapshotOnly=g_captureMode.load(std::memory_order_relaxed)==GameTemporalGuideCaptureMode::SnapshotOnly;
    if(ownedSnapshotShouldSupersedeCurrent(snapshotOnly,incoming.snapshotOwned,g_capture.snapshotOwned))return true;
    if(inPriority==curPriority && incoming.capturedTickMs<g_capture.capturedTickMs)return false;
    return true;
}

void mergeSameProviderFieldsLocked(CapturedGameGuides& incoming){
    // Never splice resources across two owned snapshot slots: consuming the old
    // slot would allow it to be recycled while the new capture still referenced
    // one of its textures. Provider components are merged before snapshotting by
    // command-list identity instead.
    if(incoming.snapshotOwned||g_capture.snapshotOwned)return;
    if(!g_capture||incoming.source!=g_capture.source||incoming.providerViewportId!=g_capture.providerViewportId||!guideCaptureFresh(g_capture.capturedTickMs,incoming.capturedTickMs,100))return;
    if(!incoming.depth11&&!incoming.depth12){
        incoming.depth11=g_capture.depth11;incoming.depth12=g_capture.depth12;incoming.depthFormat=g_capture.depthFormat;
        incoming.depthState=g_capture.depthState;incoming.depthWidth=g_capture.depthWidth;incoming.depthHeight=g_capture.depthHeight;
        incoming.depthInverted=g_capture.depthInverted;incoming.depthConventionKnown=g_capture.depthConventionKnown;
        incoming.depthSnapshotOwned=g_capture.depthSnapshotOwned;
    }
    if(!incoming.motion11&&!incoming.motion12){
        incoming.motion11=g_capture.motion11;incoming.motion12=g_capture.motion12;incoming.motionFormat=g_capture.motionFormat;
        incoming.motionState=g_capture.motionState;incoming.motionWidth=g_capture.motionWidth;incoming.motionHeight=g_capture.motionHeight;
        incoming.motionToPixelScaleX=g_capture.motionToPixelScaleX;incoming.motionToPixelScaleY=g_capture.motionToPixelScaleY;
        incoming.motionConventionKnown=g_capture.motionConventionKnown;incoming.motionSnapshotOwned=g_capture.motionSnapshotOwned;
    }
    incoming.cameraCut=incoming.cameraCut||g_capture.cameraCut;
    incoming.validUntilPresent=incoming.validUntilPresent&&g_capture.validUntilPresent;
    if(!incoming.providerCommandListId)incoming.providerCommandListId=g_capture.providerCommandListId;
    if(!incoming.producerQueueId)incoming.producerQueueId=g_capture.producerQueueId;
    incoming.snapshotOwned=incoming.depthSnapshotOwned||incoming.motionSnapshotOwned;
}

std::uint64_t publishCaptureLocked(CapturedGameGuides incoming,bool sequenceAssigned=false){
    if(!captureCanReplaceLocked(incoming))return 0;
    const bool promotedOwnedOverLive=incoming.snapshotOwned&&g_capture&&!g_capture.snapshotOwned;
    mergeSameProviderFieldsLocked(incoming);
    if(!sequenceAssigned||!incoming.captureSequence)incoming.captureSequence=g_nextCaptureSequence++;
    if(g_capture.snapshotOwned&&g_capture.captureSequence!=incoming.captureSequence)markSnapshotCaptureConsumedLocked(g_capture.captureSequence);
    const auto sequence=incoming.captureSequence;
    g_capture=std::move(incoming);
    if(promotedOwnedOverLive)g_ownedSnapshotPromotions.fetch_add(1,std::memory_order_relaxed);
    g_acceptedCaptures.fetch_add(1,std::memory_order_relaxed);
    return sequence;
}

void promoteCompletedSnapshotsLocked(){
    for(auto& [_,queue]:g_postSubmitQueues){
        if(!queue.fence)continue;
        const auto completed=queue.fence->GetCompletedValue();
        for(auto& slot:queue.slots){
            if(slot.captureConsumed||!slot.fenceValue||slot.fenceValue==UINT64_MAX||completed<slot.fenceValue||!slot.stagedCapture)continue;
            if(g_capture.captureSequence==slot.stagedCapture.captureSequence)continue;
            publishCaptureLocked(slot.stagedCapture,true);
        }
    }
}

std::uint64_t mergeCapture(CapturedGameGuides incoming){
    if(!incoming||!g_captureEnabled.load(std::memory_order_relaxed))return 0;
    const auto mode=g_captureMode.load(std::memory_order_relaxed);
    if(mode==GameTemporalGuideCaptureMode::Disabled)return 0;
    if(mode==GameTemporalGuideCaptureMode::PresentSafeOnly &&
       (incoming.source!=GameGuideSource::Streamline || !incoming.validUntilPresent))return 0;
    if(mode==GameTemporalGuideCaptureMode::SnapshotOnly && !incoming.snapshotOwned &&
       (incoming.source!=GameGuideSource::Streamline || !incoming.validUntilPresent))return 0;
    std::scoped_lock lock(g_mutex);
    return publishCaptureLocked(std::move(incoming));
}

CapturedGameGuides snapshotFor11(ID3D11Device* device,std::uint64_t now){
    std::scoped_lock lock(g_mutex);auto out=g_capture;
    const auto maxAge=out.validUntilPresent?750ull:50ull;
    if(!guideCaptureFresh(out.capturedTickMs,now,maxAge))return{};
    ID3D11Device* owner=nullptr;
    if(out.depth11)out.depth11->GetDevice(&owner);else if(out.motion11)out.motion11->GetDevice(&owner);
    ComPtr<ID3D11Device> ownerHold;ownerHold.Attach(owner);
    if(!ownerHold||!sameComObject(ownerHold.Get(),device))return{};
    out.depth12.Reset();out.motion12.Reset();return out;
}
CapturedGameGuides snapshotFor12(ID3D12Device* device,std::uint64_t now){
    std::scoped_lock lock(g_mutex);
    promoteCompletedSnapshotsLocked();
    auto out=g_capture;
    const auto maxAge=out.snapshotOwned?2000ull:(out.validUntilPresent?750ull:50ull);
    if(!guideCaptureFresh(out.capturedTickMs,now,maxAge)){
        if(out.snapshotOwned&&g_capture.captureSequence==out.captureSequence){markSnapshotCaptureConsumedLocked(out.captureSequence);g_capture={};}
        return{};
    }
    ComPtr<ID3D12Device> owner;
    if(out.depth12)out.depth12->GetDevice(IID_PPV_ARGS(&owner));else if(out.motion12)out.motion12->GetDevice(IID_PPV_ARGS(&owner));
    if(!owner||!sameComObject(owner.Get(),device))return{};
    if(out.snapshotOwned)g_ownedSnapshotReads.fetch_add(1,std::memory_order_relaxed);
    out.depth11.Reset();out.motion11.Reset();return out;
}

std::uintptr_t commandListIdentity(ID3D12CommandList* list){
    if(!list)return 0;
    ComPtr<IUnknown> id;
    if(FAILED(list->QueryInterface(IID_PPV_ARGS(&id))))return reinterpret_cast<std::uintptr_t>(list);
    return reinterpret_cast<std::uintptr_t>(id.Get());
}


void pruneStalePendingProvidersLocked(std::uint64_t now){
    for(auto it=g_pendingProviders.begin();it!=g_pendingProviders.end();){
        if(!guideCaptureFresh(it->second.capturedTickMs,now,500)){
            it=g_pendingProviders.erase(it);g_providerPendingEvictions.fetch_add(1,std::memory_order_relaxed);
        }else ++it;
    }
}

bool queuePendingProviderGuides(PendingProviderGuides incoming){
    if(!incoming||!incoming.providerCommandListId)return false;
    std::scoped_lock lock(g_mutex);
    pruneStalePendingProvidersLocked(incoming.capturedTickMs);
    auto& dst=g_pendingProviders[incoming.providerCommandListId];
    const bool sameGeneration=dst&&dst.source==incoming.source&&dst.providerViewportId==incoming.providerViewportId&&guideCaptureFresh(dst.capturedTickMs,incoming.capturedTickMs,250);
    if(!sameGeneration){dst=std::move(incoming);return true;}
    const bool incomingPreferred=guideSourcePriority(incoming.source)>=guideSourcePriority(dst.source);
    dst.capturedTickMs=std::max(dst.capturedTickMs,incoming.capturedTickMs);
    dst.confidence=std::max(dst.confidence,incoming.confidence);
    dst.cameraCut=dst.cameraCut||incoming.cameraCut;
    if(incoming.depth&&(incomingPreferred||!dst.depth)){
        dst.depth=incoming.depth;dst.depthDesc=incoming.depthDesc;dst.depthState=incoming.depthState;dst.depthFormat=incoming.depthFormat;
        dst.depthWidth=incoming.depthWidth;dst.depthHeight=incoming.depthHeight;dst.depthInverted=incoming.depthInverted;
        dst.depthConventionKnown=incoming.depthConventionKnown;
    }
    if(incoming.motion&&(incomingPreferred||!dst.motion)){
        dst.motion=incoming.motion;dst.motionDesc=incoming.motionDesc;dst.motionState=incoming.motionState;dst.motionFormat=incoming.motionFormat;
        dst.motionWidth=incoming.motionWidth;dst.motionHeight=incoming.motionHeight;dst.motionToPixelScaleX=incoming.motionToPixelScaleX;
        dst.motionToPixelScaleY=incoming.motionToPixelScaleY;dst.motionConventionKnown=incoming.motionConventionKnown;
    }
    if(incomingPreferred){dst.source=incoming.source;dst.providerViewportId=incoming.providerViewportId;if(!incoming.provider.empty())dst.provider=std::move(incoming.provider);}
    return true;
}

PendingProviderGuides pendingFromCapturedStreamline(const CapturedGameGuides& c,std::uintptr_t commandListId){
    PendingProviderGuides pending{};
    if(c.source!=GameGuideSource::Streamline||c.snapshotOwned||!c.validUntilPresent||!commandListId)return pending;
    pending.source=GameGuideSource::Streamline;pending.provider=L"Streamline game tags";pending.capturedTickMs=c.capturedTickMs;
    pending.providerCommandListId=commandListId;pending.providerViewportId=c.providerViewportId;pending.confidence=c.confidence;pending.depthInverted=c.depthInverted;
    pending.depthConventionKnown=c.depthConventionKnown;pending.motionConventionKnown=c.motionConventionKnown;
    pending.motionToPixelScaleX=c.motionToPixelScaleX;pending.motionToPixelScaleY=c.motionToPixelScaleY;pending.cameraCut=c.cameraCut;
    if(c.depth12){pending.depth=c.depth12;pending.depthDesc=c.depth12->GetDesc();pending.depthState=c.depthState;pending.depthFormat=c.depthFormat;pending.depthWidth=c.depthWidth;pending.depthHeight=c.depthHeight;}
    if(c.motion12){pending.motion=c.motion12;pending.motionDesc=c.motion12->GetDesc();pending.motionState=c.motionState;pending.motionFormat=c.motionFormat;pending.motionWidth=c.motionWidth;pending.motionHeight=c.motionHeight;}
    return pending;
}

#ifdef UDLSS_WITH_STREAMLINE
constexpr std::size_t kMaxSlHookTargets=8;
std::array<PFun_slSetTagForFrame*,kMaxSlHookTargets> g_origSlSetTagForFrameTargets{};
std::array<PFun_slSetTag*,kMaxSlHookTargets> g_origSlSetTagTargets{};
std::array<PFun_slSetConstants*,kMaxSlHookTargets> g_origSlSetConstantsTargets{};
std::array<PFun_slEvaluateFeature*,kMaxSlHookTargets> g_origSlEvaluateTargets{};
std::array<void*,kMaxSlHookTargets> g_slSetTagForFrameTargets{};
std::array<void*,kMaxSlHookTargets> g_slSetTagTargets{};
std::array<void*,kMaxSlHookTargets> g_slSetConstantsTargets{};
std::array<void*,kMaxSlHookTargets> g_slEvaluateTargets{};
struct SlConstantsSnapshot {
    float sx{1},sy{1};
    bool depthInverted{true},depthKnown{},reset{};
    std::uint32_t renderWidth{},renderHeight{};
    std::uint64_t tick{};
};
SlConstantsSnapshot g_slConstants;
std::unordered_map<std::uint32_t,SlConstantsSnapshot> g_slConstantsByViewport;

SlConstantsSnapshot slConstantsForViewport(std::uint32_t viewportId){
    std::scoped_lock lock(g_mutex);
    if(viewportId){auto it=g_slConstantsByViewport.find(viewportId);if(it!=g_slConstantsByViewport.end())return it->second;}
    return g_slConstants;
}

void noteSlResourceTag(const sl::ResourceTag& tag){
    g_slTagItems.fetch_add(1,std::memory_order_relaxed);
    if(tag.type==sl::kBufferTypeDepth)g_slDepthTags.fetch_add(1,std::memory_order_relaxed);
    else if(tag.type==sl::kBufferTypeMotionVectors)g_slMotionTags.fetch_add(1,std::memory_order_relaxed);
    if(!tag.resource||!tag.resource->native)g_slNullResourceTags.fetch_add(1,std::memory_order_relaxed);
}

void captureSlTag(const sl::ResourceTag& tag,bool localEvaluation,sl::CommandBuffer* cmd,std::uint32_t viewportId){
    if(g_suppressed||!g_captureEnabled.load(std::memory_order_relaxed)||!tag.resource||!tag.resource->native)return;
    // DLSS motion-vector scale is defined relative to the render/input-color extent,
    // not necessarily the motion-texture extent. Keep that extent per viewport so
    // multi-feature Streamline traffic cannot corrupt Cyberpunk's DLSS conventions.
    if(tag.type==sl::kBufferTypeScalingInputColor){
        std::uint32_t rw=tag.extent.width,rh=tag.extent.height;
        if((!rw||!rh)&&tag.resource->native){
            IUnknown* n=reinterpret_cast<IUnknown*>(tag.resource->native);ComPtr<ID3D12Resource> r12;ComPtr<ID3D11Texture2D> r11;DXGI_FORMAT f{};
            if(SUCCEEDED(n->QueryInterface(IID_PPV_ARGS(&r12))))describe12(r12.Get(),f,rw,rh);
            else if(SUCCEEDED(n->QueryInterface(IID_PPV_ARGS(&r11))))describe11(r11.Get(),f,rw,rh);
        }
        if(viewportId&&rw&&rh){std::scoped_lock lock(g_mutex);auto& c=g_slConstantsByViewport[viewportId];c.renderWidth=rw;c.renderHeight=rh;}
        return;
    }
    if(tag.type!=sl::kBufferTypeDepth && tag.type!=sl::kBufferTypeMotionVectors)return;
    const auto mode=g_captureMode.load(std::memory_order_relaxed);
    const auto constants=slConstantsForViewport(viewportId);
    if(mode==GameTemporalGuideCaptureMode::Disabled)return;
    const bool providerPresentSafe=tag.lifecycle==sl::eValidUntilPresent;
    const bool volatileLifecycle=tag.lifecycle==sl::eOnlyValidNow || tag.lifecycle==sl::eValidUntilEvaluate;
    if(mode==GameTemporalGuideCaptureMode::PresentSafeOnly && !providerPresentSafe)return;
    if(mode==GameTemporalGuideCaptureMode::SnapshotOnly && !providerPresentSafe && !volatileLifecycle)return;

    const auto now=GetTickCount64();
    IUnknown* native=reinterpret_cast<IUnknown*>(tag.resource->native);
    ComPtr<ID3D11Texture2D> t11;ComPtr<ID3D12Resource> t12;
    const auto extent=tag.extent;
    const auto nativeFormat=static_cast<DXGI_FORMAT>(tag.resource->nativeFormat);
    const auto commandListId=commandListIdentity(reinterpret_cast<ID3D12CommandList*>(cmd));

    // Recovery snapshots D3D12 provider guides on the provider queue immediately
    // after the exact command list that consumed them. This applies to volatile
    // tags and, when possible, Present-safe tags as well so the later recovery
    // pipeline never needs to retain or synchronize a game-owned resource.
    if(mode==GameTemporalGuideCaptureMode::SnapshotOnly &&
       SUCCEEDED(native->QueryInterface(IID_PPV_ARGS(&t12)))){
        const bool stateKnown=tag.resource->state!=std::numeric_limits<uint32_t>::max();
        if(commandListId&&stateKnown){
            DXGI_FORMAT fmt{};std::uint32_t w{},h{};describe12(t12.Get(),fmt,w,h);
            if(nativeFormat!=DXGI_FORMAT_UNKNOWN)fmt=nativeFormat;if(extent.width&&extent.height){w=extent.width;h=extent.height;}
            PendingProviderGuides pending{};pending.source=GameGuideSource::Streamline;pending.provider=L"Streamline game tags";
            pending.capturedTickMs=now;pending.providerCommandListId=commandListId;pending.providerViewportId=viewportId;pending.confidence=providerPresentSafe?100u:(localEvaluation?94u:82u);
            const auto constantsAge=(constants.tick&&now>=constants.tick)?now-constants.tick:UINT64_MAX;
            const bool constantsFresh=streamlineConventionFresh(constantsAge);
            const bool resetFresh=streamlineResetFresh(constantsAge);
            if(constantsFresh){pending.depthInverted=constants.depthInverted;pending.depthConventionKnown=constants.depthKnown;}
            pending.cameraCut=resetFresh&&constants.reset;
            const auto state=static_cast<D3D12_RESOURCE_STATES>(tag.resource->state);
            if(tag.type==sl::kBufferTypeDepth){
                pending.depth=t12;pending.depthDesc=t12->GetDesc();pending.depthState=state;pending.depthFormat=fmt;pending.depthWidth=w;pending.depthHeight=h;
            }else{
                pending.motion=t12;pending.motionDesc=t12->GetDesc();pending.motionState=state;pending.motionFormat=fmt;pending.motionWidth=w;pending.motionHeight=h;
                const auto scaleW=constants.renderWidth?constants.renderWidth:w;const auto scaleH=constants.renderHeight?constants.renderHeight:h;
                const bool scaleXKnown=scaleW&&constantsFresh&&std::isfinite(constants.sx)&&std::abs(constants.sx)<16.0f;
                const bool scaleYKnown=scaleH&&constantsFresh&&std::isfinite(constants.sy)&&std::abs(constants.sy)<16.0f;
                if(scaleXKnown)pending.motionToPixelScaleX=constants.sx*(float)scaleW;
                if(scaleYKnown)pending.motionToPixelScaleY=constants.sy*(float)scaleH;
                pending.motionConventionKnown=scaleXKnown&&scaleYKnown;
            }
            if(queuePendingProviderGuides(std::move(pending))){if(volatileLifecycle)g_streamlineVolatileTags.fetch_add(1,std::memory_order_relaxed);return;}
        }
        if(volatileLifecycle){
            if(!stateKnown)g_streamlineRejectedUnknownState.fetch_add(1,std::memory_order_relaxed);
            return;
        }
        // A Present-safe resource can still be observed directly when the game
        // did not supply a snapshot-capable command buffer/state. It remains
        // subject to provider/recovery queue matching at consumption time.
    }

    CapturedGameGuides c{};c.source=GameGuideSource::Streamline;c.provider=L"Streamline game tags";c.capturedTickMs=now;c.providerViewportId=viewportId;
    c.validUntilPresent=providerPresentSafe;c.confidence=c.validUntilPresent?100u:(localEvaluation?94u:82u);
    if(SUCCEEDED(native->QueryInterface(IID_PPV_ARGS(&t11)))){
        DXGI_FORMAT fmt{};std::uint32_t w{},h{};describe11(t11.Get(),fmt,w,h);if(nativeFormat!=DXGI_FORMAT_UNKNOWN)fmt=nativeFormat;if(extent.width&&extent.height){w=extent.width;h=extent.height;}
        if(tag.type==sl::kBufferTypeDepth){c.depth11=t11;c.depthFormat=fmt;c.depthWidth=w;c.depthHeight=h;}
        else{c.motion11=t11;c.motionFormat=fmt;c.motionWidth=w;c.motionHeight=h;}
    }else if(SUCCEEDED(native->QueryInterface(IID_PPV_ARGS(&t12)))){
        DXGI_FORMAT fmt{};std::uint32_t w{},h{};describe12(t12.Get(),fmt,w,h);if(nativeFormat!=DXGI_FORMAT_UNKNOWN)fmt=nativeFormat;if(extent.width&&extent.height){w=extent.width;h=extent.height;}
        const auto state=static_cast<D3D12_RESOURCE_STATES>(tag.resource->state==std::numeric_limits<uint32_t>::max()?D3D12_RESOURCE_STATE_COMMON:tag.resource->state);
        c.providerCommandListId=commandListId;
        if(tag.type==sl::kBufferTypeDepth){c.depth12=t12;c.depthFormat=fmt;c.depthState=state;c.depthWidth=w;c.depthHeight=h;}
        else{c.motion12=t12;c.motionFormat=fmt;c.motionState=state;c.motionWidth=w;c.motionHeight=h;}
    }else return;
    {
        const auto constantsAge=(constants.tick&&now>=constants.tick)?now-constants.tick:UINT64_MAX;
        const bool constantsFresh=streamlineConventionFresh(constantsAge);
        const bool resetFresh=streamlineResetFresh(constantsAge);
        if(constantsFresh){
            c.depthInverted=constants.depthInverted;c.depthConventionKnown=constants.depthKnown;
            const auto scaleW=constants.renderWidth?constants.renderWidth:c.motionWidth;const auto scaleH=constants.renderHeight?constants.renderHeight:c.motionHeight;
            const bool scaleXKnown=scaleW&&std::isfinite(constants.sx)&&std::abs(constants.sx)<16.0f;
            const bool scaleYKnown=scaleH&&std::isfinite(constants.sy)&&std::abs(constants.sy)<16.0f;
            if(scaleXKnown)c.motionToPixelScaleX=constants.sx*(float)scaleW;
            if(scaleYKnown)c.motionToPixelScaleY=constants.sy*(float)scaleH;
            if(c.motion11||c.motion12)c.motionConventionKnown=scaleXKnown&&scaleYKnown;
        }
        c.cameraCut=resetFresh&&constants.reset;
    }
    mergeCapture(std::move(c));
}

template<std::size_t Slot>
sl::Result hkSlSetTagForFrame(const sl::FrameToken& frame,const sl::ViewportHandle& viewport,const sl::ResourceTag* tags,uint32_t count,sl::CommandBuffer* cmd){
    g_streamlineHookHits.fetch_add(1,std::memory_order_relaxed);g_slSetTagForFrameCalls.fetch_add(1,std::memory_order_relaxed);
    udlss::bridge::HookCallScope call;
    if(call.customWorkAllowed()&&!g_suppressed&&tags)for(uint32_t i=0;i<count;i++){noteSlResourceTag(tags[i]);captureSlTag(tags[i],false,cmd,(std::uint32_t)viewport);}
    return g_origSlSetTagForFrameTargets[Slot](frame,viewport,tags,count,cmd);
}
template<std::size_t Slot>
sl::Result hkSlSetTag(const sl::ViewportHandle& viewport,const sl::ResourceTag* tags,uint32_t count,sl::CommandBuffer* cmd){
    g_streamlineHookHits.fetch_add(1,std::memory_order_relaxed);g_slSetTagCalls.fetch_add(1,std::memory_order_relaxed);
    udlss::bridge::HookCallScope call;
    if(call.customWorkAllowed()&&!g_suppressed&&tags)for(uint32_t i=0;i<count;i++){noteSlResourceTag(tags[i]);captureSlTag(tags[i],false,cmd,(std::uint32_t)viewport);}
    return g_origSlSetTagTargets[Slot](viewport,tags,count,cmd);
}
template<std::size_t Slot>
sl::Result hkSlSetConstants(const sl::Constants& values,const sl::FrameToken& frame,const sl::ViewportHandle& viewport){
    g_streamlineHookHits.fetch_add(1,std::memory_order_relaxed);g_slSetConstantsCalls.fetch_add(1,std::memory_order_relaxed);
    udlss::bridge::HookCallScope call;
    if(call.customWorkAllowed()&&!g_suppressed){
        std::scoped_lock lock(g_mutex);const auto now=GetTickCount64();const auto id=(std::uint32_t)viewport;
        auto update=[&](SlConstantsSnapshot& c){c.sx=values.mvecScale.x;c.sy=values.mvecScale.y;c.depthKnown=values.depthInverted!=sl::Boolean::eInvalid;c.depthInverted=values.depthInverted==sl::Boolean::eTrue;c.reset=values.reset==sl::Boolean::eTrue;c.tick=now;};
        update(g_slConstants);if(id)update(g_slConstantsByViewport[id]);
    }
    return g_origSlSetConstantsTargets[Slot](values,frame,viewport);
}
template<std::size_t Slot>
sl::Result hkSlEvaluateFeature(sl::Feature feature,const sl::FrameToken& frame,const sl::BaseStructure** inputs,uint32_t count,sl::CommandBuffer* cmd){
    g_streamlineHookHits.fetch_add(1,std::memory_order_relaxed);g_slEvaluateCalls.fetch_add(1,std::memory_order_relaxed);
    udlss::bridge::HookCallScope call;
    if(call.customWorkAllowed()&&!g_suppressed){
        std::uint32_t evalViewport=0;
        if(inputs)for(uint32_t i=0;i<count;i++){const auto* base=inputs[i];if(base&&base->structType==sl::ViewportHandle::s_structType){evalViewport=(std::uint32_t)*static_cast<const sl::ViewportHandle*>(base);break;}}
        if(inputs)for(uint32_t i=0;i<count;i++){const auto* base=inputs[i];if(base&&base->structType==sl::ResourceTag::s_structType){const auto& tag=*static_cast<const sl::ResourceTag*>(base);noteSlResourceTag(tag);captureSlTag(tag,true,cmd,evalViewport);}}
        // Some Streamline games tag eValidUntilPresent resources with no command
        // buffer and supply the provider list only when the feature evaluates.
        // Bind only a recent unbound capture; never overwrite known provenance.
        const auto id=commandListIdentity(reinterpret_cast<ID3D12CommandList*>(cmd));
        if(id){
            PendingProviderGuides boundPending{};bool queueBoundSnapshot=false;
            {
                std::scoped_lock lock(g_mutex);const auto now=GetTickCount64();
                const auto age=(g_capture.capturedTickMs&&now>=g_capture.capturedTickMs)?now-g_capture.capturedTickMs:UINT64_MAX;
                const bool viewportMatch=!evalViewport||!g_capture.providerViewportId||g_capture.providerViewportId==evalViewport;
                if(viewportMatch&&shouldBindStreamlineEvaluationCommandList(g_capture.source==GameGuideSource::Streamline,g_capture.snapshotOwned,g_capture.validUntilPresent,g_capture.providerCommandListId,age)){
                    g_capture.providerCommandListId=id;
                    g_streamlineEvaluationBinds.fetch_add(1,std::memory_order_relaxed);
                    const bool hasD3D12Guide=!!g_capture.depth12||!!g_capture.motion12;
                    queueBoundSnapshot=shouldQueueBoundPresentSafeSnapshot(g_captureMode.load(std::memory_order_relaxed)==GameTemporalGuideCaptureMode::SnapshotOnly,hasD3D12Guide,true);
                    if(queueBoundSnapshot)boundPending=pendingFromCapturedStreamline(g_capture,id);
                }
            }
            if(queueBoundSnapshot&&queuePendingProviderGuides(std::move(boundPending)))g_streamlineBoundSnapshotQueues.fetch_add(1,std::memory_order_relaxed);
        }
    }
    return g_origSlEvaluateTargets[Slot](feature,frame,inputs,count,cmd);
}
#endif

#ifdef UDLSS_WITH_NGX_NR
using Ngx12EvaluateFn=NVSDK_NGX_Result (NVSDK_CONV*)(ID3D12GraphicsCommandList*,const NVSDK_NGX_Handle*,const NVSDK_NGX_Parameter*,PFN_NVSDK_NGX_ProgressCallback);
using Ngx11EvaluateFn=NVSDK_NGX_Result (NVSDK_CONV*)(ID3D11DeviceContext*,const NVSDK_NGX_Handle*,const NVSDK_NGX_Parameter*,PFN_NVSDK_NGX_ProgressCallback);
Ngx12EvaluateFn g_origNgx12Evaluate{};Ngx11EvaluateFn g_origNgx11Evaluate{};

void captureNgx12(ID3D12GraphicsCommandList* list,const NVSDK_NGX_Parameter* p){
    if(g_suppressed||!g_captureEnabled.load(std::memory_order_relaxed)||!p)return;
    const auto mode=g_captureMode.load(std::memory_order_relaxed);
    if(mode==GameTemporalGuideCaptureMode::Disabled||mode==GameTemporalGuideCaptureMode::PresentSafeOnly)return;
    ID3D12Resource* depth{};ID3D12Resource* motion{};
    p->Get(NVSDK_NGX_Parameter_Depth,&depth);p->Get(NVSDK_NGX_Parameter_MotionVectors,&motion);if(!depth&&!motion)return;
    constexpr auto ngxGuideState=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    float sx=1,sy=1;const bool sxKnown=p->Get(NVSDK_NGX_Parameter_MV_Scale_X,&sx)==NVSDK_NGX_Result_Success;
    const bool syKnown=p->Get(NVSDK_NGX_Parameter_MV_Scale_Y,&sy)==NVSDK_NGX_Result_Success;
    int flags=0;const bool flagsKnown=p->Get(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags,&flags)==NVSDK_NGX_Result_Success;
    int reset=0;const bool resetKnown=p->Get(NVSDK_NGX_Parameter_Reset,&reset)==NVSDK_NGX_Result_Success;

    if(mode==GameTemporalGuideCaptureMode::SnapshotOnly){
        const auto listId=commandListIdentity(list);if(!listId)return;
        PendingProviderGuides pending{};pending.source=GameGuideSource::Ngx;pending.provider=L"NGX game evaluation";
        pending.capturedTickMs=GetTickCount64();pending.providerCommandListId=listId;pending.confidence=96;
        pending.depthConventionKnown=flagsKnown;pending.depthInverted=flagsKnown&&(flags&NVSDK_NGX_DLSS_Feature_Flags_DepthInverted)!=0;
        pending.cameraCut=resetKnown&&reset!=0;
        if(depth){pending.depth=depth;pending.depthDesc=depth->GetDesc();pending.depthState=ngxGuideState;describe12(depth,pending.depthFormat,pending.depthWidth,pending.depthHeight);}
        if(motion){
            pending.motion=motion;pending.motionDesc=motion->GetDesc();pending.motionState=ngxGuideState;describe12(motion,pending.motionFormat,pending.motionWidth,pending.motionHeight);
            pending.motionConventionKnown=sxKnown&&syKnown;
            if(sxKnown)pending.motionToPixelScaleX=sx;if(syKnown)pending.motionToPixelScaleY=sy;
        }
        queuePendingProviderGuides(std::move(pending));
        return;
    }

    CapturedGameGuides c{};c.source=GameGuideSource::Ngx;c.provider=L"NGX game evaluation";c.capturedTickMs=GetTickCount64();c.confidence=96;c.validUntilPresent=false;
    c.providerCommandListId=commandListIdentity(list);
    if(depth){c.depth12=depth;describe12(depth,c.depthFormat,c.depthWidth,c.depthHeight);c.depthState=ngxGuideState;}
    if(motion){c.motion12=motion;describe12(motion,c.motionFormat,c.motionWidth,c.motionHeight);c.motionState=ngxGuideState;c.motionConventionKnown=sxKnown&&syKnown;}
    if(sxKnown)c.motionToPixelScaleX=sx;if(syKnown)c.motionToPixelScaleY=sy;
    if(flagsKnown){c.depthConventionKnown=true;c.depthInverted=(flags&NVSDK_NGX_DLSS_Feature_Flags_DepthInverted)!=0;}
    if(resetKnown)c.cameraCut=reset!=0;
    mergeCapture(std::move(c));
}

void captureNgx11(const NVSDK_NGX_Parameter* p){
    if(g_suppressed||!g_captureEnabled.load(std::memory_order_relaxed)||g_captureMode.load(std::memory_order_relaxed)!=GameTemporalGuideCaptureMode::Full||!p)return;ID3D11Resource* depth{};ID3D11Resource* motion{};
    p->Get(NVSDK_NGX_Parameter_Depth,&depth);p->Get(NVSDK_NGX_Parameter_MotionVectors,&motion);if(!depth&&!motion)return;
    CapturedGameGuides c{};c.source=GameGuideSource::Ngx;c.provider=L"NGX game evaluation";c.capturedTickMs=GetTickCount64();c.confidence=96;c.validUntilPresent=false;
    if(depth){ComPtr<ID3D11Texture2D> t;if(SUCCEEDED(depth->QueryInterface(IID_PPV_ARGS(&t)))){c.depth11=t;describe11(t.Get(),c.depthFormat,c.depthWidth,c.depthHeight);}}
    if(motion){ComPtr<ID3D11Texture2D> t;if(SUCCEEDED(motion->QueryInterface(IID_PPV_ARGS(&t)))){c.motion11=t;describe11(t.Get(),c.motionFormat,c.motionWidth,c.motionHeight);c.motionConventionKnown=true;}}
    float sx=1,sy=1;if(p->Get(NVSDK_NGX_Parameter_MV_Scale_X,&sx)==NVSDK_NGX_Result_Success)c.motionToPixelScaleX=sx;if(p->Get(NVSDK_NGX_Parameter_MV_Scale_Y,&sy)==NVSDK_NGX_Result_Success)c.motionToPixelScaleY=sy;
    int flags=0;if(p->Get(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags,&flags)==NVSDK_NGX_Result_Success){c.depthConventionKnown=true;c.depthInverted=(flags&NVSDK_NGX_DLSS_Feature_Flags_DepthInverted)!=0;}
    int reset=0;if(p->Get(NVSDK_NGX_Parameter_Reset,&reset)==NVSDK_NGX_Result_Success)c.cameraCut=reset!=0;
    mergeCapture(std::move(c));
}
NVSDK_NGX_Result NVSDK_CONV hkNgx12Evaluate(ID3D12GraphicsCommandList* l,const NVSDK_NGX_Handle* h,const NVSDK_NGX_Parameter* p,PFN_NVSDK_NGX_ProgressCallback cb){g_ngx12HookHits.fetch_add(1,std::memory_order_relaxed);udlss::bridge::HookCallScope call;if(call.customWorkAllowed())captureNgx12(l,p);return g_origNgx12Evaluate(l,h,p,cb);}
NVSDK_NGX_Result NVSDK_CONV hkNgx11Evaluate(ID3D11DeviceContext* c,const NVSDK_NGX_Handle* h,const NVSDK_NGX_Parameter* p,PFN_NVSDK_NGX_ProgressCallback cb){udlss::bridge::HookCallScope call;if(call.customWorkAllowed())captureNgx11(p);return g_origNgx11Evaluate(c,h,p,cb);}
#endif

void* resolveHookTarget(void* target){
    if(!target)return nullptr;
    auto* p=static_cast<std::uint8_t*>(target);
    for(int hop=0;hop<6 && p;hop++){
        MEMORY_BASIC_INFORMATION mbi{};
        if(!VirtualQuery(p,&mbi,sizeof(mbi)) || mbi.State!=MEM_COMMIT)return p;
        if(p[0]==0xE9){
            std::int32_t rel{};std::memcpy(&rel,p+1,sizeof(rel));
            p=p+5+rel;continue;
        }
        if(p[0]==0xEB){
            std::int8_t rel{};std::memcpy(&rel,p+1,sizeof(rel));
            p=p+2+rel;continue;
        }
#if defined(_WIN64)
        if(p[0]==0xFF && p[1]==0x25){
            std::int32_t disp{};std::memcpy(&disp,p+2,sizeof(disp));
            void* next{};std::memcpy(&next,p+6+disp,sizeof(next));
            if(next){p=static_cast<std::uint8_t*>(next);continue;}
        }
        if(p[0]==0x48 && p[1]==0xB8 && p[10]==0xFF && p[11]==0xE0){
            void* next{};std::memcpy(&next,p+2,sizeof(next));
            if(next){p=static_cast<std::uint8_t*>(next);continue;}
        }
#else
        if(p[0]==0xFF && p[1]==0x25){
            std::uint32_t slot{};std::memcpy(&slot,p+2,sizeof(slot));
            void* next{};std::memcpy(&next,reinterpret_cast<void*>(static_cast<std::uintptr_t>(slot)),sizeof(next));
            if(next){p=static_cast<std::uint8_t*>(next);continue;}
        }
#endif
        break;
    }
    return p;
}

bool createAndEnable(void* target,void* detour,void** original,void** installedTarget=nullptr){
    if(!target||!detour||!original)return false;
    auto tryHook=[&](void* candidate)->bool{
        if(!candidate)return false;
        const auto c=MH_CreateHook(candidate,detour,original);
        if(c==MH_ERROR_ALREADY_CREATED && !*original)return false;
        if(c!=MH_OK&&c!=MH_ERROR_ALREADY_CREATED)return false;
        const auto e=MH_EnableHook(candidate);
        if(e!=MH_OK&&e!=MH_ERROR_ENABLED)return false;
        if(installedTarget)*installedTarget=candidate;
        return true;
    };
    if(tryHook(target))return true;
    void* resolved=resolveHookTarget(target);
    return resolved!=target && tryHook(resolved);
}

#ifdef UDLSS_WITH_STREAMLINE
using SlGetPluginFunctionFn=void* (*)(const char*);

// Streamline plugins intentionally expose most callbacks through a single
// slGetPluginFunction gateway rather than as PE exports. Newer games that opt
// into frame-based resource tagging can therefore call slSetTagForFrame through
// a plugin-manager cached callback even when GetProcAddress(module,
// "slSetTagForFrame") is null. Resolve both surfaces so MinHook patches the
// actual callback body; cached function pointers then hit the detour as well.
void* resolveStreamlineFunction(HMODULE module,const char* name){
    if(!module||!name||!*name)return nullptr;
    if(auto direct=GetProcAddress(module,name))return reinterpret_cast<void*>(direct);
    auto gatewayProc=GetProcAddress(module,"slGetPluginFunction");
    if(!gatewayProc)return nullptr;
    const auto gateway=reinterpret_cast<SlGetPluginFunctionFn>(gatewayProc);
    return gateway(name);
}

template<class OriginalArray>
bool installSlTarget(void* target,std::array<void*,kMaxSlHookTargets>& targets,OriginalArray& originals,
                     const std::array<void*,kMaxSlHookTargets>& detours,std::atomic_bool& installedFlag){
    if(!target)return false;
    const void* resolved=resolveHookTarget(target);
    for(void* existing:targets)if(existing&&(existing==target||existing==resolved))return true;
    for(std::size_t i=0;i<kMaxSlHookTargets;++i){
        if(targets[i])continue;
        void* actual{};
        if(createAndEnable(target,detours[i],reinterpret_cast<void**>(&originals[i]),&actual)){
            targets[i]=actual?actual:target;installedFlag.store(true,std::memory_order_relaxed);return true;
        }
        return false;
    }
    return false;
}
#endif

std::uintptr_t queueIdentity(ID3D12CommandQueue* queue){
    if(!queue)return 0;ComPtr<IUnknown> id;
    if(FAILED(queue->QueryInterface(IID_PPV_ARGS(&id))))return reinterpret_cast<std::uintptr_t>(queue);
    return reinterpret_cast<std::uintptr_t>(id.Get());
}

D3D12_RESOURCE_BARRIER snapshotTransition(ID3D12Resource* resource,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after){
    D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition.pResource=resource;
    b.Transition.StateBefore=before;b.Transition.StateAfter=after;b.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;return b;
}

bool snapshotDescSupported(const D3D12_RESOURCE_DESC& d){
    return d.Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE2D&&d.SampleDesc.Count==1&&d.MipLevels==1;
}

DXGI_FORMAT providerSnapshotResourceFormat(DXGI_FORMAT viewFormat,bool depth){
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
    // Snapshot simple depth families into typed shader-readable representatives.
    // CopyResource permits reinterpret copies inside the same typeless group,
    // and typed R32/R16 resources survive D3D12 -> standalone D3D11 sharing
    // more reliably than engine-style typeless depth allocations.
    case DXGI_FORMAT_D32_FLOAT: case DXGI_FORMAT_R32_FLOAT: case DXGI_FORMAT_R32_TYPELESS:return DXGI_FORMAT_R32_FLOAT;
    case DXGI_FORMAT_D24_UNORM_S8_UINT: case DXGI_FORMAT_R24G8_TYPELESS: case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:return DXGI_FORMAT_R24G8_TYPELESS;
    case DXGI_FORMAT_D16_UNORM: case DXGI_FORMAT_R16_TYPELESS: case DXGI_FORMAT_R16_UNORM:return DXGI_FORMAT_R16_UNORM;
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT: case DXGI_FORMAT_R32G8X24_TYPELESS: case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:return DXGI_FORMAT_R32G8X24_TYPELESS;
    default:return DXGI_FORMAT_UNKNOWN;
    }
}

bool ensurePostSubmitResource(ID3D12Device* device,const D3D12_RESOURCE_DESC& sourceDesc,DXGI_FORMAT viewFormat,bool depth,ComPtr<ID3D12Resource>& resource,D3D12_RESOURCE_DESC& current){
    if(!device||!snapshotDescSupported(sourceDesc))return false;
    D3D12_RESOURCE_DESC preferred=sourceDesc;
    const auto normalizedFormat=providerSnapshotResourceFormat(viewFormat!=DXGI_FORMAT_UNKNOWN?viewFormat:sourceDesc.Format,depth);
    if(normalizedFormat!=DXGI_FORMAT_UNKNOWN){preferred.Format=normalizedFormat;preferred.Flags=sharedGuideTextureFlags(normalizedFormat);preferred.Layout=D3D12_TEXTURE_LAYOUT_UNKNOWN;}
    if(resource&&sameSnapshotDesc(current,preferred))return true;
    resource.Reset();current={};
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;heap.CreationNodeMask=1;heap.VisibleNodeMask=1;
    // Prefer a shareable SRV-friendly snapshot rather than preserving engine-only
    // depth-stencil/UAV flags. CopyResource permits formats from the same typeless
    // family, so D32/R32 and the other depth families can be normalized safely.
    HRESULT hr=device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_SHARED,&preferred,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&resource));
    if(FAILED(hr)||!resource){
        resource.Reset();
        // Conservative fallback for unusual descriptors: retain the exact game
        // description and let recovery stage it later if D3D11 cannot open it.
        hr=device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_SHARED,&sourceDesc,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&resource));
        if(FAILED(hr)||!resource){resource.Reset();hr=device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&sourceDesc,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&resource));}
    }
    if(FAILED(hr)||!resource)return false;
    current=resource->GetDesc();return true;
}

std::vector<HMODULE> loadedModules(){
    std::vector<HMODULE> out;
    auto add=[&](HMODULE m){if(m&&std::find(out.begin(),out.end(),m)==out.end())out.push_back(m);};
#ifdef UDLSS_WITH_STREAMLINE
    add(GetModuleHandleW(L"sl.interposer.dll"));
#endif
#ifdef UDLSS_WITH_NGX_NR
    add(GetModuleHandleW(L"nvngx.dll"));add(GetModuleHandleW(L"nvngx_dlss.dll"));add(GetModuleHandleW(L"nvsdk_ngx.dll"));
#endif
    const HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,GetCurrentProcessId());
    if(snap!=INVALID_HANDLE_VALUE){
        MODULEENTRY32W e{};e.dwSize=sizeof(e);
        if(Module32FirstW(snap,&e))do{add(reinterpret_cast<HMODULE>(e.modBaseAddr));e.dwSize=sizeof(e);}while(Module32NextW(snap,&e));
        CloseHandle(snap);
    }
    return out;
}
}

void setGameGuideCaptureSuppressed(bool value){g_suppressed=value;}
bool gameGuideCaptureSuppressed(){return g_suppressed;}
void setGameTemporalGuideCaptureMode(GameTemporalGuideCaptureMode mode){
    const auto previous=g_captureMode.exchange(mode,std::memory_order_relaxed);
    const bool enabled=mode!=GameTemporalGuideCaptureMode::Disabled;
    g_captureEnabled.store(enabled,std::memory_order_relaxed);
    if(previous!=mode){
        std::scoped_lock lock(g_mutex);
        if(g_capture.snapshotOwned)markSnapshotCaptureConsumedLocked(g_capture.captureSequence);
        g_capture={};g_pendingProviders.clear();
        for(auto& [_,queue]:g_postSubmitQueues)for(auto& slot:queue.slots){slot.captureConsumed=true;slot.stagedCapture={};}
    }
}
GameTemporalGuideCaptureMode gameTemporalGuideCaptureMode(){return g_captureMode.load(std::memory_order_relaxed);}
void setGameTemporalGuideCaptureEnabled(bool value){setGameTemporalGuideCaptureMode(value?GameTemporalGuideCaptureMode::Full:GameTemporalGuideCaptureMode::Disabled);}
bool gameTemporalGuideCaptureEnabled(){return g_captureEnabled.load(std::memory_order_relaxed);}

bool pendingGameTemporalGuideSnapshotActive(){
    if(g_captureMode.load(std::memory_order_relaxed)!=GameTemporalGuideCaptureMode::SnapshotOnly)return false;
    std::scoped_lock lock(g_mutex);const auto now=GetTickCount64();pruneStalePendingProvidersLocked(now);
    return !g_pendingProviders.empty();
}

bool snapshotPendingGameTemporalGuidesAfterExecute(ID3D12CommandQueue* queue,ID3D12CommandList* commandList,D3D12ExecuteCommandListsFn originalExecute){
    if(!queue||!commandList||!originalExecute||queue->GetDesc().Type!=D3D12_COMMAND_LIST_TYPE_DIRECT)return false;
    if(g_captureMode.load(std::memory_order_relaxed)!=GameTemporalGuideCaptureMode::SnapshotOnly)return false;
    const auto listId=commandListIdentity(commandList);if(!listId)return false;
    const auto qid=queueIdentity(queue);if(!qid)return false;

    PendingProviderGuides pending{};std::size_t slotIndex=0;ComPtr<ID3D12GraphicsCommandList> copyList;ComPtr<ID3D12Fence> fence;
    std::uint64_t fenceValue{};
    {
        std::unique_lock lock(g_mutex);
        const auto now=GetTickCount64();pruneStalePendingProvidersLocked(now);
        auto pit=g_pendingProviders.find(listId);if(pit==g_pendingProviders.end()||!pit->second)return false;
        pending=std::move(pit->second);g_pendingProviders.erase(pit);

        ComPtr<ID3D12Device> device;if(FAILED(queue->GetDevice(IID_PPV_ARGS(&device)))||!device)return false;
        auto& qs=g_postSubmitQueues[qid];if(!qs.queue)qs.queue=queue;
        if(!qs.fence&&FAILED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&qs.fence))))return false;
        const auto completed=qs.fence->GetCompletedValue();
        bool found=false;
        for(std::size_t i=0;i<qs.slots.size();++i){
            auto& candidate=qs.slots[i];
            const bool neverSubmitted=candidate.fenceValue==0;
            const bool gpuDone=candidate.fenceValue!=0&&candidate.fenceValue!=UINT64_MAX&&completed>=candidate.fenceValue;
            if((neverSubmitted||gpuDone)&&candidate.captureConsumed){slotIndex=i;found=true;break;}
        }
        if(!found)return false;
        auto& slot=qs.slots[slotIndex];slot.stagedCapture={};
        const bool newCommandObjects=!slot.allocator||!slot.list;
        if(newCommandObjects){
            if(FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&slot.allocator)))||
               FAILED(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,slot.allocator.Get(),nullptr,IID_PPV_ARGS(&slot.list))))return false;
        }else if(FAILED(slot.allocator->Reset())||FAILED(slot.list->Reset(slot.allocator.Get(),nullptr)))return false;
        if(pending.depth&&!ensurePostSubmitResource(device.Get(),pending.depthDesc,pending.depthFormat,true,slot.depth,slot.depthDesc))return false;
        if(pending.motion&&!ensurePostSubmitResource(device.Get(),pending.motionDesc,pending.motionFormat,false,slot.motion,slot.motionDesc))return false;

        std::array<D3D12_RESOURCE_BARRIER,8> barriers{};UINT count=0;
        if(pending.depth){
            if(pending.depthState!=D3D12_RESOURCE_STATE_COPY_SOURCE)barriers[count++]=snapshotTransition(pending.depth.Get(),pending.depthState,D3D12_RESOURCE_STATE_COPY_SOURCE);
            barriers[count++]=snapshotTransition(slot.depth.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST);
        }
        if(pending.motion){
            if(pending.motionState!=D3D12_RESOURCE_STATE_COPY_SOURCE)barriers[count++]=snapshotTransition(pending.motion.Get(),pending.motionState,D3D12_RESOURCE_STATE_COPY_SOURCE);
            barriers[count++]=snapshotTransition(slot.motion.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST);
        }
        if(count)slot.list->ResourceBarrier(count,barriers.data());
        if(pending.depth)slot.list->CopyResource(slot.depth.Get(),pending.depth.Get());
        if(pending.motion)slot.list->CopyResource(slot.motion.Get(),pending.motion.Get());
        count=0;
        if(pending.depth){
            barriers[count++]=snapshotTransition(slot.depth.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COMMON);
            if(pending.depthState!=D3D12_RESOURCE_STATE_COPY_SOURCE)barriers[count++]=snapshotTransition(pending.depth.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,pending.depthState);
        }
        if(pending.motion){
            barriers[count++]=snapshotTransition(slot.motion.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COMMON);
            if(pending.motionState!=D3D12_RESOURCE_STATE_COPY_SOURCE)barriers[count++]=snapshotTransition(pending.motion.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,pending.motionState);
        }
        if(count)slot.list->ResourceBarrier(count,barriers.data());
        if(FAILED(slot.list->Close()))return false;
        slot.captureConsumed=false;slot.fenceValue=UINT64_MAX;
        copyList=slot.list;fence=qs.fence;fenceValue=qs.nextFenceValue++;
    }

    ID3D12CommandList* lists[]={copyList.Get()};originalExecute(queue,1,lists);
    if(FAILED(queue->Signal(fence.Get(),fenceValue)))return false;

    CapturedGameGuides captured{};captured.source=pending.source;captured.provider=pending.provider;
    if(captured.provider.empty())captured.provider=pending.source==GameGuideSource::Ngx?L"NGX game evaluation":L"Streamline game tags";
    captured.provider+=L" (post-submit owned snapshot)";
    captured.capturedTickMs=GetTickCount64();captured.validUntilPresent=true;captured.snapshotOwned=true;
    captured.confidence=pending.confidence;captured.cameraCut=pending.cameraCut;captured.depthInverted=pending.depthInverted;captured.depthConventionKnown=pending.depthConventionKnown;
    captured.motionConventionKnown=pending.motionConventionKnown;captured.motionToPixelScaleX=pending.motionToPixelScaleX;captured.motionToPixelScaleY=pending.motionToPixelScaleY;
    captured.providerCommandListId=listId;captured.producerQueueId=qid;captured.providerViewportId=pending.providerViewportId;
    {
        std::scoped_lock lock(g_mutex);auto qit=g_postSubmitQueues.find(qid);if(qit==g_postSubmitQueues.end())return false;auto& slot=qit->second.slots[slotIndex];
        slot.fenceValue=fenceValue;
        captured.captureSequence=g_nextCaptureSequence++;
        if(pending.depth){captured.depth12=slot.depth;captured.depthFormat=pending.depthFormat;captured.depthState=D3D12_RESOURCE_STATE_COMMON;captured.depthWidth=pending.depthWidth;captured.depthHeight=pending.depthHeight;captured.depthSnapshotOwned=true;}
        if(pending.motion){captured.motion12=slot.motion;captured.motionFormat=pending.motionFormat;captured.motionState=D3D12_RESOURCE_STATE_COMMON;captured.motionWidth=pending.motionWidth;captured.motionHeight=pending.motionHeight;captured.motionSnapshotOwned=true;}
        slot.stagedCapture=captured;slot.captureConsumed=false;
    }
    if(pending.source==GameGuideSource::Ngx)g_ngxPostSubmitSnapshots.fetch_add(1,std::memory_order_relaxed);
    else g_streamlinePostSubmitSnapshots.fetch_add(1,std::memory_order_relaxed);
    return true;
}

void noteGameTemporalGuideCommandListExecuted(ID3D12CommandList* commandList,std::uintptr_t queueId){
    if(!commandList||!queueId)return;
    const auto id=commandListIdentity(commandList);if(!id)return;
    std::scoped_lock lock(g_mutex);
    if(g_capture.source==GameGuideSource::Streamline&&g_capture.providerCommandListId==id&&!g_capture.snapshotOwned)g_capture.producerQueueId=queueId;
}

void releaseCapturedGameTemporalGuides(){
    std::scoped_lock lock(g_mutex);
    if(g_capture.snapshotOwned)markSnapshotCaptureConsumedLocked(g_capture.captureSequence);
    g_capture={};g_pendingProviders.clear();
    for(auto& [_,queue]:g_postSubmitQueues)for(auto& slot:queue.slots){slot.captureConsumed=true;slot.stagedCapture={};}
}
void consumeCapturedGameTemporalGuides(std::uint64_t captureSequence){
    std::scoped_lock lock(g_mutex);
    if(!shouldConsumeGuideGeneration(g_capture.captureSequence,captureSequence))return;
    if(g_capture.snapshotOwned)markSnapshotCaptureConsumedLocked(g_capture.captureSequence);
    g_capture={};
}
void resetGameTemporalGuides(){releaseCapturedGameTemporalGuides();
#ifdef UDLSS_WITH_STREAMLINE
 std::scoped_lock lock(g_mutex);g_slConstants={};g_slConstantsByViewport.clear();g_pendingProviders.clear();
#endif
}

CapturedGameGuides snapshotGameTemporalGuides(ID3D11Device* device,std::uint64_t nowMs){return snapshotFor11(device,nowMs);}
CapturedGameGuides snapshotGameTemporalGuides(ID3D12Device* device,std::uint64_t nowMs){return snapshotFor12(device,nowMs);}

bool installGameTemporalGuideHooks(){
    bool any=false;
    const auto modules=loadedModules();
    for(HMODULE m:modules){
        if(!m)continue;
#ifdef UDLSS_WITH_STREAMLINE
        const auto pluginGateway=GetProcAddress(m,"slGetPluginFunction");
        const auto setTagForFrame=resolveStreamlineFunction(m,"slSetTagForFrame");
        const auto setTag=resolveStreamlineFunction(m,"slSetTag");
        const auto setConstants=resolveStreamlineFunction(m,"slSetConstants");
        const auto evaluate=resolveStreamlineFunction(m,"slEvaluateFeature");
        if(pluginGateway||setTagForFrame||setTag||setConstants||evaluate)g_streamlineModuleSeen.store(true,std::memory_order_relaxed);
        static const std::array<void*,kMaxSlHookTargets> tagFDetours={reinterpret_cast<void*>(&hkSlSetTagForFrame<0>),reinterpret_cast<void*>(&hkSlSetTagForFrame<1>),reinterpret_cast<void*>(&hkSlSetTagForFrame<2>),reinterpret_cast<void*>(&hkSlSetTagForFrame<3>),reinterpret_cast<void*>(&hkSlSetTagForFrame<4>),reinterpret_cast<void*>(&hkSlSetTagForFrame<5>),reinterpret_cast<void*>(&hkSlSetTagForFrame<6>),reinterpret_cast<void*>(&hkSlSetTagForFrame<7>)};
        static const std::array<void*,kMaxSlHookTargets> tagDetours={reinterpret_cast<void*>(&hkSlSetTag<0>),reinterpret_cast<void*>(&hkSlSetTag<1>),reinterpret_cast<void*>(&hkSlSetTag<2>),reinterpret_cast<void*>(&hkSlSetTag<3>),reinterpret_cast<void*>(&hkSlSetTag<4>),reinterpret_cast<void*>(&hkSlSetTag<5>),reinterpret_cast<void*>(&hkSlSetTag<6>),reinterpret_cast<void*>(&hkSlSetTag<7>)};
        static const std::array<void*,kMaxSlHookTargets> constantsDetours={reinterpret_cast<void*>(&hkSlSetConstants<0>),reinterpret_cast<void*>(&hkSlSetConstants<1>),reinterpret_cast<void*>(&hkSlSetConstants<2>),reinterpret_cast<void*>(&hkSlSetConstants<3>),reinterpret_cast<void*>(&hkSlSetConstants<4>),reinterpret_cast<void*>(&hkSlSetConstants<5>),reinterpret_cast<void*>(&hkSlSetConstants<6>),reinterpret_cast<void*>(&hkSlSetConstants<7>)};
        static const std::array<void*,kMaxSlHookTargets> evalDetours={reinterpret_cast<void*>(&hkSlEvaluateFeature<0>),reinterpret_cast<void*>(&hkSlEvaluateFeature<1>),reinterpret_cast<void*>(&hkSlEvaluateFeature<2>),reinterpret_cast<void*>(&hkSlEvaluateFeature<3>),reinterpret_cast<void*>(&hkSlEvaluateFeature<4>),reinterpret_cast<void*>(&hkSlEvaluateFeature<5>),reinterpret_cast<void*>(&hkSlEvaluateFeature<6>),reinterpret_cast<void*>(&hkSlEvaluateFeature<7>)};
        any|=installSlTarget((void*)setTagForFrame,g_slSetTagForFrameTargets,g_origSlSetTagForFrameTargets,tagFDetours,g_slSetTagForFrameInstalled);
        any|=installSlTarget((void*)setTag,g_slSetTagTargets,g_origSlSetTagTargets,tagDetours,g_slSetTagInstalled);
        any|=installSlTarget((void*)setConstants,g_slSetConstantsTargets,g_origSlSetConstantsTargets,constantsDetours,g_slSetConstantsInstalled);
        any|=installSlTarget((void*)evaluate,g_slEvaluateTargets,g_origSlEvaluateTargets,evalDetours,g_slEvaluateInstalled);
        if(g_slSetTagForFrameInstalled.load(std::memory_order_relaxed)||g_slSetTagInstalled.load(std::memory_order_relaxed)||g_slSetConstantsInstalled.load(std::memory_order_relaxed)||g_slEvaluateInstalled.load(std::memory_order_relaxed))g_streamlineHooksInstalled.store(true,std::memory_order_relaxed);
#endif
#ifdef UDLSS_WITH_NGX_NR
        const auto ngx12=GetProcAddress(m,"NVSDK_NGX_D3D12_EvaluateFeature");
        const auto ngx11=GetProcAddress(m,"NVSDK_NGX_D3D11_EvaluateFeature");
        if(ngx12||ngx11)g_ngxModuleSeen.store(true,std::memory_order_relaxed);
        if(!g_origNgx12Evaluate&&ngx12){if(createAndEnable((void*)ngx12,(void*)hkNgx12Evaluate,(void**)&g_origNgx12Evaluate)){any=true;g_ngx12HookInstalled.store(true,std::memory_order_relaxed);}}else if(g_origNgx12Evaluate){any=true;g_ngx12HookInstalled.store(true,std::memory_order_relaxed);}
        if(!g_origNgx11Evaluate&&ngx11){if(createAndEnable((void*)ngx11,(void*)hkNgx11Evaluate,(void**)&g_origNgx11Evaluate)){any=true;g_ngx11HookInstalled.store(true,std::memory_order_relaxed);}}else if(g_origNgx11Evaluate){any=true;g_ngx11HookInstalled.store(true,std::memory_order_relaxed);}
#endif
    }
    return any;
}

std::wstring gameTemporalGuideHookDiagnostics(){
    std::wostringstream ss;
    // RuntimeStatus::guideFields is intentionally small, so use compact field
    // names while preserving the counters needed to distinguish hook, binding,
    // snapshot and convention failures.
    ss<<L"SL tagF="<<g_slSetTagForFrameCalls.load(std::memory_order_relaxed)
      <<L" T="<<g_slSetTagCalls.load(std::memory_order_relaxed)
      <<L" C="<<g_slSetConstantsCalls.load(std::memory_order_relaxed)
      <<L" E="<<g_slEvaluateCalls.load(std::memory_order_relaxed)
      <<L" D="<<g_slDepthTags.load(std::memory_order_relaxed)
      <<L" M="<<g_slMotionTags.load(std::memory_order_relaxed)
      <<L" N="<<g_slNullResourceTags.load(std::memory_order_relaxed)
      <<L" A="<<g_acceptedCaptures.load(std::memory_order_relaxed)
      <<L" S="<<g_streamlinePostSubmitSnapshots.load(std::memory_order_relaxed)
      <<L" R="<<g_ownedSnapshotReads.load(std::memory_order_relaxed)
      <<L" B="<<g_streamlineEvaluationBinds.load(std::memory_order_relaxed)
      <<L" Q="<<g_streamlineBoundSnapshotQueues.load(std::memory_order_relaxed)
      <<L" P="<<g_ownedSnapshotPromotions.load(std::memory_order_relaxed)
      <<L" U="<<g_streamlineRejectedUnknownState.load(std::memory_order_relaxed)
      <<L" X="<<g_providerPendingEvictions.load(std::memory_order_relaxed)
      <<L" I="<<(g_slSetTagForFrameInstalled.load(std::memory_order_relaxed)?1:0)
      <<(g_slSetTagInstalled.load(std::memory_order_relaxed)?1:0)
      <<(g_slSetConstantsInstalled.load(std::memory_order_relaxed)?1:0)
      <<(g_slEvaluateInstalled.load(std::memory_order_relaxed)?1:0)
      <<L" G="<<g_ngx12HookHits.load(std::memory_order_relaxed);
    return ss.str();
}

} // namespace udlss::gpu
