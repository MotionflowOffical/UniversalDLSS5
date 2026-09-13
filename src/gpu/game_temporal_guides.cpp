#include "game_temporal_guides.hpp"
#include "../bridge/hook_lifecycle.hpp"
#include <MinHook.h>
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <mutex>
#include <string_view>
#include <utility>
#include <limits>

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
thread_local bool g_suppressed=false;

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

void mergeCapture(CapturedGameGuides incoming){
    if(!incoming)return;
    std::scoped_lock lock(g_mutex);
    const auto inPriority=guideSourcePriority(incoming.source),curPriority=guideSourcePriority(g_capture.source);
    const bool newer=incoming.capturedTickMs>=g_capture.capturedTickMs;
    if(inPriority<curPriority && guideCaptureFresh(g_capture.capturedTickMs,incoming.capturedTickMs,1000))return;
    if(inPriority==curPriority && !newer)return;
    // Preserve a guide from the same provider when APIs tag depth and motion in separate calls.
    if(incoming.source==g_capture.source && guideCaptureFresh(g_capture.capturedTickMs,incoming.capturedTickMs,100)){
        if(!incoming.depth11&&!incoming.depth12){incoming.depth11=g_capture.depth11;incoming.depth12=g_capture.depth12;incoming.depthFormat=g_capture.depthFormat;incoming.depthState=g_capture.depthState;incoming.depthWidth=g_capture.depthWidth;incoming.depthHeight=g_capture.depthHeight;incoming.depthInverted=g_capture.depthInverted;incoming.depthConventionKnown=g_capture.depthConventionKnown;}
        if(!incoming.motion11&&!incoming.motion12){incoming.motion11=g_capture.motion11;incoming.motion12=g_capture.motion12;incoming.motionFormat=g_capture.motionFormat;incoming.motionState=g_capture.motionState;incoming.motionWidth=g_capture.motionWidth;incoming.motionHeight=g_capture.motionHeight;incoming.motionToPixelScaleX=g_capture.motionToPixelScaleX;incoming.motionToPixelScaleY=g_capture.motionToPixelScaleY;incoming.motionConventionKnown=g_capture.motionConventionKnown;}
        incoming.cameraCut=incoming.cameraCut||g_capture.cameraCut;
        incoming.validUntilPresent=incoming.validUntilPresent&&g_capture.validUntilPresent;
    }
    g_capture=std::move(incoming);
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
    std::scoped_lock lock(g_mutex);auto out=g_capture;
    const auto maxAge=out.validUntilPresent?750ull:50ull;
    if(!guideCaptureFresh(out.capturedTickMs,now,maxAge))return{};
    ComPtr<ID3D12Device> owner;
    if(out.depth12)out.depth12->GetDevice(IID_PPV_ARGS(&owner));else if(out.motion12)out.motion12->GetDevice(IID_PPV_ARGS(&owner));
    if(!owner||!sameComObject(owner.Get(),device))return{};
    out.depth11.Reset();out.motion11.Reset();return out;
}

#ifdef UDLSS_WITH_STREAMLINE
PFun_slSetTagForFrame* g_origSlSetTagForFrame{};
PFun_slSetTag* g_origSlSetTag{};
PFun_slSetConstants* g_origSlSetConstants{};
PFun_slEvaluateFeature* g_origSlEvaluateFeature{};
struct SlConstantsSnapshot { float sx{1},sy{1};bool depthInverted{true},depthKnown{},reset{};std::uint64_t tick{}; } g_slConstants;

