#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
static std::string read(const std::filesystem::path&p){std::ifstream f(p,std::ios::binary);std::ostringstream s;s<<f.rdbuf();return s.str();}
static bool need(const std::string&s,const std::string&n,const char*m){if(s.find(n)==std::string::npos){std::cerr<<m<<"\n";return false;}return true;}
int main(){
 const std::filesystem::path root=UDLSS_SOURCE_DIR;
 const auto dispatch=read(root/"src/compat/compat_dispatch.cpp");
 const auto h=read(root/"src/compat/renderer_frontend.hpp");
 const auto dxgi=read(root/"src/bridge/dxgi_hooks.cpp");
 const auto route=read(root/"include/udlss/d3d12_route_policy.hpp");
 bool ok=true;
 ok&=need(dispatch,"routeNeedsCompatDispatch","compat dispatch does not guard native routes");
 ok&=need(dispatch,"isCompatibilityRendererRoute","compat dispatch does not reject non-compat routes");
 ok&=need(h,"IRendererFrontend","compat frontend interface missing");
 ok&=need(h,"CompatFrame","canonical compatibility frame missing");
 ok&=need(dxgi,"RendererRoute::NativeD3D11","native D3D11 route missing");
 ok&=need(route,"RendererRoute::NativeD3D12","native D3D12 route missing from exact-queue classifier");
 ok&=need(route,"synchronizeBeforePresent=false","native D3D12 route unexpectedly inherits recovery synchronization");
 return ok?0:1;
}
