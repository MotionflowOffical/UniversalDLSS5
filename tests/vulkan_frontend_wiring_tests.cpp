#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#ifndef UDLSS_SOURCE_DIR
#error UDLSS_SOURCE_DIR must be defined by CMake
#endif

static std::string readFile(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return {};
    std::ostringstream ss; ss << f.rdbuf(); return ss.str();
}

static bool has(const std::string& s,const char* token){ return s.find(token)!=std::string::npos; }

int main(){
    const std::filesystem::path root=UDLSS_SOURCE_DIR;
    const auto abi=readFile(root/"src"/"compat"/"vulkan_abi.hpp");
    const auto frontendH=readFile(root/"src"/"compat"/"vulkan_frontend.hpp");
    const auto frontend=readFile(root/"src"/"compat"/"vulkan_frontend.cpp");
    const auto hooksH=readFile(root/"src"/"bridge"/"vulkan_hooks.hpp");
    const auto hooks=readFile(root/"src"/"bridge"/"vulkan_hooks.cpp");
    const auto bridge=readFile(root/"src"/"bridge"/"bridge_main.cpp");
    const auto cmake=readFile(root/"CMakeLists.txt");

    if(abi.empty()||frontendH.empty()||frontend.empty()||hooksH.empty()||hooks.empty()){
        std::cerr<<"Vulkan compatibility source files are missing\n";return 1;
    }
    const char* loaderTokens[]={"vkGetInstanceProcAddr","vkGetDeviceProcAddr","vkCreateDevice","vkGetDeviceQueue","vkCreateSwapchainKHR","vkDestroySwapchainKHR","vkGetSwapchainImagesKHR","vkQueuePresentKHR"};
    for(auto* t:loaderTokens) if(!has(hooks,t)){std::cerr<<"Vulkan hook layer is missing "<<t<<"\n";return 1;}
    if(!has(hooks,"GetModuleHandleW(L\"vulkan-1.dll\")") || !has(hooks,"GetProcAddress") || has(cmake,"vulkan-1")){
        std::cerr<<"Vulkan path must use dynamic loader interception without a hard Vulkan link dependency\n";return 1;
    }
    if(!has(frontend,"VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT") ||
       !has(frontend,"VkExternalMemoryImageCreateInfo") ||
       !has(frontend,"VkImportMemoryWin32HandleInfoKHR") ||
       !has(frontend,"CreateSharedHandle")){
        std::cerr<<"Vulkan frontend is missing D3D11 external-memory interop\n";return 1;
    }
    if(!has(frontend,"vkCmdPipelineBarrier") || !has(frontend,"vkCmdCopyImage") ||
       !has(frontend,"vkQueueSubmit") || !has(frontend,"vkQueueWaitIdle")){
        std::cerr<<"Vulkan frontend is missing explicit copy/synchronization processing\n";return 1;
    }
    if(!has(frontend,"RendererRoute::CompatVulkan") || !has(frontend,"CompatInterop::VulkanExternalMemory") ||
       !has(frontend,"full-resolution")){
        std::cerr<<"Vulkan route diagnostics/full-resolution invariant are missing\n";return 1;
    }
    if(!has(hooks,"HookCallScope") || !has(bridge,"installVulkanHooks") || !has(bridge,"shutdownVulkanCompatibility")){
        std::cerr<<"Vulkan hooks are not integrated with quiescent attach/detach\n";return 1;
    }
    if(!has(cmake,"src/bridge/vulkan_hooks.cpp") || !has(cmake,"src/compat/vulkan_frontend.cpp")){
        std::cerr<<"Vulkan compatibility sources are not wired into the bridge build\n";return 1;
    }
    const auto all=abi+frontendH+frontend+hooksH+hooks;
    if(has(all,"BitBlt")||has(all,"PrintWindow")||has(all,"Desktop Duplication")||has(all,"IDXGIOutputDuplication")){
        std::cerr<<"Screen-capture fallback is forbidden\n";return 1;
    }
    std::cout<<"Vulkan compatibility wiring OK\n";
    return 0;
}