void captureSlTag(const sl::ResourceTag& tag,bool localEvaluation){
    if(g_suppressed||!tag.resource||!tag.resource->native)return;
    if(tag.type!=sl::kBufferTypeDepth && tag.type!=sl::kBufferTypeMotionVectors)return;
    CapturedGameGuides c{};c.source=GameGuideSource::Streamline;c.provider=L"Streamline game tags";c.capturedTickMs=GetTickCount64();
    c.validUntilPresent=tag.lifecycle==sl::eValidUntilPresent;
    c.confidence=c.validUntilPresent?100u:(localEvaluation?94u:82u);
    IUnknown* native=reinterpret_cast<IUnknown*>(tag.resource->native);
    ComPtr<ID3D11Texture2D> t11;ComPtr<ID3D12Resource> t12;
    const auto extent=tag.extent;
    const auto nativeFormat=static_cast<DXGI_FORMAT>(tag.resource->nativeFormat);
    if(SUCCEEDED(native->QueryInterface(IID_PPV_ARGS(&t11)))){
        DXGI_FORMAT fmt{};std::uint32_t w{},h{};describe11(t11.Get(),fmt,w,h);if(nativeFormat!=DXGI_FORMAT_UNKNOWN)fmt=nativeFormat;if(extent.width&&extent.height){w=extent.width;h=extent.height;}
        if(tag.type==sl::kBufferTypeDepth){c.depth11=t11;c.depthFormat=fmt;c.depthWidth=w;c.depthHeight=h;}
        else{c.motion11=t11;c.motionFormat=fmt;c.motionWidth=w;c.motionHeight=h;}
    }else if(SUCCEEDED(native->QueryInterface(IID_PPV_ARGS(&t12)))){
        DXGI_FORMAT fmt{};std::uint32_t w{},h{};describe12(t12.Get(),fmt,w,h);if(nativeFormat!=DXGI_FORMAT_UNKNOWN)fmt=nativeFormat;if(extent.width&&extent.height){w=extent.width;h=extent.height;}
        const auto state=static_cast<D3D12_RESOURCE_STATES>(tag.resource->state==std::numeric_limits<uint32_t>::max()?D3D12_RESOURCE_STATE_COMMON:tag.resource->state);
        if(tag.type==sl::kBufferTypeDepth){c.depth12=t12;c.depthFormat=fmt;c.depthState=state;c.depthWidth=w;c.depthHeight=h;}
        else{c.motion12=t12;c.motionFormat=fmt;c.motionState=state;c.motionWidth=w;c.motionHeight=h;}
    }else return;
    {
        std::scoped_lock lock(g_mutex);
        const bool constantsFresh=guideCaptureFresh(g_slConstants.tick,c.capturedTickMs,100);
        if(constantsFresh){
            c.depthInverted=g_slConstants.depthInverted;c.depthConventionKnown=g_slConstants.depthKnown;c.cameraCut=g_slConstants.reset;
            const bool scaleXKnown=c.motionWidth&&std::isfinite(g_slConstants.sx)&&std::abs(g_slConstants.sx)<16.0f;
            const bool scaleYKnown=c.motionHeight&&std::isfinite(g_slConstants.sy)&&std::abs(g_slConstants.sy)<16.0f;
            if(scaleXKnown)c.motionToPixelScaleX=g_slConstants.sx*(float)c.motionWidth;
            if(scaleYKnown)c.motionToPixelScaleY=g_slConstants.sy*(float)c.motionHeight;
            if(c.motion11||c.motion12)c.motionConventionKnown=scaleXKnown&&scaleYKnown;
        }
    }
    mergeCapture(std::move(c));
}

sl::Result hkSlSetTagForFrame(const sl::FrameToken& frame,const sl::ViewportHandle& viewport,const sl::ResourceTag* tags,uint32_t count,sl::CommandBuffer* cmd){
    udlss::bridge::HookCallScope call;
    if(call.customWorkAllowed()&&!g_suppressed&&tags)for(uint32_t i=0;i<count;i++)captureSlTag(tags[i],false);
    return g_origSlSetTagForFrame(frame,viewport,tags,count,cmd);
}
sl::Result hkSlSetTag(const sl::ViewportHandle& viewport,const sl::ResourceTag* tags,uint32_t count,sl::CommandBuffer* cmd){
    udlss::bridge::HookCallScope call;
    if(call.customWorkAllowed()&&!g_suppressed&&tags)for(uint32_t i=0;i<count;i++)captureSlTag(tags[i],false);
    return g_origSlSetTag(viewport,tags,count,cmd);
}
sl::Result hkSlSetConstants(const sl::Constants& values,const sl::FrameToken& frame,const sl::ViewportHandle& viewport){
    udlss::bridge::HookCallScope call;
    if(call.customWorkAllowed()&&!g_suppressed){std::scoped_lock lock(g_mutex);g_slConstants.sx=values.mvecScale.x;g_slConstants.sy=values.mvecScale.y;g_slConstants.depthKnown=values.depthInverted!=sl::Boolean::eInvalid;g_slConstants.depthInverted=values.depthInverted==sl::Boolean::eTrue;g_slConstants.reset=values.reset==sl::Boolean::eTrue;g_slConstants.tick=GetTickCount64();}
    return g_origSlSetConstants(values,frame,viewport);
}
sl::Result hkSlEvaluateFeature(sl::Feature feature,const sl::FrameToken& frame,const sl::BaseStructure** inputs,uint32_t count,sl::CommandBuffer* cmd){
    udlss::bridge::HookCallScope call;
    if(call.customWorkAllowed()&&!g_suppressed&&inputs){for(uint32_t i=0;i<count;i++){const auto* base=inputs[i];if(base&&base->structType==sl::ResourceTag::s_structType)captureSlTag(*static_cast<const sl::ResourceTag*>(base),true);}}
    return g_origSlEvaluateFeature(feature,frame,inputs,count,cmd);
}
#endif

