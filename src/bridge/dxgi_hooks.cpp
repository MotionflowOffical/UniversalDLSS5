#include "dxgi_hooks.hpp"
#include "../gpu/d3d11_pipeline.hpp"
#include "../gpu/d3d12_on12.hpp"
#include "../gpu/d3d11_resource_tracker.hpp"
#include "../gpu/d3d11_camera_tracker.hpp"
#include "../gpu/d3d12_resource_tracker.hpp"
#include "../gpu/game_temporal_guides.hpp"
#include "../gpu/swapchain_color.hpp"
#include "../neural/nvidia_driver_win.hpp"
#include "attach_logger.hpp"
#include <d3d11.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <MinHook.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <memory>
#include <mutex>
#include <filesystem>
#include <chrono>
#include "udlss/control_generation.hpp"
#include "udlss/swapchain_policy.hpp"
#include "udlss/safe_attach_policy.hpp"
#include "udlss/present_queue_policy.hpp"

using Microsoft::WRL::ComPtr;
namespace fs=std::filesystem;
namespace udlss::bridge {
namespace {
using PresentFn=HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*,UINT,UINT);
using Present1Fn=HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain1*,UINT,UINT,const DXGI_PRESENT_PARAMETERS*);
using ResizeFn=HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*,UINT,UINT,UINT,DXGI_FORMAT,UINT);
using CreateSwapFn=HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory*,IUnknown*,DXGI_SWAP_CHAIN_DESC*,IDXGISwapChain**);
using CreateHwndFn=HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory2*,IUnknown*,HWND,const DXGI_SWAP_CHAIN_DESC1*,const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*,IDXGIOutput*,IDXGISwapChain1**);
using CreateCoreFn=HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory2*,IUnknown*,IUnknown*,const DXGI_SWAP_CHAIN_DESC1*,IDXGIOutput*,IDXGISwapChain1**);
using CreateCompFn=HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory2*,IUnknown*,const DXGI_SWAP_CHAIN_DESC1*,IDXGIOutput*,IDXGISwapChain1**);
using ExecuteFn=void(STDMETHODCALLTYPE*)(ID3D12CommandQueue*,UINT,ID3D12CommandList*const*);
using SetFullscreenFn=HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*,BOOL,IDXGIOutput*);
using SetColorSpaceFn=HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain3*,DXGI_COLOR_SPACE_TYPE);
using SetHdrMetaFn=HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain4*,DXGI_HDR_METADATA_TYPE,UINT,void*);
using Resize1Fn=HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain3*,UINT,UINT,UINT,DXGI_FORMAT,UINT,const UINT*,IUnknown*const*);
PresentFn origPresent{};Present1Fn origPresent1{};ResizeFn origResize{};CreateSwapFn origCreateSwap{};CreateHwndFn origCreateHwnd{};CreateCoreFn origCreateCore{};CreateCompFn origCreateComp{};ExecuteFn origExecute{};SetFullscreenFn origSetFullscreen{};SetColorSpaceFn origSetColorSpace{};SetHdrMetaFn origSetHdrMeta{};Resize1Fn origResize1{};
HMODULE self{};SharedControl* ctl{};std::mutex mx;IDXGISwapChain* primary{};thread_local bool inside{};
struct SwapCtx{ComPtr<ID3D12CommandQueue> q12;ComPtr<ID3D12CommandQueue> queueCandidate;PresentQueueProofState queueProof{};std::unique_ptr<gpu::D3D11Pipeline> p11;std::unique_ptr<gpu::D3D12On12Pipeline> p12;GenerationTracker resetGeneration;GenerationTracker neuralRetryGeneration;SafeAttachState safeAttach{};ColorTransitionState colorState{};GraphicsApi api{GraphicsApi::Unknown};DXGI_COLOR_SPACE_TYPE trackedColorSpace{DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709};bool colorSpaceKnown{},d11TrackersInstalled{},d12TrackersInstalled{},guideHooksLogged{},safeLogged{},neuralLogged{},queueProofLogged{},backbuffersRegistered{},colorDirty{},hdrMetadataPresent{},driverLogged{};float hdrMaxNits{1000.0f};HRESULT lastPresentResult{S_OK};HRESULT deviceRemovedReason{S_OK};std::uint64_t lastGuideHookProbeFrame{};std::uintptr_t pipelineQueueId{};std::uint32_t width{},height{};std::uint64_t frames{},processed{},bypassed{},neural{},lastTick{};double fps{};};
std::unordered_map<IDXGISwapChain*,std::unique_ptr<SwapCtx>> swaps;
std::unordered_map<ID3D12Resource*,IDXGISwapChain*> backbufferOwners;
struct TrackingGuard{TrackingGuard(){gpu::setD3D11TrackingSuppressed(true);gpu::setD3D12TrackingSuppressed(true);gpu::setGameGuideCaptureSuppressed(true);}~TrackingGuard(){gpu::setGameGuideCaptureSuppressed(false);gpu::setD3D12TrackingSuppressed(false);gpu::setD3D11TrackingSuppressed(false);}};
std::wstring moduleDir(){wchar_t p[32768];DWORD n=GetModuleFileNameW(self,p,_countof(p));return fs::path(std::wstring(p,n)).parent_path().wstring();}
std::wstring runtimePath(){if(!ctl||!ctl->raw())return{};return ctl->raw()->runtimePath;}
SwapCtx& getCtx(IDXGISwapChain*s){std::scoped_lock l(mx);auto&v=swaps[s];if(!v)v=std::make_unique<SwapCtx>();return*v;}
std::uintptr_t queueIdentity(ID3D12CommandQueue* q){if(!q)return 0;ComPtr<IUnknown> id;if(FAILED(q->QueryInterface(IID_PPV_ARGS(&id))))return reinterpret_cast<std::uintptr_t>(q);return reinterpret_cast<std::uintptr_t>(id.Get());}
void noteQueueEvidence(IDXGISwapChain* s,ID3D12CommandQueue* q,bool exactCreation){
 if(!s||!q||q->GetDesc().Type!=D3D12_COMMAND_LIST_TYPE_DIRECT)return;const auto now=GetTickCount64();const auto id=queueIdentity(q);bool newlyTrusted=false;
 {std::scoped_lock lock(mx);auto& ptr=swaps[s];if(!ptr)ptr=std::make_unique<SwapCtx>();auto& c=*ptr;const auto wasTrusted=presentQueueTrusted(c.queueProof,now);observePresentQueueEvidence(c.queueProof,id,now,exactCreation);const auto trusted=presentQueueTrusted(c.queueProof,now);if(trusted&&c.queueProof.trustedQueueId==id){c.q12=q;c.queueCandidate=q;c.safeAttach.queueCaptured=true;newlyTrusted=!wasTrusted||!c.queueProofLogged;c.queueProofLogged=true;}else if(!trusted){c.safeAttach.queueCaptured=false;if(!c.queueProof.creationProven)c.q12.Reset();}}
 if(newlyTrusted)logAttachStage(AttachLogStage::D3D12QueueCaptured,exactCreation?L"queue supplied by DXGI swapchain creation":L"queue proven by swapchain backbuffer command-list transitions");
}
void unregisterSwapchainBackbuffersLocked(IDXGISwapChain* s){for(auto it=backbufferOwners.begin();it!=backbufferOwners.end();){if(it->second==s)it=backbufferOwners.erase(it);else ++it;}}
void registerSwapchainBackbuffers(IDXGISwapChain* s){
 if(!s)return;DXGI_SWAP_CHAIN_DESC sd{};if(FAILED(s->GetDesc(&sd))||!sd.BufferCount)return;std::vector<ID3D12Resource*> buffers;buffers.reserve(sd.BufferCount);std::vector<ComPtr<ID3D12Resource>> holds;holds.reserve(sd.BufferCount);
 for(UINT i=0;i<sd.BufferCount;++i){ComPtr<ID3D12Resource> r;if(SUCCEEDED(s->GetBuffer(i,IID_PPV_ARGS(&r)))&&r){buffers.push_back(r.Get());holds.push_back(r);}}
 std::scoped_lock lock(mx);unregisterSwapchainBackbuffersLocked(s);for(auto* r:buffers)backbufferOwners[r]=s;auto it=swaps.find(s);if(it!=swaps.end()&&it->second)it->second->backbuffersRegistered=!buffers.empty();
}
void STDMETHODCALLTYPE hookExecute(ID3D12CommandQueue*q,UINT n,ID3D12CommandList*const*l){
 std::vector<IDXGISwapChain*> owners;
 if(q&&q->GetDesc().Type==D3D12_COMMAND_LIST_TYPE_DIRECT&&l){std::unordered_set<IDXGISwapChain*> unique;for(UINT i=0;i<n;++i){for(auto* r:gpu::takeD3D12CommandListTouches(l[i])){std::scoped_lock lock(mx);auto it=backbufferOwners.find(r);if(it!=backbufferOwners.end()&&it->second)unique.insert(it->second);}}owners.assign(unique.begin(),unique.end());}
 origExecute(q,n,l);for(auto* owner:owners)noteQueueEvidence(owner,q,false);
}
void rememberQueue(IDXGISwapChain*s,IUnknown*d){if(!s||!d)return;ComPtr<ID3D12CommandQueue>q;if(SUCCEEDED(d->QueryInterface(IID_PPV_ARGS(&q))))noteQueueEvidence(s,q.Get(),true);}
void markColorDirty(IDXGISwapChain*s){if(!s)return;std::scoped_lock l(mx);auto&v=swaps[s];if(!v)v=std::make_unique<SwapCtx>();v->colorDirty=true;}
void updateFps(SwapCtx&c){auto now=GetTickCount64();if(c.lastTick){double inst=1000.0/(double)std::max<std::uint64_t>(1,now-c.lastTick);c.fps=c.fps?c.fps*.9+inst*.1:inst;}c.lastTick=now;}
void writeStatus(SwapCtx&c,RuntimeStatus&st){st.pid=GetCurrentProcessId();st.presentedFrames=c.frames;st.processedFrames=c.processed;st.bypassedFrames=c.bypassed;st.neuralFrames=c.neural;st.estimatedFps=(float)c.fps;st.lastTickMs=GetTickCount64();if(ctl)ctl->writeStatus(st);}
HRESULT queryDeviceRemovedReason(IDXGISwapChain* s){
 if(!s)return S_OK;ComPtr<ID3D12Device>d12;if(SUCCEEDED(s->GetDevice(IID_PPV_ARGS(&d12)))&&d12)return d12->GetDeviceRemovedReason();
 ComPtr<ID3D11Device>d11;if(SUCCEEDED(s->GetDevice(IID_PPV_ARGS(&d11)))&&d11)return d11->GetDeviceRemovedReason();return S_OK;
}
void recordPresentResult(IDXGISwapChain* s,HRESULT hr){
 if(!s)return;HRESULT removed=S_OK;if(FAILED(hr))removed=queryDeviceRemovedReason(s);
 {std::scoped_lock l(mx);auto it=swaps.find(s);if(it!=swaps.end()&&it->second){it->second->lastPresentResult=hr;it->second->deviceRemovedReason=removed;}}
 if(FAILED(hr)){wchar_t detail[256]{};swprintf_s(detail,L"Present failed: hr=0x%08X, deviceRemovedReason=0x%08X",(unsigned)hr,(unsigned)removed);logAttachStage(AttachLogStage::Failure,detail);}
}
void probeGameGuideHooks(SwapCtx& c){
 if(c.lastGuideHookProbeFrame && c.frames-c.lastGuideHookProbeFrame<120)return;
 c.lastGuideHookProbeFrame=c.frames;
 if(gpu::installGameTemporalGuideHooks()&&!c.guideHooksLogged){c.guideHooksLogged=true;logAttachStage(AttachLogStage::GameGuideHooksInstalled);}
}
bool process(IDXGISwapChain*s){
 if(!ctl||!ctl->valid()||inside)return false;setCrashStage(CrashStage::ProcessEntry);inside=true;Settings set=ctl->readSettings();auto&c=getCtx(s);++c.frames;updateFps(c);RuntimeStatus st{};st.pid=GetCurrentProcessId();st.state=RuntimeState::Hooked;st.lastTickMs=GetTickCount64();markPipelineStage(st.stageMask,PipelineStage::BridgeInjected);markPipelineStage(st.stageMask,PipelineStage::HooksInstalled);markPipelineStage(st.stageMask,PipelineStage::PresentObserved);if(!c.safeAttach.firstPresentTickMs){c.safeAttach.firstPresentTickMs=st.lastTickMs;logAttachStage(AttachLogStage::PresentObserved);}++c.safeAttach.stablePresents;
 if(!set.enabled){st.state=RuntimeState::Bypassed;++c.bypassed;wcscpy_s(st.message,L"processing disabled");writeStatus(c,st);inside=false;return false;}
 const auto resetGen=ctl->historyResetGeneration();if(c.resetGeneration.consume(resetGen)){if(c.p11)c.p11->reset();if(c.p12)c.p12->reset();}
 const auto retryGen=ctl->neuralRetryGeneration();if(c.neuralRetryGeneration.consume(retryGen)){if(c.p11)c.p11->retryNeural();if(c.p12)c.p12->retryNeural();}
 DXGI_SWAP_CHAIN_DESC sd{};if(SUCCEEDED(s->GetDesc(&sd))){st.width=sd.BufferDesc.Width;st.height=sd.BufferDesc.Height;c.width=st.width;c.height=st.height;}
 const auto color=gpu::querySwapchainColor(s,set.hdrMode,c.trackedColorSpace,c.colorSpaceKnown,c.hdrMaxNits,c.hdrMetadataPresent);
 st.swapchainFormat=(std::uint32_t)color.format;st.swapchainColorSpace=(std::uint32_t)color.colorSpace;st.hdrActive=color.decision.hdrActive?1u:0u;st.hdrSupported=color.decision.supported?1u:0u;st.hdrEncoding=(std::uint32_t)color.decision.encoding;st.hdrPaperWhiteNits=color.paperWhiteNits;st.hdrMaxNits=color.maxNits;st.lastPresentResult=c.lastPresentResult;st.deviceRemovedReason=c.deviceRemovedReason;
 const bool firstColorObservation=!c.colorState.initialized;bool colorChanged=observeColorSignature(c.colorState,gpu::colorSignature(color));
 if(firstColorObservation){wchar_t detail[256]{};swprintf_s(detail,L"initial: %ls | format=%u colorSpace=%u metadata=%ls peak=%.0f nits",gpu::colorEncodingLabel(color.decision.encoding),(unsigned)color.format,(unsigned)color.colorSpace,color.metadataPresent?L"YES":L"NO",color.maxNits);logAttachStage(AttachLogStage::ColorSpaceChanged,detail);}
 if(c.colorDirty){c.colorDirty=false;if(!colorChanged){c.colorState.stablePresents=1;++c.colorState.transitionCount;colorChanged=true;}}
 st.hdrTransitionCount=c.colorState.transitionCount;
 if(colorChanged){if(c.p11)c.p11->reset();if(c.p12)c.p12->reset();c.safeAttach.stablePresents=1;wchar_t detail[256]{};swprintf_s(detail,L"%ls | format=%u colorSpace=%u metadata=%ls peak=%.0f nits",gpu::colorEncodingLabel(color.decision.encoding),(unsigned)color.format,(unsigned)color.colorSpace,color.metadataPresent?L"YES":L"NO",color.maxNits);logAttachStage(AttachLogStage::ColorSpaceChanged,detail);}
 if(!color.decision.supported){st.state=RuntimeState::Bypassed;++c.bypassed;wcscpy_s(st.message,L"unsupported HDR swapchain contract; bypassing rather than corrupting the game backbuffer");writeStatus(c,st);inside=false;return false;}
 if(!colorPipelineStable(c.colorState)){st.state=RuntimeState::Bypassed;++c.bypassed;wcscpy_s(st.message,L"display color-space transition detected; waiting for HDR/SDR swapchain to stabilize");writeStatus(c,st);inside=false;return false;}
 // The controller elects one renderer PID across launcher/helper/overlay
 // processes. Non-primary bridges keep publishing lightweight status but do
 // not execute the neural path, preventing cross-process on/off flicker.
 const auto electedRenderer=ctl->primaryRendererPid();
 if(electedRenderer && electedRenderer!=GetCurrentProcessId()){
  st.api=c.api;st.state=RuntimeState::Bypassed;st.neuralActive=0;++c.bypassed;wcscpy_s(st.message,L"suppressed by primary renderer election");writeStatus(c,st);inside=false;return false;
 }
 if(!set.processSecondarySwapchains){
  bool selected=false;{std::scoped_lock l(mx);if(primary==s)selected=true;else{std::uint64_t primaryTick=0,primaryPixels=0;bool hasPrimary=primary!=nullptr;auto it=primary?swaps.find(primary):swaps.end();if(it!=swaps.end()&&it->second){primaryTick=it->second->lastTick;primaryPixels=(std::uint64_t)it->second->width*it->second->height;}const std::uint64_t pixels=(std::uint64_t)c.width*c.height;if(shouldPromotePrimary(hasPrimary,primaryTick,GetTickCount64(),pixels,primaryPixels)){primary=s;selected=true;}}}
  if(!selected){++c.bypassed;inside=false;return false;}
 }
 bool ok=false;
 ComPtr<ID3D11Device>d11;
 if(SUCCEEDED(s->GetDevice(IID_PPV_ARGS(&d11)))){
  st.api=GraphicsApi::D3D11;c.api=st.api;markPipelineStage(st.stageMask,PipelineStage::SourceApiDetected);
  if(!c.safeLogged)logAttachStage(AttachLogStage::SourceD3D11Detected);
  if(!safeAttachReady(c.safeAttach,st.lastTickMs,false)){st.state=RuntimeState::Bypassed;++c.bypassed;wcscpy_s(st.message,L"safe attach: waiting for stable D3D11 presents");writeStatus(c,st);inside=false;return false;}
  if(!c.safeLogged){logAttachStage(AttachLogStage::SafeAttachStable,L"D3D11 swapchain stable");c.safeLogged=true;}
  ComPtr<ID3D11DeviceContext> gameCtx;d11->GetImmediateContext(&gameCtx);
  if(!c.d11TrackersInstalled){const bool rt=gpu::installD3D11ResourceTrackingHooks(gameCtx.Get());const bool cam=gpu::installD3D11CameraTrackingHooks(d11.Get(),gameCtx.Get());const auto enabled=MH_EnableHook(MH_ALL_HOOKS);if(rt&&cam&&(enabled==MH_OK||enabled==MH_ERROR_ENABLED)){c.d11TrackersInstalled=true;logAttachStage(AttachLogStage::D3D11TrackersInstalled);} }
  probeGameGuideHooks(c);
  gpu::globalD3D11ResourceTracker().finalizeFrame(d11.Get(),st.width,st.height);gpu::globalD3D11CameraTracker().finalizeFrame(d11.Get());
  if(!c.p11){c.p11=std::make_unique<gpu::D3D11Pipeline>();if(!c.p11->initialize(d11.Get(),gameCtx.Get(),moduleDir(),st)){if(st.failureStage==PipelineStage::None)st.failureStage=PipelineStage::GuideResourcesFailed;c.p11.reset();}else logAttachStage(AttachLogStage::D3D11PipelineInitialized);}
  if(c.p11){ComPtr<ID3D11Texture2D>bb;if(SUCCEEDED(s->GetBuffer(0,IID_PPV_ARGS(&bb)))){TrackingGuard guard;ok=c.p11->process(bb.Get(),set,runtimePath(),st,nullptr,&color);}else{st.failureStage=PipelineStage::GuideResourcesFailed;wcscpy_s(st.message,L"Could not acquire D3D11 swapchain backbuffer");}}
 }else{
  ComPtr<ID3D12Device>d12;
  if(SUCCEEDED(s->GetDevice(IID_PPV_ARGS(&d12)))){
   st.api=GraphicsApi::D3D12;c.api=st.api;markPipelineStage(st.stageMask,PipelineStage::SourceApiDetected);
   if(!c.safeLogged)logAttachStage(AttachLogStage::SourceD3D12Detected);
   if(!c.driverLogged){const auto di=neural::queryNvidiaDriverInfo();if(di.found){std::wstring detail=di.text;if(di.directFeature18Risk)detail+=L" | known direct Feature-18 crash-risk route";logAttachStage(AttachLogStage::NvidiaDriverDetected,detail);}c.driverLogged=true;}
   setCrashStage(CrashStage::QueueProof);
   if(!c.d12TrackersInstalled&&gpu::installD3D12ResourceTrackingHooks(d12.Get())){c.d12TrackersInstalled=true;logAttachStage(AttachLogStage::D3D12TrackersInstalled);}
   if(!c.backbuffersRegistered)registerSwapchainBackbuffers(s);
   ComPtr<ID3D12CommandQueue> provenQueue;std::uintptr_t provenQueueId{};{std::scoped_lock lock(mx);const bool trusted=presentQueueTrusted(c.queueProof,st.lastTickMs);if(trusted){provenQueue=c.q12;provenQueueId=c.queueProof.trustedQueueId;}c.safeAttach.queueCaptured=trusted&&provenQueue!=nullptr;}
   if(!safeAttachReady(c.safeAttach,st.lastTickMs,true)){st.state=RuntimeState::Bypassed;++c.bypassed;wcscpy_s(st.message,c.safeAttach.queueCaptured?L"safe attach: stabilizing proven D3D12 presentation queue":L"safe attach: waiting for proven swapchain presentation queue");writeStatus(c,st);inside=false;return false;}
   if(!c.safeLogged){logAttachStage(AttachLogStage::SafeAttachStable,L"D3D12 presenting queue and swapchain stable");c.safeLogged=true;}
   probeGameGuideHooks(c);
   gpu::globalD3D12ResourceTracker().finalizeFrame(d12.Get(),st.width,st.height);
   if(provenQueue&&set.allowD3D11On12){if(c.p12&&c.pipelineQueueId!=provenQueueId){c.p12->reset();c.p12.reset();c.pipelineQueueId=0;}if(!c.p12){c.p12=std::make_unique<gpu::D3D12On12Pipeline>();if(!c.p12->initialize(provenQueue.Get(),moduleDir(),st)){if(st.failureStage==PipelineStage::None)st.failureStage=PipelineStage::GuideResourcesFailed;c.p12.reset();c.pipelineQueueId=0;}else{c.pipelineQueueId=provenQueueId;logAttachStage(AttachLogStage::D3D12On12Initialized,L"owned color staging on proven presentation queue");}}if(c.p12){TrackingGuard guard;ok=c.p12->process(s,set,runtimePath(),st,&color);}}
   else{st.failureStage=PipelineStage::QueueCaptureFailed;wcscpy_s(st.message,L"D3D12 source detected but no proven presenting DIRECT queue is available, or D3D11On12 is disabled");}
  }else{
   st.failureStage=PipelineStage::SourceApiUnsupported;wcscpy_s(st.message,L"No D3D11/D3D12 DXGI device found for this swapchain; Vulkan/OpenGL are not implemented in this build");
  }
 }
 setCrashStage(CrashStage::ProcessComplete);
 if(ok){++c.processed;if(st.neuralActive){++c.neural;st.state=RuntimeState::Processing;if(!c.neuralLogged){logAttachStage(AttachLogStage::NeuralProcessingStarted);c.neuralLogged=true;}}else{++c.bypassed;st.state=RuntimeState::Bypassed;}}else{++c.bypassed;st.state=st.failureStage==PipelineStage::None?RuntimeState::Bypassed:RuntimeState::Error;}writeStatus(c,st);inside=false;return ok;
}
HRESULT STDMETHODCALLTYPE hookPresent(IDXGISwapChain*s,UINT sync,UINT flags){process(s);setCrashStage(CrashStage::CallingPresent);const auto hr=origPresent(s,sync,flags);setCrashStage(CrashStage::PresentReturned);recordPresentResult(s,hr);setCrashStage(CrashStage::Idle);return hr;} 
HRESULT STDMETHODCALLTYPE hookPresent1(IDXGISwapChain1*s,UINT sync,UINT flags,const DXGI_PRESENT_PARAMETERS*p){process(s);setCrashStage(CrashStage::CallingPresent);const auto hr=origPresent1(s,sync,flags,p);setCrashStage(CrashStage::PresentReturned);recordPresentResult(s,hr);setCrashStage(CrashStage::Idle);return hr;} 
HRESULT STDMETHODCALLTYPE hookResize(IDXGISwapChain*s,UINT count,UINT w,UINT h,DXGI_FORMAT f,UINT flags){{std::scoped_lock l(mx);auto it=swaps.find(s);if(it!=swaps.end()){if(it->second->p11)it->second->p11->reset();if(it->second->p12)it->second->p12->reset();it->second->backbuffersRegistered=false;it->second->colorDirty=true;}unregisterSwapchainBackbuffersLocked(s);if(primary==s)primary=nullptr;}return origResize(s,count,w,h,f,flags);} 
HRESULT STDMETHODCALLTYPE hookSetFullscreen(IDXGISwapChain*s,BOOL full,IDXGIOutput*o){auto hr=origSetFullscreen(s,full,o);if(SUCCEEDED(hr))markColorDirty(s);return hr;}
HRESULT STDMETHODCALLTYPE hookSetColorSpace(IDXGISwapChain3*s,DXGI_COLOR_SPACE_TYPE color){
 auto hr=origSetColorSpace(s,color);
 if(SUCCEEDED(hr)){
  std::scoped_lock l(mx);auto&v=swaps[s];if(!v)v=std::make_unique<SwapCtx>();
  v->trackedColorSpace=color;v->colorSpaceKnown=true;v->colorDirty=true;
 }
 return hr;
}
HRESULT STDMETHODCALLTYPE hookSetHdrMeta(IDXGISwapChain4*s,DXGI_HDR_METADATA_TYPE type,UINT size,void* data){auto hr=origSetHdrMeta(s,type,size,data);if(SUCCEEDED(hr)){std::scoped_lock l(mx);auto&v=swaps[s];if(!v)v=std::make_unique<SwapCtx>();v->colorDirty=true;v->hdrMetadataPresent=type!=DXGI_HDR_METADATA_TYPE_NONE;if(type==DXGI_HDR_METADATA_TYPE_HDR10&&data&&size>=sizeof(DXGI_HDR_METADATA_HDR10)){auto*m=(DXGI_HDR_METADATA_HDR10*)data;float master=m->MaxMasteringLuminance*0.0001f;float cll=(float)m->MaxContentLightLevel;float peak=std::max(master,cll);if(peak>=80.0f&&peak<=10000.0f)v->hdrMaxNits=peak;}}return hr;}
HRESULT STDMETHODCALLTYPE hookResize1(IDXGISwapChain3*s,UINT count,UINT w,UINT h,DXGI_FORMAT f,UINT flags,const UINT*masks,IUnknown*const*queues){{std::scoped_lock l(mx);auto it=swaps.find(s);if(it!=swaps.end()){if(it->second->p11)it->second->p11->reset();if(it->second->p12)it->second->p12->reset();it->second->backbuffersRegistered=false;it->second->colorDirty=true;}unregisterSwapchainBackbuffersLocked(s);if(primary==s)primary=nullptr;}return origResize1(s,count,w,h,f,flags,masks,queues);}
HRESULT STDMETHODCALLTYPE hookCreateSwap(IDXGIFactory*f,IUnknown*d,DXGI_SWAP_CHAIN_DESC*desc,IDXGISwapChain**out){auto hr=origCreateSwap(f,d,desc,out);if(SUCCEEDED(hr)&&out&&*out)rememberQueue(*out,d);return hr;}
HRESULT STDMETHODCALLTYPE hookCreateHwnd(IDXGIFactory2*f,IUnknown*d,HWND h,const DXGI_SWAP_CHAIN_DESC1*a,const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*b,IDXGIOutput*o,IDXGISwapChain1**out){auto hr=origCreateHwnd(f,d,h,a,b,o,out);if(SUCCEEDED(hr)&&out&&*out)rememberQueue(*out,d);return hr;}
HRESULT STDMETHODCALLTYPE hookCreateCore(IDXGIFactory2*f,IUnknown*d,IUnknown*w,const DXGI_SWAP_CHAIN_DESC1*a,IDXGIOutput*o,IDXGISwapChain1**out){auto hr=origCreateCore(f,d,w,a,o,out);if(SUCCEEDED(hr)&&out&&*out)rememberQueue(*out,d);return hr;}
HRESULT STDMETHODCALLTYPE hookCreateComp(IDXGIFactory2*f,IUnknown*d,const DXGI_SWAP_CHAIN_DESC1*a,IDXGIOutput*o,IDXGISwapChain1**out){auto hr=origCreateComp(f,d,a,o,out);if(SUCCEEDED(hr)&&out&&*out)rememberQueue(*out,d);return hr;}
LRESULT CALLBACK dummyWnd(HWND h,UINT m,WPARAM w,LPARAM l){return DefWindowProcW(h,m,w,l);} 
bool hook(void*target,void*detour,void**orig){return target&&MH_CreateHook(target,detour,orig)==MH_OK;}
}
bool installHooks(HMODULE h,SharedControl*c){self=h;ctl=c;if(MH_Initialize()!=MH_OK)return false;WNDCLASSW wc{};wc.lpfnWndProc=dummyWnd;wc.hInstance=h;wc.lpszClassName=L"UDLSS5_Dummy";RegisterClassW(&wc);HWND wnd=CreateWindowW(wc.lpszClassName,L"",WS_OVERLAPPED,0,0,16,16,nullptr,nullptr,h,nullptr);DXGI_SWAP_CHAIN_DESC sd{};sd.BufferCount=2;sd.BufferDesc.Width=16;sd.BufferDesc.Height=16;sd.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;sd.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;sd.OutputWindow=wnd;sd.SampleDesc.Count=1;sd.Windowed=TRUE;sd.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;ComPtr<ID3D11Device>d;ComPtr<ID3D11DeviceContext>ctx;ComPtr<IDXGISwapChain>s;D3D_FEATURE_LEVEL fl;if(FAILED(D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&sd,&s,&d,&fl,&ctx))){DestroyWindow(wnd);MH_Uninitialize();return false;}void**sv=*(void***)s.Get();bool ok=hook(sv[8],(void*)hookPresent,(void**)&origPresent)&&hook(sv[10],(void*)hookSetFullscreen,(void**)&origSetFullscreen)&&hook(sv[13],(void*)hookResize,(void**)&origResize);ComPtr<IDXGISwapChain1>s1;if(SUCCEEDED(s.As(&s1))){void**v=*(void***)s1.Get();ok&=hook(v[22],(void*)hookPresent1,(void**)&origPresent1);}ComPtr<IDXGISwapChain4>s4;if(SUCCEEDED(s.As(&s4))){void**v=*(void***)s4.Get();ok&=hook(v[38],(void*)hookSetColorSpace,(void**)&origSetColorSpace);ok&=hook(v[39],(void*)hookResize1,(void**)&origResize1);ok&=hook(v[40],(void*)hookSetHdrMeta,(void**)&origSetHdrMeta);}ComPtr<IDXGIFactory2>fac;if(SUCCEEDED(CreateDXGIFactory2(0,IID_PPV_ARGS(&fac)))){void**v=*(void***)fac.Get();ok&=hook(v[10],(void*)hookCreateSwap,(void**)&origCreateSwap);ok&=hook(v[15],(void*)hookCreateHwnd,(void**)&origCreateHwnd);ok&=hook(v[16],(void*)hookCreateCore,(void**)&origCreateCore);ok&=hook(v[24],(void*)hookCreateComp,(void**)&origCreateComp);}ComPtr<ID3D12Device> dd; if(SUCCEEDED(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&dd)))){D3D12_COMMAND_QUEUE_DESC qd{};ComPtr<ID3D12CommandQueue> q;if(SUCCEEDED(dd->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)))){void**qv=*(void***)q.Get();hook(qv[10],(void*)hookExecute,(void**)&origExecute);}}logAttachStage(AttachLogStage::DxgiBootstrapCreated);if(ok){const auto enabled=MH_EnableHook(MH_ALL_HOOKS);ok=enabled==MH_OK||enabled==MH_ERROR_ENABLED;}DestroyWindow(wnd);if(ok)logAttachStage(AttachLogStage::CoreHooksInstalled);RuntimeStatus st{};st.pid=GetCurrentProcessId();st.state=ok?RuntimeState::Hooked:RuntimeState::Error;st.lastTickMs=GetTickCount64();markPipelineStage(st.stageMask,PipelineStage::BridgeInjected);if(ok)markPipelineStage(st.stageMask,PipelineStage::HooksInstalled);else st.failureStage=PipelineStage::HookInstallFailed;wcscpy_s(st.message,ok?L"DXGI hooks installed":L"DXGI hook installation failed");if(ctl)ctl->writeStatus(st);return ok;}
void removeHooks(){RuntimeStatus st{};st.pid=GetCurrentProcessId();st.state=RuntimeState::Unloading;st.lastTickMs=GetTickCount64();wcscpy_s(st.message,L"unloading bridge");if(ctl)ctl->writeStatus(st);MH_DisableHook(MH_ALL_HOOKS);MH_Uninitialize();std::scoped_lock l(mx);backbufferOwners.clear();swaps.clear();primary=nullptr;gpu::globalD3D11ResourceTracker().reset();gpu::globalD3D11CameraTracker().reset();gpu::globalD3D12ResourceTracker().reset();gpu::resetGameTemporalGuides();removeCrashStageHandler();}
}
