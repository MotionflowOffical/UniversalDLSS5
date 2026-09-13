#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#ifndef UDLSS_SOURCE_DIR
#error UDLSS_SOURCE_DIR must be defined
#endif
static std::string read(const std::filesystem::path&p){std::ifstream f(p,std::ios::binary);std::ostringstream s;s<<f.rdbuf();return s.str();}
static bool need(const std::string&s,const char*t,const char*m){if(s.find(t)!=std::string::npos)return true;std::cerr<<m<<" missing: "<<t<<"\n";return false;}
static bool forbid(const std::string&s,const char*t,const char*m){if(s.find(t)==std::string::npos)return true;std::cerr<<m<<" found: "<<t<<"\n";return false;}
int main(){
 const std::filesystem::path r=UDLSS_SOURCE_DIR;
 const auto cm=read(r/"CMakeLists.txt"), bridge=read(r/"src/bridge/dxgi_hooks.cpp"), tracker=read(r/"src/gpu/d3d12_resource_tracker.cpp"), trackerH=read(r/"src/gpu/d3d12_resource_tracker.hpp"), on12=read(r/"src/gpu/d3d12_on12.cpp"), ui=read(r/"src/controller/ui.cpp"), rc=read(r/"resources/UniversalDLSS5.rc"), bw=read(r/"BUILD_WINDOWS.bat"), br=read(r/"BUILD_RELEASE.bat"), host=read(r/"src/host/main.cpp"), ngx=read(r/"src/neural/ngx_nr.cpp"), sl=read(r/"src/neural/streamline_nr.cpp"), logh=read(r/"src/bridge/attach_logger.hpp"), logc=read(r/"src/bridge/attach_logger.cpp");
 bool ok=true;
 ok&=need(cm,"project(UniversalDLSS5 VERSION 0.3.1","CMake version is not v0.3.1");
 ok&=need(bw,"UniversalDLSS5 v0.3.1","Windows build banner is stale");
 ok&=need(br,"UniversalDLSS5 v0.3.1","release build banner is stale");
 ok&=need(ui,"v0.3.1","controller UI version is stale");
 ok&=need(rc,"FILEVERSION 0,3,1,0","Windows file version is stale");
 ok&=need(rc,"\"0.3.1.0\\0\"","Windows string version is stale");
 ok&=need(host,"0.3.1","NRHost engine version is stale");
 ok&=need(ngx,"0.3.1","NGX engine version is stale");
 ok&=need(sl,"UniversalDLSS5-0.3.1","Streamline engine version is stale");

 ok&=need(trackerH,"takeD3D12CommandListTouches","D3D12 tracker lacks command-list touch extraction");
 ok&=need(tracker,"commandListTouches","D3D12 tracker does not record command-list resource touches");
 ok&=need(bridge,"registerSwapchainBackbuffers","bridge does not register swapchain backbuffers for queue proof");
 ok&=need(bridge,"observePresentQueueEvidence","bridge does not score queue ownership evidence");
 ok&=need(bridge,"presentQueueTrusted","bridge does not require trusted presentation queue");
 ok&=forbid(bridge,"recentQueue","unsafe recent DIRECT queue fallback remains");

 ok&=need(on12,"ownedColor12_","D3D12On12 does not use injector-owned color staging");
 ok&=need(on12,"submitBackbufferCopy","D3D12On12 staging copy path is missing");
 ok&=forbid(on12,"CreateWrappedResource(bb.Get()","game swapchain backbuffer is still wrapped directly by D3D11On12");

 ok&=need(logh,"CrashStage","fine crash-stage enum is missing");
 ok&=need(logc,"AddVectoredExceptionHandler","vectored crash-stage logger is missing");
 ok&=need(bridge,"CrashStage::CallingPresent","actual Present stage is not marked");
 ok&=need(on12,"CrashStage::PreCopy","D3D12 pre-copy crash stage is missing");
 ok&=need(on12,"CrashStage::PostCopy","D3D12 post-copy crash stage is missing");
 return ok?0:1;
}