#ifdef UDLSS_WITH_NGX_NR
using Ngx12EvaluateFn=NVSDK_NGX_Result (NVSDK_CONV*)(ID3D12GraphicsCommandList*,const NVSDK_NGX_Handle*,const NVSDK_NGX_Parameter*,PFN_NVSDK_NGX_ProgressCallback);
using Ngx11EvaluateFn=NVSDK_NGX_Result (NVSDK_CONV*)(ID3D11DeviceContext*,const NVSDK_NGX_Handle*,const NVSDK_NGX_Parameter*,PFN_NVSDK_NGX_ProgressCallback);
Ngx12EvaluateFn g_origNgx12Evaluate{};Ngx11EvaluateFn g_origNgx11Evaluate{};

void captureNgx12(const NVSDK_NGX_Parameter* p){
    if(g_suppressed||!p)return;ID3D12Resource* depth{};ID3D12Resource* motion{};
    p->Get(NVSDK_NGX_Parameter_Depth,&depth);p->Get(NVSDK_NGX_Parameter_MotionVectors,&motion);if(!depth&&!motion)return;
    CapturedGameGuides c{};c.source=GameGuideSource::Ngx;c.provider=L"NGX game evaluation";c.capturedTickMs=GetTickCount64();c.confidence=96;c.validUntilPresent=false;
    if(depth){c.depth12=depth;describe12(depth,c.depthFormat,c.depthWidth,c.depthHeight);c.depthState=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;}
    if(motion){c.motion12=motion;describe12(motion,c.motionFormat,c.motionWidth,c.motionHeight);c.motionState=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;c.motionConventionKnown=true;}
    float sx=1,sy=1;if(p->Get(NVSDK_NGX_Parameter_MV_Scale_X,&sx)==NVSDK_NGX_Result_Success)c.motionToPixelScaleX=sx;if(p->Get(NVSDK_NGX_Parameter_MV_Scale_Y,&sy)==NVSDK_NGX_Result_Success)c.motionToPixelScaleY=sy;
    int flags=0;if(p->Get(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags,&flags)==NVSDK_NGX_Result_Success){c.depthConventionKnown=true;c.depthInverted=(flags&NVSDK_NGX_DLSS_Feature_Flags_DepthInverted)!=0;}
    int reset=0;if(p->Get(NVSDK_NGX_Parameter_Reset,&reset)==NVSDK_NGX_Result_Success)c.cameraCut=reset!=0;
    mergeCapture(std::move(c));
}
void captureNgx11(const NVSDK_NGX_Parameter* p){
    if(g_suppressed||!p)return;ID3D11Resource* depth{};ID3D11Resource* motion{};
    p->Get(NVSDK_NGX_Parameter_Depth,&depth);p->Get(NVSDK_NGX_Parameter_MotionVectors,&motion);if(!depth&&!motion)return;
    CapturedGameGuides c{};c.source=GameGuideSource::Ngx;c.provider=L"NGX game evaluation";c.capturedTickMs=GetTickCount64();c.confidence=96;c.validUntilPresent=false;
    if(depth){ComPtr<ID3D11Texture2D> t;if(SUCCEEDED(depth->QueryInterface(IID_PPV_ARGS(&t)))){c.depth11=t;describe11(t.Get(),c.depthFormat,c.depthWidth,c.depthHeight);}}
    if(motion){ComPtr<ID3D11Texture2D> t;if(SUCCEEDED(motion->QueryInterface(IID_PPV_ARGS(&t)))){c.motion11=t;describe11(t.Get(),c.motionFormat,c.motionWidth,c.motionHeight);c.motionConventionKnown=true;}}
    float sx=1,sy=1;if(p->Get(NVSDK_NGX_Parameter_MV_Scale_X,&sx)==NVSDK_NGX_Result_Success)c.motionToPixelScaleX=sx;if(p->Get(NVSDK_NGX_Parameter_MV_Scale_Y,&sy)==NVSDK_NGX_Result_Success)c.motionToPixelScaleY=sy;
    int flags=0;if(p->Get(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags,&flags)==NVSDK_NGX_Result_Success){c.depthConventionKnown=true;c.depthInverted=(flags&NVSDK_NGX_DLSS_Feature_Flags_DepthInverted)!=0;}
    int reset=0;if(p->Get(NVSDK_NGX_Parameter_Reset,&reset)==NVSDK_NGX_Result_Success)c.cameraCut=reset!=0;
    mergeCapture(std::move(c));
}
NVSDK_NGX_Result NVSDK_CONV hkNgx12Evaluate(ID3D12GraphicsCommandList* l,const NVSDK_NGX_Handle* h,const NVSDK_NGX_Parameter* p,PFN_NVSDK_NGX_ProgressCallback cb){udlss::bridge::HookCallScope call;if(call.customWorkAllowed())captureNgx12(p);return g_origNgx12Evaluate(l,h,p,cb);}
NVSDK_NGX_Result NVSDK_CONV hkNgx11Evaluate(ID3D11DeviceContext* c,const NVSDK_NGX_Handle* h,const NVSDK_NGX_Parameter* p,PFN_NVSDK_NGX_ProgressCallback cb){udlss::bridge::HookCallScope call;if(call.customWorkAllowed())captureNgx11(p);return g_origNgx11Evaluate(c,h,p,cb);}
#endif

