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
 const auto bridge=read(root/"src/bridge/bridge_main.cpp");
 const auto dxgi=read(root/"src/bridge/dxgi_hooks.cpp");
 const auto d9=read(root/"src/bridge/d3d9_hooks.cpp");
 const auto gl=read(root/"src/bridge/opengl_hooks.cpp");
 const auto vk=read(root/"src/bridge/vulkan_hooks.cpp");
 const auto ui=read(root/"src/controller/ui.cpp");
 bool ok=true;
 ok&=need(bridge,"shouldAutoDetach","bridge worker does not auto-detach after target renderer disappears");
 ok&=need(bridge,"processHasTopLevelWindow","bridge worker does not track target-window lifetime");
 ok&=need(dxgi,"noteRendererActivity","DXGI Present does not refresh renderer heartbeat");
 ok&=need(d9,"noteRendererActivity","D3D9 Present does not refresh renderer heartbeat");
 ok&=need(gl,"noteRendererActivity","OpenGL swap does not refresh renderer heartbeat");
 ok&=need(vk,"noteRendererActivity","Vulkan Present does not refresh renderer heartbeat");
 ok&=need(ui,"auto-detached after application exit","controller does not clear stale attached state after automatic detach");
 return ok?0:1;
}
