#include "opengl_hooks.hpp"
#include "hook_lifecycle.hpp"
#include "attach_logger.hpp"
#include "../compat/opengl_frontend.hpp"
#include "../compat/compat_dispatch.hpp"
#include "udlss/renderer_ownership_policy.hpp"
#include <gl/GL.h>
#include <MinHook.h>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <filesystem>
#include <atomic>
#include "udlss/renderer_activity.hpp"
namespace fs=std::filesystem;
namespace udlss::bridge {
namespace {
using SwapBuffersFn=BOOL(WINAPI*)(HDC);using MakeCurrentFn=BOOL(WINAPI*)(HDC,HGLRC);using DeleteContextFn=BOOL(WINAPI*)(HGLRC);
SwapBuffersFn origSwapBuffers{};MakeCurrentFn origWglMakeCurrent{};DeleteContextFn origWglDeleteContext{};HMODULE self{};SharedControl* ctl{};std::mutex mx;thread_local bool inside{};std::atomic_bool openglHooksInstalled{false};
struct ContextCtx{std::unique_ptr<compat::OpenGLFrontend> frontend;std::unique_ptr<compat::CompatDispatch> dispatch;std::uint64_t frames{},processed{},bypassed{},neural{};std::uint64_t lastTick{};double fps{};};std::unordered_map<HGLRC,std::unique_ptr<ContextCtx>> contexts;
std::wstring moduleDir(){wchar_t p[32768]{};const DWORD n=GetModuleFileNameW(self,p,_countof(p));return fs::path(std::wstring(p,n)).parent_path().wstring();}std::wstring runtimePath(){return ctl&&ctl->raw()?std::wstring(ctl->raw()->runtimePath):std::wstring{};}
ContextCtx& getCtx(HGLRC rc){std::scoped_lock l(mx);auto& p=contexts[rc];if(!p)p=std::make_unique<ContextCtx>();return *p;}void updateFps(ContextCtx&c){const auto now=GetTickCount64();if(c.lastTick){const double inst=1000.0/(double)std::max<std::uint64_t>(1,now-c.lastTick);c.fps=c.fps?c.fps*.9+inst*.1:inst;}c.lastTick=now;}
bool processOpenGL(HDC dc){if(!ctl||!ctl->valid()||inside||!processCompatibilityRendererAllowed(GraphicsApi::OpenGL))return false;HGLRC rc=wglGetCurrentContext();if(!rc)return false;inside=true;auto& c=getCtx(rc);++c.frames;updateFps(c);RuntimeStatus st{};st.pid=GetCurrentProcessId();st.api=GraphicsApi::OpenGL;st.rendererRoute=RendererRoute::CompatOpenGL;st.gameArchitectureBits=sizeof(void*)*8u;st.state=RuntimeState::Hooked;st.lastTickMs=GetTickCount64();markPipelineStage(st.stageMask,PipelineStage::BridgeInjected);markPipelineStage(st.stageMask,PipelineStage::HooksInstalled);markPipelineStage(st.stageMask,PipelineStage::PresentObserved);markPipelineStage(st.stageMask,PipelineStage::SourceApiDetected);const auto settings=ctl->readSettings();if(!settings.enabled||c.frames<3){++c.bypassed;st.state=RuntimeState::Bypassed;wcscpy_s(st.message,!settings.enabled?L"processing disabled":L"safe attach: waiting for stable OpenGL swaps");st.presentedFrames=c.frames;st.bypassedFrames=c.bypassed;ctl->writeStatus(st);inside=false;return false;}const auto elected=ctl->primaryRendererPid();if(elected&&elected!=GetCurrentProcessId()){++c.bypassed;inside=false;return false;}if(!c.frontend){c.frontend=std::make_unique<compat::OpenGLFrontend>();if(!c.frontend->initialize(dc,rc,st)){c.frontend.reset();++c.bypassed;st.state=RuntimeState::Error;st.presentedFrames=c.frames;st.bypassedFrames=c.bypassed;ctl->writeStatus(st);inside=false;return false;}c.dispatch=std::make_unique<compat::CompatDispatch>();}st.compatInterop=c.frontend->interop();const bool ok=c.dispatch->process(*c.frontend,settings,moduleDir(),runtimePath(),st);if(ok){++c.processed;if(st.neuralActive){++c.neural;st.state=RuntimeState::Processing;}else{++c.bypassed;st.state=RuntimeState::Bypassed;}}else{++c.bypassed;st.state=st.failureStage==PipelineStage::None?RuntimeState::Bypassed:RuntimeState::Error;}st.presentedFrames=c.frames;st.processedFrames=c.processed;st.bypassedFrames=c.bypassed;st.neuralFrames=c.neural;st.estimatedFps=(float)c.fps;ctl->writeStatus(st);inside=false;return ok;}
BOOL WINAPI hkSwapBuffers(HDC dc){noteRendererActivity();HookCallScope call;if(call.customWorkAllowed())processOpenGL(dc);return origSwapBuffers(dc);}BOOL WINAPI hkWglMakeCurrent(HDC dc,HGLRC rc){HookCallScope call;return origWglMakeCurrent(dc,rc);}BOOL WINAPI hkWglDeleteContext(HGLRC rc){HookCallScope call;if(call.customWorkAllowed()){std::scoped_lock l(mx);contexts.erase(rc);}return origWglDeleteContext(rc);}
bool hookTarget(void*t,void*d,void**o){if(!t)return false;const auto r=MH_CreateHook(t,d,o);if(r!=MH_OK&&r!=MH_ERROR_ALREADY_CREATED)return false;const auto e=MH_EnableHook(t);return e==MH_OK||e==MH_ERROR_ENABLED;}
}
bool installOpenGLHooks(HMODULE module,SharedControl* control){self=module;ctl=control;if(openglHooksInstalled.load(std::memory_order_acquire))return true;HMODULE ogl=GetModuleHandleW(L"opengl32.dll");if(!ogl)return true;HMODULE gdi=GetModuleHandleW(L"gdi32.dll");if(!gdi)return false;auto swap=(void*)GetProcAddress(gdi,"SwapBuffers");auto make=(void*)GetProcAddress(ogl,"wglMakeCurrent");auto del=(void*)GetProcAddress(ogl,"wglDeleteContext");const bool ok=hookTarget(swap,(void*)hkSwapBuffers,(void**)&origSwapBuffers)&&hookTarget(make,(void*)hkWglMakeCurrent,(void**)&origWglMakeCurrent)&&hookTarget(del,(void*)hkWglDeleteContext,(void**)&origWglDeleteContext);if(ok)openglHooksInstalled.store(true,std::memory_order_release);if(ok)logAttachStage(AttachLogStage::CoreHooksInstalled,L"OpenGL SwapBuffers/wglMakeCurrent hooks installed");return true;}
void shutdownOpenGLCompatibility(){std::scoped_lock l(mx);contexts.clear();openglHooksInstalled.store(false,std::memory_order_release);}
}
