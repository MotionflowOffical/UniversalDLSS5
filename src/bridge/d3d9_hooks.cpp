#include "d3d9_hooks.hpp"
#include "hook_lifecycle.hpp"
#include "attach_logger.hpp"
#include "udlss/renderer_activity.hpp"
#include "../compat/d3d9_frontend.hpp"
#include "../compat/compat_dispatch.hpp"
#include "udlss/renderer_ownership_policy.hpp"
#include <d3d9.h>
#include <MinHook.h>
#include <wrl/client.h>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <filesystem>
#include <atomic>

using Microsoft::WRL::ComPtr;
namespace fs=std::filesystem;
namespace udlss::bridge {
namespace {
// IDirect3DDevice9::Present is slot 17 and Reset is slot 16 in the documented COM layout.
// IDirect3DSwapChain9::Present is slot 3 (immediately after IUnknown).
constexpr std::size_t kResetVtableIndex=16;
constexpr std::size_t kPresentVtableIndex=17;
constexpr std::size_t kSwapChainPresentVtableIndex=3;
constexpr std::size_t kPresentExVtableIndex=121;
constexpr std::size_t kResetExVtableIndex=132;
using Present9Fn=HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,const RECT*,const RECT*,HWND,const RGNDATA*);
using Reset9Fn=HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,D3DPRESENT_PARAMETERS*);
using Present9ExFn=HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9Ex*,const RECT*,const RECT*,HWND,const RGNDATA*,DWORD);
using PresentSwap9Fn=HRESULT(STDMETHODCALLTYPE*)(IDirect3DSwapChain9*,const RECT*,const RECT*,HWND,const RGNDATA*,DWORD);
using Reset9ExFn=HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9Ex*,D3DPRESENT_PARAMETERS*,D3DDISPLAYMODEEX*);
Present9Fn origPresent9{};Reset9Fn origReset9{};Present9ExFn origPresent9Ex{};Reset9ExFn origReset9Ex{};PresentSwap9Fn origSwapPresent9{};
HMODULE self{};SharedControl* ctl{};std::mutex mx;std::mutex installMx;thread_local bool inside{};thread_local unsigned presentNesting{};std::atomic_bool d3d9HooksInstalled{false};bool basePresentInstalled{},swapPresentInstalled{},exPresentInstalled{};std::uint64_t lastInstallAttemptMs{};
struct DeviceCtx{std::unique_ptr<compat::D3D9Frontend> frontend;std::unique_ptr<compat::CompatDispatch> dispatch;std::uint64_t frames{},processed{},bypassed{},neural{};std::uint64_t lastTick{};double fps{};};
std::unordered_map<IDirect3DDevice9*,std::unique_ptr<DeviceCtx>> devices;
std::wstring moduleDir(){wchar_t p[32768]{};const DWORD n=GetModuleFileNameW(self,p,_countof(p));return fs::path(std::wstring(p,n)).parent_path().wstring();}
std::wstring runtimePath(){return ctl&&ctl->raw()?std::wstring(ctl->raw()->runtimePath):std::wstring{};}
struct PresentNestingScope{bool outer{};PresentNestingScope():outer(presentNesting++==0){}~PresentNestingScope(){--presentNesting;}};
DeviceCtx& getCtx(IDirect3DDevice9*d){std::scoped_lock l(mx);auto& p=devices[d];if(!p)p=std::make_unique<DeviceCtx>();return *p;}
void updateFps(DeviceCtx& c){const auto now=GetTickCount64();if(c.lastTick){const double inst=1000.0/(double)std::max<std::uint64_t>(1,now-c.lastTick);c.fps=c.fps?c.fps*.9+inst*.1:inst;}c.lastTick=now;}
bool processD3D9(IDirect3DDevice9* device){
    if(!device||!ctl||!ctl->valid()||inside||!processCompatibilityRendererAllowed(GraphicsApi::D3D9))return false;inside=true;auto& c=getCtx(device);++c.frames;if(c.frames==1)logAttachStage(AttachLogStage::SourceD3D9Detected,L"D3D9 Device/SwapChain Present observed");updateFps(c);RuntimeStatus st{};st.pid=GetCurrentProcessId();st.api=GraphicsApi::D3D9;st.gameArchitectureBits=sizeof(void*)*8u;st.state=RuntimeState::Hooked;st.lastTickMs=GetTickCount64();markPipelineStage(st.stageMask,PipelineStage::BridgeInjected);markPipelineStage(st.stageMask,PipelineStage::HooksInstalled);markPipelineStage(st.stageMask,PipelineStage::PresentObserved);markPipelineStage(st.stageMask,PipelineStage::SourceApiDetected);
    const auto settings=ctl->readSettings();if(!settings.enabled){++c.bypassed;st.state=RuntimeState::Bypassed;wcscpy_s(st.message,L"processing disabled");ctl->writeStatus(st);inside=false;return false;}
    const auto elected=ctl->primaryRendererPid();if(elected&&elected!=GetCurrentProcessId()){++c.bypassed;st.state=RuntimeState::Bypassed;wcscpy_s(st.message,L"suppressed by primary renderer election");ctl->writeStatus(st);inside=false;return false;}
    if(!c.frontend){c.frontend=std::make_unique<compat::D3D9Frontend>();if(!c.frontend->initialize(device,st)){c.frontend.reset();++c.bypassed;st.state=RuntimeState::Error;st.presentedFrames=c.frames;st.bypassedFrames=c.bypassed;ctl->writeStatus(st);inside=false;return false;}c.dispatch=std::make_unique<compat::CompatDispatch>();}
    st.rendererRoute=c.frontend->route();st.compatInterop=c.frontend->interop();const bool ok=c.dispatch->process(*c.frontend,settings,moduleDir(),runtimePath(),st);if(ok){++c.processed;if(st.neuralActive){++c.neural;st.state=RuntimeState::Processing;}else{++c.bypassed;st.state=RuntimeState::Bypassed;}}else{++c.bypassed;st.state=st.failureStage==PipelineStage::None?RuntimeState::Bypassed:RuntimeState::Error;}
    st.presentedFrames=c.frames;st.processedFrames=c.processed;st.bypassedFrames=c.bypassed;st.neuralFrames=c.neural;st.estimatedFps=(float)c.fps;ctl->writeStatus(st);inside=false;return ok;
}
void resetDeviceCtx(IDirect3DDevice9* d){std::scoped_lock l(mx);auto it=devices.find(d);if(it!=devices.end()&&it->second){if(it->second->dispatch)it->second->dispatch->reset();if(it->second->frontend)it->second->frontend->reset();it->second->dispatch.reset();it->second->frontend.reset();}}
HRESULT STDMETHODCALLTYPE hkPresent9(IDirect3DDevice9*d,const RECT*a,const RECT*b,HWND h,const RGNDATA*r){noteRendererActivity();HookCallScope call;PresentNestingScope nesting;if(call.customWorkAllowed()&&nesting.outer)processD3D9(d);return origPresent9(d,a,b,h,r);}
HRESULT STDMETHODCALLTYPE hkReset9(IDirect3DDevice9*d,D3DPRESENT_PARAMETERS*p){HookCallScope call;if(call.customWorkAllowed())resetDeviceCtx(d);return origReset9(d,p);}
HRESULT STDMETHODCALLTYPE hkPresent9Ex(IDirect3DDevice9Ex*d,const RECT*a,const RECT*b,HWND h,const RGNDATA*r,DWORD flags){noteRendererActivity();HookCallScope call;PresentNestingScope nesting;if(call.customWorkAllowed()&&nesting.outer)processD3D9(d);return origPresent9Ex(d,a,b,h,r,flags);}
HRESULT STDMETHODCALLTYPE hkSwapPresent9(IDirect3DSwapChain9*s,const RECT*a,const RECT*b,HWND h,const RGNDATA*r,DWORD flags){noteRendererActivity();HookCallScope call;PresentNestingScope nesting;if(call.customWorkAllowed()&&nesting.outer){ComPtr<IDirect3DDevice9> device;if(SUCCEEDED(s->GetDevice(device.GetAddressOf()))&&device)processD3D9(device.Get());}return origSwapPresent9(s,a,b,h,r,flags);}
HRESULT STDMETHODCALLTYPE hkReset9Ex(IDirect3DDevice9Ex*d,D3DPRESENT_PARAMETERS*p,D3DDISPLAYMODEEX*m){HookCallScope call;if(call.customWorkAllowed())resetDeviceCtx(d);return origReset9Ex(d,p,m);}
bool hookTarget(void* target,void* detour,void** original){if(!target)return false;const auto r=MH_CreateHook(target,detour,original);if(r!=MH_OK&&r!=MH_ERROR_ALREADY_CREATED)return false;const auto e=MH_EnableHook(target);return e==MH_OK||e==MH_ERROR_ENABLED;}
LRESULT CALLBACK dummyWnd(HWND h,UINT m,WPARAM w,LPARAM l){return DefWindowProcW(h,m,w,l);}
}
bool ensureD3D9HooksInstalled(HMODULE module,SharedControl* control){
    if(module)self=module;if(control)ctl=control;
    HMODULE d3d9=GetModuleHandleW(L"d3d9.dll");if(!d3d9)return false;
    const bool exExportPresent=GetProcAddress(d3d9,"Direct3DCreate9Ex")!=nullptr;
    if(basePresentInstalled&&swapPresentInstalled&&(!exExportPresent||exPresentInstalled)){d3d9HooksInstalled.store(true,std::memory_order_release);return true;}
    std::scoped_lock installLock(installMx);
    const auto now=GetTickCount64();if(lastInstallAttemptMs&&now-lastInstallAttemptMs<750)return d3d9HooksInstalled.load(std::memory_order_acquire);lastInstallAttemptMs=now;
    using Create9Fn=IDirect3D9*(WINAPI*)(UINT);auto create9=(Create9Fn)GetProcAddress(d3d9,"Direct3DCreate9");if(!create9)return false;
    WNDCLASSW wc{};wc.lpfnWndProc=dummyWnd;wc.hInstance=self;wc.lpszClassName=L"UDLSS5_D3D9_Dummy";RegisterClassW(&wc);HWND wnd=CreateWindowW(wc.lpszClassName,L"",WS_OVERLAPPED,0,0,32,32,nullptr,nullptr,self,nullptr);if(!wnd)return false;
    if(!basePresentInstalled||!swapPresentInstalled){ComPtr<IDirect3D9> root;root.Attach(create9(D3D_SDK_VERSION));if(root){D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=wnd;pp.BackBufferFormat=D3DFMT_UNKNOWN;ComPtr<IDirect3DDevice9> dev;if(SUCCEEDED(root->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,wnd,D3DCREATE_SOFTWARE_VERTEXPROCESSING|D3DCREATE_MULTITHREADED,&pp,&dev))&&dev){void**v=*(void***)dev.Get();if(!basePresentInstalled){const bool resetOk=hookTarget(v[kResetVtableIndex],(void*)hkReset9,(void**)&origReset9);(void)resetOk;basePresentInstalled=hookTarget(v[kPresentVtableIndex],(void*)hkPresent9,(void**)&origPresent9);}if(!swapPresentInstalled){ComPtr<IDirect3DSwapChain9> swap;if(SUCCEEDED(dev->GetSwapChain(0,&swap))&&swap){void**sv=*(void***)swap.Get();swapPresentInstalled=hookTarget(sv[kSwapChainPresentVtableIndex],(void*)hkSwapPresent9,(void**)&origSwapPresent9);}}}}}
    using Create9ExFn=HRESULT(WINAPI*)(UINT,IDirect3D9Ex**);auto create9ex=(Create9ExFn)GetProcAddress(d3d9,"Direct3DCreate9Ex");if(create9ex&&!exPresentInstalled){ComPtr<IDirect3D9Ex> rootEx;if(SUCCEEDED(create9ex(D3D_SDK_VERSION,&rootEx))&&rootEx){D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=wnd;pp.BackBufferFormat=D3DFMT_UNKNOWN;ComPtr<IDirect3DDevice9Ex> devEx;if(SUCCEEDED(rootEx->CreateDeviceEx(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,wnd,D3DCREATE_SOFTWARE_VERTEXPROCESSING|D3DCREATE_MULTITHREADED,&pp,nullptr,&devEx))&&devEx){void**v=*(void***)devEx.Get();const bool resetExOk=hookTarget(v[kResetExVtableIndex],(void*)hkReset9Ex,(void**)&origReset9Ex);(void)resetExOk;exPresentInstalled=hookTarget(v[kPresentExVtableIndex],(void*)hkPresent9Ex,(void**)&origPresent9Ex);}}}
    DestroyWindow(wnd);
    const bool installed=basePresentInstalled||swapPresentInstalled||exPresentInstalled;const bool was=d3d9HooksInstalled.exchange(installed,std::memory_order_acq_rel);if(installed&&!was)logAttachStage(AttachLogStage::CoreHooksInstalled,L"D3D9 Device/SwapChain/D3D9Ex presentation hooks installed (late-install capable)");return installed;
}
bool installD3D9Hooks(HMODULE module,SharedControl* control){ensureD3D9HooksInstalled(module,control);return true;}
void shutdownD3D9Compatibility(){std::scoped_lock l(mx);devices.clear();d3d9HooksInstalled.store(false,std::memory_order_release);basePresentInstalled=swapPresentInstalled=exPresentInstalled=false;lastInstallAttemptMs=0;}
}
