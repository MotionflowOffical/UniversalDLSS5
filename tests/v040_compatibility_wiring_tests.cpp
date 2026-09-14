#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#ifndef UDLSS_SOURCE_DIR
#error UDLSS_SOURCE_DIR must be defined by CMake
#endif
static std::string readFile(const std::filesystem::path&p){std::ifstream f(p,std::ios::binary);if(!f)return{};std::ostringstream s;s<<f.rdbuf();return s.str();}
static bool has(const std::string&s,const char*t){return s.find(t)!=std::string::npos;}
int main(){
 const std::filesystem::path root=UDLSS_SOURCE_DIR;
 const auto procH=readFile(root/"src/controller/processes.hpp");
 const auto proc=readFile(root/"src/controller/processes.cpp");
 const auto picker=readFile(root/"include/udlss/app_picker_policy.hpp");
 const auto ui=readFile(root/"src/controller/ui.cpp");
 const auto bridge=readFile(root/"src/bridge/bridge_main.cpp");
 const auto d9=readFile(root/"src/bridge/d3d9_hooks.cpp");
 const auto ogl=readFile(root/"src/bridge/opengl_hooks.cpp");
 const auto vk=readFile(root/"src/bridge/vulkan_hooks.cpp");
 const auto dxgi=readFile(root/"src/bridge/dxgi_hooks.cpp");
 const auto d11=readFile(root/"src/gpu/d3d11_pipeline.cpp");
 const auto compatDispatch=readFile(root/"src/compat/compat_dispatch.cpp");
 const auto readme=readFile(root/"README.md");
 const auto release=readFile(root/"docs/V0.4.0_RELEASE.md");
 const auto verify=readFile(root/"tools/verify_package.py");
 const auto diag=readFile(root/"include/udlss/runtime_diagnostics.hpp");
 if(!has(proc,"d3d9.dll")||!has(proc,"d3d10.dll")||!has(proc,"d3d11.dll")||!has(proc,"d3d12.dll")||!has(proc,"opengl32.dll")||!has(proc,"vulkan-1.dll")){
  std::cerr<<"Process scanner does not detect all supported renderer modules\n";return 1;
 }
 if(!has(procH,"rendererModules")||!has(picker,"rendererModules")||!has(ui,"rendererModuleSummary")){
  std::cerr<<"Renderer module capability is not propagated into the application picker\n";return 1;
 }
 if(!has(ui,"Renderer route:")||!has(ui,"Compatibility frontend:")||!has(ui,"Route counters:")||!has(ui,"Detach mode:")){
  std::cerr<<"v0.4.0 route/compatibility diagnostics are not visible in the UI\n";return 1;
 }
 if(!has(diag,"Graphics hooks installed")){
  std::cerr<<"pipeline diagnostics still describe the multi-API bridge as DXGI-only\n";return 1;
 }
 if(!has(bridge,"ensureD3D9HooksInstalled")||!has(bridge,"installOpenGLHooks")||!has(bridge,"installVulkanHooks")||!has(dxgi,"D3D10Frontend")){
  std::cerr<<"Not all compatibility hook families are installed\n";return 1;
 }
 if(!has(d11,"External x64 Feature 18 host (x86 game)")||!has(d11,"INTPTR_MAX == INT32_MAX")){
  std::cerr<<"x86 games are not explicitly routed to the x64 Feature 18 host\n";return 1;
 }
 if(!has(compatDispatch,"NeuralExecutionLocation::ExternalHost")||has(compatDispatch,"NeuralExecutionLocation::ExternalProcess")){
  std::cerr<<"Compatibility dispatcher uses a stale neural execution location enum spelling\n";return 1;
 }
 if(!has(dxgi,"ModernD3D12Recovery")||!has(dxgi,"d3d12RecoveryTrusted")||!has(dxgi,"enhancedBarrier")){
  std::cerr<<"Modern D3D12 recovery path is missing\n";return 1;
 }
 if(!has(readme,"Direct3D 9")||!has(readme,"Direct3D 10")||!has(readme,"OpenGL")||!has(readme,"Vulkan")||!has(readme,"32-bit")){
  std::cerr<<"README does not document the current renderer support matrix\n";return 1;
 }
 if(release.empty()||!has(release,"UniversalDLSS5 v0.4.0")||!has(release,"Half Sword")||!has(release,"Call of Duty 4")){
  std::cerr<<"v0.4.0 release notes are missing compatibility validation targets\n";return 1;
 }
 if(!has(verify,"V0.4.0_RELEASE.md")||!has(verify,"vulkan_hooks.cpp")||!has(verify,"d3d9_hooks.cpp")||!has(verify,"opengl_hooks.cpp")){
  std::cerr<<"Package verifier does not cover the v0.4.0 compatibility release surfaces\n";return 1;
 }
 const auto all=procH+proc+picker+ui+bridge+d9+ogl+vk+dxgi+d11+readme+release;
 const char* forbidden[]={"IDXGIOutputDuplication","Desktop Duplication","PrintWindow(","BitBlt("};
 for(auto*t:forbidden)if(has(all,t)){std::cerr<<"Forbidden screen-capture fallback token found: "<<t<<"\n";return 1;}
 std::cout<<"v0.4.0 compatibility wiring OK\n";return 0;
}
