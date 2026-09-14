#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#ifndef UDLSS_SOURCE_DIR
#define UDLSS_SOURCE_DIR "."
#endif
static std::string read(const char* rel){std::ifstream f(std::string(UDLSS_SOURCE_DIR)+"/"+rel);std::ostringstream s;s<<f.rdbuf();return s.str();}
static bool need(const std::string&s,const char*n,const char*m){if(s.find(n)!=std::string::npos)return true;std::cerr<<m<<"\n";return false;}
int main(){
    const auto ui=read("src/controller/ui.cpp");
    const auto dxgi=read("src/bridge/dxgi_hooks.cpp");
    const auto on12h=read("src/gpu/d3d12_on12.hpp");
    const auto on12=read("src/gpu/d3d12_on12.cpp");
    bool ok=true;
    ok&=need(ui,"shouldAttachRendererProcess(p.pid==root.pid,p.hasDxgi,p.rendererModules)","process-tree attach is still DXGI-only and will skip D3D9/OpenGL/Vulkan renderer children");
    ok&=need(dxgi,"chooseD3D12Route(c.queueProof,c.recovery,st.lastTickMs)","D3D12 queue classification does not distinguish exact creation queues from inferred recovery queues");
    ok&=need(dxgi,"synchronizeRecoveryBeforePresent","D3D12 recovery route is not requesting a safe completion wait before Present");
    ok&=need(on12h,"bool synchronizeBeforePresent=false","D3D12On12 process API cannot isolate recovery-only synchronization");
    ok&=need(on12,"if(synchronizeBeforePresent && !waitCopyContext(postCopy_,st))return false;","recovery path does not wait for the injected post-copy to finish before DXGI Present");
    return ok?0:1;
}