bool createAndEnable(void* target,void* detour,void** original){
    if(!target)return false;const auto c=MH_CreateHook(target,detour,original);if(c!=MH_OK&&c!=MH_ERROR_ALREADY_CREATED)return false;
    const auto e=MH_EnableHook(target);return e==MH_OK||e==MH_ERROR_ENABLED;
}
}

void setGameGuideCaptureSuppressed(bool value){g_suppressed=value;}
bool gameGuideCaptureSuppressed(){return g_suppressed;}
void resetGameTemporalGuides(){std::scoped_lock lock(g_mutex);g_capture={};
#ifdef UDLSS_WITH_STREAMLINE
 g_slConstants={};
#endif
}
CapturedGameGuides snapshotGameTemporalGuides(ID3D11Device* device,std::uint64_t nowMs){return snapshotFor11(device,nowMs);}
CapturedGameGuides snapshotGameTemporalGuides(ID3D12Device* device,std::uint64_t nowMs){return snapshotFor12(device,nowMs);}

bool installGameTemporalGuideHooks(){
    bool any=false;
#ifdef UDLSS_WITH_STREAMLINE
    if(HMODULE slm=GetModuleHandleW(L"sl.interposer.dll")){
        if(!g_origSlSetTagForFrame)any|=createAndEnable((void*)GetProcAddress(slm,"slSetTagForFrame"),(void*)hkSlSetTagForFrame,(void**)&g_origSlSetTagForFrame);else any=true;
        if(!g_origSlSetTag)any|=createAndEnable((void*)GetProcAddress(slm,"slSetTag"),(void*)hkSlSetTag,(void**)&g_origSlSetTag);else any=true;
        if(!g_origSlSetConstants)any|=createAndEnable((void*)GetProcAddress(slm,"slSetConstants"),(void*)hkSlSetConstants,(void**)&g_origSlSetConstants);else any=true;
        if(!g_origSlEvaluateFeature)any|=createAndEnable((void*)GetProcAddress(slm,"slEvaluateFeature"),(void*)hkSlEvaluateFeature,(void**)&g_origSlEvaluateFeature);else any=true;
    }
#endif
#ifdef UDLSS_WITH_NGX_NR
    constexpr const wchar_t* modules[]={L"nvngx.dll",L"nvngx_dlss.dll",L"nvsdk_ngx.dll"};
    for(const auto* name:modules){HMODULE m=GetModuleHandleW(name);if(!m)continue;
        if(!g_origNgx12Evaluate){if(auto* p=GetProcAddress(m,"NVSDK_NGX_D3D12_EvaluateFeature"))any|=createAndEnable((void*)p,(void*)hkNgx12Evaluate,(void**)&g_origNgx12Evaluate);}else any=true;
        if(!g_origNgx11Evaluate){if(auto* p=GetProcAddress(m,"NVSDK_NGX_D3D11_EvaluateFeature"))any|=createAndEnable((void*)p,(void*)hkNgx11Evaluate,(void**)&g_origNgx11Evaluate);}else any=true;
    }
#endif
    return any;
}

} // namespace udlss::gpu
