#include "vulkan_hooks.hpp"
#include "hook_lifecycle.hpp"
#include "../compat/vulkan_abi.hpp"
#include "../compat/vulkan_frontend.hpp"
#include "../compat/compat_dispatch.hpp"
#include "udlss/renderer_ownership_policy.hpp"
#include <MinHook.h>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include "udlss/renderer_activity.hpp"

namespace fs=std::filesystem;
namespace udlss::bridge {
namespace {
using namespace udlss::compat::vkabi;
HMODULE g_self{};SharedControl* g_control{};std::mutex g_mutex;bool g_installed{};

PFN_vkGetInstanceProcAddr origGetInstanceProcAddr{};PFN_vkGetDeviceProcAddr origGetDeviceProcAddr{};
PFN_vkCreateDevice origCreateDevice{};PFN_vkDestroyDevice origDestroyDevice{};PFN_vkGetDeviceQueue origGetDeviceQueue{};
PFN_vkCreateSwapchainKHR origCreateSwapchainKHR{};PFN_vkDestroySwapchainKHR origDestroySwapchainKHR{};PFN_vkGetSwapchainImagesKHR origGetSwapchainImagesKHR{};PFN_vkQueuePresentKHR origQueuePresentKHR{};

struct DeviceState{VkPhysicalDevice physical{};};
struct QueueState{VkDevice device{};std::uint32_t family{};};
struct SwapchainState{
    VkDevice device{};VkPhysicalDevice physical{};VkSwapchainCreateInfoKHR desc{};std::vector<std::uint32_t> queueFamilies;std::vector<VkImage> images;
    std::unique_ptr<compat::VulkanFrontend> frontend;std::unique_ptr<compat::CompatDispatch> dispatch;
    std::uint64_t presented{},processed{},bypassed{},neural{};
};
std::unordered_map<VkDevice,DeviceState> devices;std::unordered_map<VkQueue,QueueState> queues;std::unordered_map<VkSwapchainKHR,std::shared_ptr<SwapchainState>> swapchains;

std::wstring moduleDir(){wchar_t p[32768]{};const auto n=GetModuleFileNameW(g_self,p,(DWORD)_countof(p));return fs::path(std::wstring(p,n)).parent_path().wstring();}
std::wstring runtimePath(){return g_control&&g_control->raw()?g_control->raw()->runtimePath:L"";}
void markBase(RuntimeStatus& st,const SwapchainState* sc=nullptr){st.pid=GetCurrentProcessId();st.api=GraphicsApi::Vulkan;st.rendererRoute=RendererRoute::CompatVulkan;st.compatInterop=CompatInterop::VulkanExternalMemory;st.gameArchitectureBits=sizeof(void*)*8u;st.state=RuntimeState::Bypassed;st.lastTickMs=GetTickCount64();markPipelineStage(st.stageMask,PipelineStage::BridgeInjected);markPipelineStage(st.stageMask,PipelineStage::HooksInstalled);markPipelineStage(st.stageMask,PipelineStage::PresentObserved);markPipelineStage(st.stageMask,PipelineStage::SourceApiDetected);if(sc){st.width=sc->desc.imageExtent.width;st.height=sc->desc.imageExtent.height;st.colorPathWidth=st.width;st.colorPathHeight=st.height;st.presentedFrames=sc->presented;st.processedFrames=sc->processed;st.bypassedFrames=sc->bypassed;st.neuralFrames=sc->neural;}}
void publishUnsupported(const wchar_t* message,PipelineStage failure=PipelineStage::SourceApiUnsupported){if(!g_control)return;RuntimeStatus st{};markBase(st);st.failureStage=failure;wcscpy_s(st.message,message);g_control->writeStatus(st);}

template<class T> void rememberOriginal(T& slot,PFN_vkVoidFunction fn){if(!slot&&fn)slot=reinterpret_cast<T>(fn);}

PFN_vkVoidFunction VKAPI_CALL hookGetInstanceProcAddr(VkInstance instance,const char* name);
PFN_vkVoidFunction VKAPI_CALL hookGetDeviceProcAddr(VkDevice device,const char* name);
VkResult VKAPI_CALL hookCreateDevice(VkPhysicalDevice physical,const void* ci,const void* alloc,VkDevice* out);
void VKAPI_CALL hookDestroyDevice(VkDevice device,const void* alloc);
void VKAPI_CALL hookGetDeviceQueue(VkDevice device,std::uint32_t family,std::uint32_t index,VkQueue* out);
VkResult VKAPI_CALL hookCreateSwapchainKHR(VkDevice device,const VkSwapchainCreateInfoKHR* ci,const void* alloc,VkSwapchainKHR* out);
void VKAPI_CALL hookDestroySwapchainKHR(VkDevice device,VkSwapchainKHR swapchain,const void* alloc);
VkResult VKAPI_CALL hookGetSwapchainImagesKHR(VkDevice device,VkSwapchainKHR swapchain,std::uint32_t* count,VkImage* images);
VkResult VKAPI_CALL hookQueuePresentKHR(VkQueue queue,const VkPresentInfoKHR* present);

PFN_vkVoidFunction wrapInstanceProc(const char* name,PFN_vkVoidFunction real){if(!name)return real;
    if(std::strcmp(name,"vkGetInstanceProcAddr")==0)return reinterpret_cast<PFN_vkVoidFunction>(hookGetInstanceProcAddr);
    if(std::strcmp(name,"vkGetDeviceProcAddr")==0){rememberOriginal(origGetDeviceProcAddr,real);return reinterpret_cast<PFN_vkVoidFunction>(hookGetDeviceProcAddr);}
    if(std::strcmp(name,"vkCreateDevice")==0){rememberOriginal(origCreateDevice,real);return reinterpret_cast<PFN_vkVoidFunction>(hookCreateDevice);}
    return real;
}
PFN_vkVoidFunction wrapDeviceProc(const char* name,PFN_vkVoidFunction real){if(!name)return real;
    if(std::strcmp(name,"vkGetDeviceQueue")==0){rememberOriginal(origGetDeviceQueue,real);return reinterpret_cast<PFN_vkVoidFunction>(hookGetDeviceQueue);}
    if(std::strcmp(name,"vkDestroyDevice")==0){rememberOriginal(origDestroyDevice,real);return reinterpret_cast<PFN_vkVoidFunction>(hookDestroyDevice);}
    if(std::strcmp(name,"vkCreateSwapchainKHR")==0){rememberOriginal(origCreateSwapchainKHR,real);return reinterpret_cast<PFN_vkVoidFunction>(hookCreateSwapchainKHR);}
    if(std::strcmp(name,"vkDestroySwapchainKHR")==0){rememberOriginal(origDestroySwapchainKHR,real);return reinterpret_cast<PFN_vkVoidFunction>(hookDestroySwapchainKHR);}
    if(std::strcmp(name,"vkGetSwapchainImagesKHR")==0){rememberOriginal(origGetSwapchainImagesKHR,real);return reinterpret_cast<PFN_vkVoidFunction>(hookGetSwapchainImagesKHR);}
    if(std::strcmp(name,"vkQueuePresentKHR")==0){rememberOriginal(origQueuePresentKHR,real);return reinterpret_cast<PFN_vkVoidFunction>(hookQueuePresentKHR);}
    return real;
}

PFN_vkVoidFunction VKAPI_CALL hookGetInstanceProcAddr(VkInstance instance,const char* name){HookCallScope call;auto real=origGetInstanceProcAddr?origGetInstanceProcAddr(instance,name):nullptr;if(!call.customWorkAllowed())return real;return wrapInstanceProc(name,real);}
PFN_vkVoidFunction VKAPI_CALL hookGetDeviceProcAddr(VkDevice device,const char* name){HookCallScope call;auto real=origGetDeviceProcAddr?origGetDeviceProcAddr(device,name):nullptr;if(!call.customWorkAllowed())return real;return wrapDeviceProc(name,real);}
VkResult VKAPI_CALL hookCreateDevice(VkPhysicalDevice physical,const void* ci,const void* alloc,VkDevice* out){HookCallScope call;if(!origCreateDevice)return -3;const auto r=origCreateDevice(physical,ci,alloc,out);if(call.customWorkAllowed()&&r==VK_SUCCESS&&out&&*out){std::scoped_lock lock(g_mutex);devices[*out]={physical};}return r;}
void VKAPI_CALL hookDestroyDevice(VkDevice device,const void* alloc){HookCallScope call;std::vector<std::shared_ptr<SwapchainState>> retired;if(call.customWorkAllowed()){std::scoped_lock lock(g_mutex);for(auto it=swapchains.begin();it!=swapchains.end();){if(it->second&&it->second->device==device){retired.push_back(it->second);it=swapchains.erase(it);}else ++it;}for(auto it=queues.begin();it!=queues.end();)if(it->second.device==device)it=queues.erase(it);else ++it;devices.erase(device);}retired.clear();if(origDestroyDevice)origDestroyDevice(device,alloc);}
void VKAPI_CALL hookGetDeviceQueue(VkDevice device,std::uint32_t family,std::uint32_t index,VkQueue* out){HookCallScope call;if(origGetDeviceQueue)origGetDeviceQueue(device,family,index,out);if(call.customWorkAllowed()&&out&&*out){std::scoped_lock lock(g_mutex);queues[*out]={device,family};}}
VkResult VKAPI_CALL hookCreateSwapchainKHR(VkDevice device,const VkSwapchainCreateInfoKHR* ci,const void* alloc,VkSwapchainKHR* out){HookCallScope call;if(!origCreateSwapchainKHR)return -3;const auto r=origCreateSwapchainKHR(device,ci,alloc,out);if(call.customWorkAllowed()&&r==VK_SUCCESS&&ci&&out&&*out){auto sc=std::make_shared<SwapchainState>();sc->device=device;sc->desc=*ci;if(ci->queueFamilyIndexCount&&ci->pQueueFamilyIndices)sc->queueFamilies.assign(ci->pQueueFamilyIndices,ci->pQueueFamilyIndices+ci->queueFamilyIndexCount);sc->desc.pQueueFamilyIndices=sc->queueFamilies.empty()?nullptr:sc->queueFamilies.data();{std::scoped_lock lock(g_mutex);auto dit=devices.find(device);if(dit!=devices.end())sc->physical=dit->second.physical;swapchains[*out]=sc;}}return r;}
void VKAPI_CALL hookDestroySwapchainKHR(VkDevice device,VkSwapchainKHR swapchain,const void* alloc){HookCallScope call;std::shared_ptr<SwapchainState> retired;if(call.customWorkAllowed()){std::scoped_lock lock(g_mutex);auto it=swapchains.find(swapchain);if(it!=swapchains.end()){retired=it->second;swapchains.erase(it);}}retired.reset();if(origDestroySwapchainKHR)origDestroySwapchainKHR(device,swapchain,alloc);}
VkResult VKAPI_CALL hookGetSwapchainImagesKHR(VkDevice device,VkSwapchainKHR swapchain,std::uint32_t* count,VkImage* images){HookCallScope call;if(!origGetSwapchainImagesKHR)return -3;const auto r=origGetSwapchainImagesKHR(device,swapchain,count,images);if(call.customWorkAllowed()&&r==VK_SUCCESS&&count&&images){std::scoped_lock lock(g_mutex);auto it=swapchains.find(swapchain);if(it!=swapchains.end()&&it->second)it->second->images.assign(images,images+*count);}return r;}

bool ensureImages(const std::shared_ptr<SwapchainState>& sc,VkSwapchainKHR swapchain){if(!sc||!origGetSwapchainImagesKHR)return false;if(!sc->images.empty())return true;std::uint32_t count=0;if(origGetSwapchainImagesKHR(sc->device,swapchain,&count,nullptr)!=VK_SUCCESS||!count)return false;std::vector<VkImage> images(count);if(origGetSwapchainImagesKHR(sc->device,swapchain,&count,images.data())!=VK_SUCCESS||!count)return false;images.resize(count);sc->images=std::move(images);return true;}

VkResult VKAPI_CALL hookQueuePresentKHR(VkQueue queue,const VkPresentInfoKHR* present){noteRendererActivity();HookCallScope call;if(!origQueuePresentKHR)return -3;if(!processCompatibilityRendererAllowed(GraphicsApi::Vulkan))return origQueuePresentKHR(queue,present);if(!call.customWorkAllowed()||!present||!present->swapchainCount||!present->pSwapchains||!present->pImageIndices)return origQueuePresentKHR(queue,present);
    QueueState qs{};{std::scoped_lock lock(g_mutex);auto q=queues.find(queue);if(q!=queues.end())qs=q->second;}
    if(!qs.device){publishUnsupported(L"Vulkan compatibility observed Present after device queues were created; recreate the Vulkan renderer/swapchain or attach earlier");return origQueuePresentKHR(queue,present);}
    for(std::uint32_t slot=0;slot<present->swapchainCount;++slot){std::shared_ptr<SwapchainState> sc;{std::scoped_lock lock(g_mutex);auto it=swapchains.find(present->pSwapchains[slot]);if(it!=swapchains.end())sc=it->second;}if(!sc||sc->device!=qs.device)continue;
        ++sc->presented;RuntimeStatus st{};markBase(st,sc.get());st.presentedFrames=sc->presented;
        if(!sc->physical){st.failureStage=PipelineStage::SourceApiUnsupported;wcscpy_s(st.message,L"Vulkan compatibility lacks the physical-device mapping because the renderer was created before attach");++sc->bypassed;st.bypassedFrames=sc->bypassed;if(g_control)g_control->writeStatus(st);continue;}
        if(!ensureImages(sc,present->pSwapchains[slot])){st.failureStage=PipelineStage::GuideResourcesFailed;wcscpy_s(st.message,L"Vulkan compatibility could not enumerate swapchain images");++sc->bypassed;st.bypassedFrames=sc->bypassed;if(g_control)g_control->writeStatus(st);continue;}
        if(!sc->frontend){sc->frontend=std::make_unique<compat::VulkanFrontend>();if(!sc->frontend->initialize(sc->physical,sc->device,qs.family,sc->desc,sc->images,origGetInstanceProcAddr,origGetDeviceProcAddr,st)){sc->frontend.reset();++sc->bypassed;st.bypassedFrames=sc->bypassed;if(g_control)g_control->writeStatus(st);continue;}sc->dispatch=std::make_unique<compat::CompatDispatch>();}
        if(!sc->frontend->preparePresent(queue,*present,slot,st)){++sc->bypassed;st.bypassedFrames=sc->bypassed;if(g_control)g_control->writeStatus(st);continue;}
        const bool ok=sc->dispatch&&sc->dispatch->process(*sc->frontend,g_control?g_control->readSettings():Settings{},moduleDir(),runtimePath(),st);if(ok){++sc->processed;st.processedFrames=sc->processed;if(st.neuralActive){++sc->neural;st.neuralFrames=sc->neural;st.state=RuntimeState::Processing;}else st.state=RuntimeState::Bypassed;}else{++sc->bypassed;st.bypassedFrames=sc->bypassed;st.state=st.failureStage==PipelineStage::None?RuntimeState::Bypassed:RuntimeState::Error;}if(g_control)g_control->writeStatus(st);
        VkPresentInfoKHR modified=*present;VkSemaphore post=sc->frontend->presentWaitSemaphore();if(sc->frontend->postCopySubmitted()){modified.waitSemaphoreCount=1;modified.pWaitSemaphores=&post;}else if(sc->frontend->originalWaitsConsumed()){modified.waitSemaphoreCount=0;modified.pWaitSemaphores=nullptr;}return origQueuePresentKHR(queue,&modified);
    }
    return origQueuePresentKHR(queue,present);
}

bool createHook(void* target,void* detour,void** original){if(!target)return true;const auto c=MH_CreateHook(target,detour,original);if(c!=MH_OK&&c!=MH_ERROR_ALREADY_CREATED)return false;const auto e=MH_EnableHook(target);return e==MH_OK||e==MH_ERROR_ENABLED;}
}

bool installVulkanHooks(HMODULE self,SharedControl* control){g_self=self;g_control=control;auto vk=GetModuleHandleW(L"vulkan-1.dll");if(!vk)return true;std::scoped_lock lock(g_mutex);if(g_installed)return true;auto gipa=reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(vk,"vkGetInstanceProcAddr"));auto gdpa=reinterpret_cast<PFN_vkGetDeviceProcAddr>(GetProcAddress(vk,"vkGetDeviceProcAddr"));if(!gipa||!gdpa)return false;bool ok=createHook(reinterpret_cast<void*>(gipa),reinterpret_cast<void*>(hookGetInstanceProcAddr),reinterpret_cast<void**>(&origGetInstanceProcAddr));ok&=createHook(reinterpret_cast<void*>(gdpa),reinterpret_cast<void*>(hookGetDeviceProcAddr),reinterpret_cast<void**>(&origGetDeviceProcAddr));
    // Also hook exported trampolines when the loader exposes them. This lets a bridge
    // attached after renderer initialization catch function pointers the game already cached.
#define UDLSS_HOOK_EXPORT(name,hook,orig) do{if(auto p=GetProcAddress(vk,#name))ok&=createHook(reinterpret_cast<void*>(p),reinterpret_cast<void*>(hook),reinterpret_cast<void**>(&orig));}while(0)
    UDLSS_HOOK_EXPORT(vkCreateDevice,hookCreateDevice,origCreateDevice);UDLSS_HOOK_EXPORT(vkDestroyDevice,hookDestroyDevice,origDestroyDevice);UDLSS_HOOK_EXPORT(vkGetDeviceQueue,hookGetDeviceQueue,origGetDeviceQueue);UDLSS_HOOK_EXPORT(vkCreateSwapchainKHR,hookCreateSwapchainKHR,origCreateSwapchainKHR);UDLSS_HOOK_EXPORT(vkDestroySwapchainKHR,hookDestroySwapchainKHR,origDestroySwapchainKHR);UDLSS_HOOK_EXPORT(vkGetSwapchainImagesKHR,hookGetSwapchainImagesKHR,origGetSwapchainImagesKHR);UDLSS_HOOK_EXPORT(vkQueuePresentKHR,hookQueuePresentKHR,origQueuePresentKHR);
#undef UDLSS_HOOK_EXPORT
    g_installed=ok;if(!ok)publishUnsupported(L"Vulkan loader was detected but compatibility hooks could not be installed",PipelineStage::HookInstallFailed);return ok;}

void shutdownVulkanCompatibility(){std::unordered_map<VkSwapchainKHR,std::shared_ptr<SwapchainState>> retired;{std::scoped_lock lock(g_mutex);retired.swap(swapchains);queues.clear();devices.clear();g_installed=false;}retired.clear();}

} // namespace udlss::bridge
