#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#ifndef UDLSS_SOURCE_DIR
#define UDLSS_SOURCE_DIR "."
#endif
static std::string read(const std::filesystem::path&p){std::ifstream f(p,std::ios::binary);std::ostringstream s;s<<f.rdbuf();return s.str();}
static bool need(const std::string&s,const std::string&n,const char*m){if(s.find(n)==std::string::npos){std::cerr<<m<<"\n";return false;}return true;}
int main(){
 const std::filesystem::path root=UDLSS_SOURCE_DIR;
 const auto dxgi=read(root/"src/bridge/dxgi_hooks.cpp");
 const auto on12h=read(root/"src/gpu/d3d12_on12.hpp");
 const auto on12=read(root/"src/gpu/d3d12_on12.cpp");
 const auto d3d9=read(root/"src/bridge/d3d9_hooks.cpp");
 const auto crash=read(root/"src/bridge/attach_logger.cpp");
 bool ok=true;
 ok&=need(dxgi,"RecoveryQueueFence","recovery path does not keep per-game-queue fences");
 ok&=need(dxgi,"snapshotRecoveryQueueWaits","recovery path does not snapshot current queue dependencies before Present");
 ok&=need(on12h,"D3D12QueueWaitPoint","D3D12On12 recovery API has no per-queue GPU wait points");
 ok&=need(on12,"D3D12_HEAP_FLAG_SHARED","D3D12 recovery path does not use a shareable D3D12 color resource");
 ok&=need(on12,"queue_->Wait","D3D12 recovery presentation queue does not wait on other game queue fences");
 ok&=need(on12,"setForceExternalHost","recovery pipeline does not force x64 NRHost isolation");
 ok&=need(d3d9,"IDirect3DSwapChain9::Present","D3D9 swapchain Present is not hooked");
 ok&=need(d3d9,"kSwapChainPresentVtableIndex=3","D3D9 swapchain Present vtable slot is missing");
 ok&=need(d3d9,"hkSwapPresent9","D3D9 swapchain Present detour is missing");
 ok&=need(crash,"RVA=0x","crash log does not include module-relative address");
 ok&=need(crash,"module=%ls","crash log does not include the faulting module name");
 return ok?0:1;
}
