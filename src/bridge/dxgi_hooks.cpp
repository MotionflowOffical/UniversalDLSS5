#include "dxgi_hooks.hpp"
#include "../gpu/d3d11_pipeline.hpp"
#include "../gpu/d3d12_on12.hpp"
#include "../gpu/d3d11_resource_tracker.hpp"
#include "../gpu/d3d11_camera_tracker.hpp"
#include <d3d11.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <MinHook.h>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <filesystem>
#include <chrono>
#include "udlss/control_generation.hpp"
#include "udlss/swapchain_policy.hpp"

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
PresentFn origPresent{};Present1Fn origPresent1{};ResizeFn origResize{};CreateSwapFn origCreateSwap{};CreateHwndFn origCreateHwnd{};CreateCoreFn origCreateCore{};CreateCompFn origCreateComp{};ExecuteFn origExecute{};
HMODULE self{};SharedControl* ctl{};std::mutex mx;IDXGISwapChain* primary{};thread_local bool inside{};ComPtr<ID3D12CommandQueue> recentQueue;
struct SwapCtx{ComPtr<ID3D12CommandQueue> q12;std::unique_ptr<gpu::D3D11Pipeline> p11;std::unique_ptr<gpu::D3D12On12Pipeline> p12;GenerationTracker resetGeneration;GenerationTracker neuralRetryGeneration;GraphicsApi api{GraphicsApi::Unknown};std::uint32_t width{},height{};std::uint64_t frames{},processed{},bypassed{},neural{},lastTick{};double fps{};};
std::unordered_map<IDXGISwapChain*,std::unique_ptr<SwapCtx>> swaps;
struct TrackingGuard{TrackingGuard(){gpu::setD3D11TrackingSuppressed(true);}~TrackingGuard(){gpu::setD3D11TrackingSuppressed(false);}};
std::wstring moduleDir(){wchar_t p[32768];DWORD n=GetModuleFileNameW(self,p,_countof(p));return fs::path(std::wstring(p,n)).parent_path().wstring();}
std::wstring runtimePath(){if(!ctl||!ctl->raw())return{};return ctl->raw()->runtimePath;}
SwapCtx& getCtx(IDXGISwapChain*s){std::scoped_lock l(mx);auto&v=swaps[s];if(!v)v=std::make_unique<SwapCtx>();return*v;}
void STDMETHODCALLTYPE hookExecute(ID3D12CommandQueue*q,UINT n,ID3D12CommandList*const*l){{std::scoped_lock lock(mx);if(q&&q->GetDesc().Type==D3D12_COMMAND_LIST_TYPE_DIRECT)recentQueue=q;}origExecute(q,n,l);}
void rememberQueue(IDXGISwapChain*s,IUnknown*d){if(!s||!d)return;ComPtr<ID3D12CommandQueue>q;if(SUCCEEDED(d->QueryInterface(IID_PPV_ARGS(&q)))){auto&c=getCtx(s);c.q12=q;}}
void updateFps(SwapCtx&c){auto now=GetTickCount64();if(c.lastTick){double inst=1000.0/(double)std::max<std::uint64_t>(1,now-c.lastTick);c.fps=c.fps?c.fps*.9+inst*.1:inst;}c.lastTick=now;}
void writeStatus(SwapCtx&c,RuntimeStatus&st){st.pid=GetCurrentProcessId();st.presentedFrames=c.frames;st.processedFrames=c.processed;st.bypassedFrames=c.bypassed;st.neuralFrames=c.neural;st.estimatedFps=(float)c.fps;st.lastTickMs=GetTickCount64();if(ctl)ctl->writeStatus(st);}
bool process(IDXGISwapChain*s){
 if(!ctl||!ctl->valid()||inside)return false;inside=true;Settings set=ctl->readSettings();auto&c=getCtx(s);++c.frames;updateFps(c);RuntimeStatus st{};st.pid=GetCurrentProcessId();st.state=RuntimeState::Hooked;st.lastTickMs=GetTickCount64();markPipelineStage(st.stageMask,PipelineStage::BridgeInjected);markPipelineStage(st.stageMask,PipelineStage::HooksInstalled);markPipelineStage(st.stageMask,PipelineStage::PresentObserved);
 if(!set.enabled){st.state=RuntimeState::Bypassed;++c.bypassed;wcscpy_s(st.message,L"processing disabled");writeStatus(c,st);inside=false;return false;}
 const auto resetGen=ctl->historyResetGeneration();if(c.resetGeneration.consume(resetGen)){if(c.p11)c.p11->reset();if(c.p12)c.p12->reset();}
 const auto retryGen=ctl->neuralRetryGeneration();if(c.neuralRetryGeneration.consume(retryGen)){if(c.p11)c.p11->retryNeural();if(c.p12)c.p12->retryNeural();}
 DXGI_SWAP_CHAIN_DESC sd{};if(SUCCEEDED(s->GetDesc(&sd))){st.width=sd.BufferDesc.Width;st.height=sd.BufferDesc.Height;c.width=st.width;c.height=st.height;}
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
  gpu::globalD3D11ResourceTracker().finalizeFrame(d11.Get(),st.width,st.height);
  gpu::globalD3D11CameraTracker().finalizeFrame(d11.Get());
  if(!c.p11){ComPtr<ID3D11DeviceContext>ctx;d11->GetImmediateContext(&ctx);c.p11=std::make_unique<gpu::D3D11Pipeline>();if(!c.p11->initialize(d11.Get(),ctx.Get(),moduleDir(),st)){if(st.failureStage==PipelineStage::None)st.failureStage=PipelineStage::GuideResourcesFailed;c.p11.reset();}}
  if(c.p11){ComPtr<ID3D11Texture2D>bb;if(SUCCEEDED(s->GetBuffer(0,IID_PPV_ARGS(&bb)))){TrackingGuard guard;ok=c.p11->process(bb.Get(),set,runtimePath(),st);}else{st.failureStage=PipelineStage::GuideResourcesFailed;wcscpy_s(st.message,L"Could not acquire D3D11 swapchain backbuffer");}}
 }else{
  ComPtr<ID3D12Device>d12;
  if(SUCCEEDED(s->GetDevice(IID_PPV_ARGS(&d12)))){
   st.api=GraphicsApi::D3D12;c.api=st.api;markPipelineStage(st.stageMask,PipelineStage::SourceApiDetected);
   if(!c.q12){ComPtr<ID3D12CommandQueue> rq;{std::scoped_lock l(mx);rq=recentQueue;}if(rq){ComPtr<ID3D12Device>qd;if(SUCCEEDED(rq->GetDevice(IID_PPV_ARGS(&qd)))){ComPtr<IUnknown>a,b;d12.As(&a);qd.As(&b);if(a.Get()==b.Get())c.q12=rq;}}}
   if(c.q12&&set.allowD3D11On12){if(!c.p12){c.p12=std::make_unique<gpu::D3D12On12Pipeline>();if(!c.p12->initialize(c.q12.Get(),moduleDir(),st)){if(st.failureStage==PipelineStage::None)st.failureStage=PipelineStage::GuideResourcesFailed;c.p12.reset();}}if(c.p12)ok=c.p12->process(s,set,runtimePath(),st);}
   else{st.failureStage=PipelineStage::QueueCaptureFailed;wcscpy_s(st.message,L"D3D12 source detected but its DIRECT command queue was not captured, or D3D11On12 is disabled");}
  }else{
   st.failureStage=PipelineStage::SourceApiUnsupported;wcscpy_s(st.message,L"No D3D11/D3D12 DXGI device found for this swapchain; Vulkan/OpenGL are not implemented in this build");
  }
 }
 if(ok){++c.processed;if(st.neuralActive){++c.neural;st.state=RuntimeState::Processing;}else{++c.bypassed;st.state=RuntimeState::Bypassed;}}else{++c.bypassed;st.state=st.failureStage==PipelineStage::None?RuntimeState::Bypassed:RuntimeState::Error;}writeStatus(c,st);inside=false;return ok;
}
HRESULT STDMETHODCALLTYPE hookPresent(IDXGISwapChain*s,UINT sync,UINT flags){process(s);return origPresent(s,sync,flags);} 
HRESULT STDMETHODCALLTYPE hookPresent1(IDXGISwapChain1*s,UINT sync,UINT flags,const DXGI_PRESENT_PARAMETERS*p){process(s);return origPresent1(s,sync,flags,p);} 
HRESULT STDMETHODCALLTYPE hookResize(IDXGISwapChain*s,UINT count,UINT w,UINT h,DXGI_FORMAT f,UINT flags){{std::scoped_lock l(mx);auto it=swaps.find(s);if(it!=swaps.end()){if(it->second->p11)it->second->p11->reset();if(it->second->p12)it->second->p12->reset();}if(primary==s)primary=nullptr;}return origResize(s,count,w,h,f,flags);} 
HRESULT STDMETHODCALLTYPE hookCreateSwap(IDXGIFactory*f,IUnknown*d,DXGI_SWAP_CHAIN_DESC*desc,IDXGISwapChain**out){auto hr=origCreateSwap(f,d,desc,out);if(SUCCEEDED(hr)&&out&&*out)rememberQueue(*out,d);return hr;}
HRESULT STDMETHODCALLTYPE hookCreateHwnd(IDXGIFactory2*f,IUnknown*d,HWND h,const DXGI_SWAP_CHAIN_DESC1*a,const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*b,IDXGIOutput*o,IDXGISwapChain1**out){auto hr=origCreateHwnd(f,d,h,a,b,o,out);if(SUCCEEDED(hr)&&out&&*out)rememberQueue(*out,d);return hr;}
HRESULT STDMETHODCALLTYPE hookCreateCore(IDXGIFactory2*f,IUnknown*d,IUnknown*w,const DXGI_SWAP_CHAIN_DESC1*a,IDXGIOutput*o,IDXGISwapChain1**out){auto hr=origCreateCore(f,d,w,a,o,out);if(SUCCEEDED(hr)&&out&&*out)rememberQueue(*out,d);return hr;}
HRESULT STDMETHODCALLTYPE hookCreateComp(IDXGIFactory2*f,IUnknown*d,const DXGI_SWAP_CHAIN_DESC1*a,IDXGIOutput*o,IDXGISwapChain1**out){auto hr=origCreateComp(f,d,a,o,out);if(SUCCEEDED(hr)&&out&&*out)rememberQueue(*out,d);return hr;}
LRESULT CALLBACK dummyWnd(HWND h,UINT m,WPARAM w,LPARAM l){return DefWindowProcW(h,m,w,l);} 
bool hook(void*target,void*detour,void**orig){return target&&MH_CreateHook(target,detour,orig)==MH_OK;}
}
bool installHooks(HMODULE h,SharedControl*c){self=h;ctl=c;if(MH_Initialize()!=MH_OK)return false;WNDCLASSW wc{};wc.lpfnWndProc=dummyWnd;wc.hInstance=h;wc.lpszClassName=L"UDLSS5_Dummy";RegisterClassW(&wc);HWND wnd=CreateWindowW(wc.lpszClassName,L"",WS_OVERLAPPED,0,0,16,16,nullptr,nullptr,h,nullptr);DXGI_SWAP_CHAIN_DESC sd{};sd.BufferCount=2;sd.BufferDesc.Width=16;sd.BufferDesc.Height=16;sd.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;sd.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;sd.OutputWindow=wnd;sd.SampleDesc.Count=1;sd.Windowed=TRUE;sd.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;ComPtr<ID3D11Device>d;ComPtr<ID3D11DeviceContext>ctx;ComPtr<IDXGISwapChain>s;D3D_FEATURE_LEVEL fl;if(FAILED(D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&sd,&s,&d,&fl,&ctx))){DestroyWindow(wnd);MH_Uninitialize();return false;}void**sv=*(void***)s.Get();bool ok=hook(sv[8],(void*)hookPresent,(void**)&origPresent)&&hook(sv[13],(void*)hookResize,(void**)&origResize);ComPtr<IDXGISwapChain1>s1;if(SUCCEEDED(s.As(&s1))){void**v=*(void***)s1.Get();ok&=hook(v[22],(void*)hookPresent1,(void**)&origPresent1);}ComPtr<IDXGIFactory2>fac;if(SUCCEEDED(CreateDXGIFactory2(0,IID_PPV_ARGS(&fac)))){void**v=*(void***)fac.Get();ok&=hook(v[10],(void*)hookCreateSwap,(void**)&origCreateSwap);ok&=hook(v[15],(void*)hookCreateHwnd,(void**)&origCreateHwnd);ok&=hook(v[16],(void*)hookCreateCore,(void**)&origCreateCore);ok&=hook(v[24],(void*)hookCreateComp,(void**)&origCreateComp);}ComPtr<ID3D12Device> dd; if(SUCCEEDED(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&dd)))){D3D12_COMMAND_QUEUE_DESC qd{};ComPtr<ID3D12CommandQueue> q;if(SUCCEEDED(dd->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)))){void**qv=*(void***)q.Get();hook(qv[10],(void*)hookExecute,(void**)&origExecute);}}ok&=gpu::installD3D11ResourceTrackingHooks(ctx.Get());ok&=gpu::installD3D11CameraTrackingHooks(d.Get(),ctx.Get());if(ok)ok=MH_EnableHook(MH_ALL_HOOKS)==MH_OK;DestroyWindow(wnd);RuntimeStatus st{};st.pid=GetCurrentProcessId();st.state=ok?RuntimeState::Hooked:RuntimeState::Error;st.lastTickMs=GetTickCount64();markPipelineStage(st.stageMask,PipelineStage::BridgeInjected);if(ok)markPipelineStage(st.stageMask,PipelineStage::HooksInstalled);else st.failureStage=PipelineStage::HookInstallFailed;wcscpy_s(st.message,ok?L"DXGI hooks installed":L"DXGI hook installation failed");if(ctl)ctl->writeStatus(st);return ok;}
void removeHooks(){RuntimeStatus st{};st.pid=GetCurrentProcessId();st.state=RuntimeState::Unloading;st.lastTickMs=GetTickCount64();wcscpy_s(st.message,L"unloading bridge");if(ctl)ctl->writeStatus(st);MH_DisableHook(MH_ALL_HOOKS);MH_Uninitialize();std::scoped_lock l(mx);swaps.clear();primary=nullptr;gpu::globalD3D11ResourceTracker().reset();gpu::globalD3D11CameraTracker().reset();}
}
