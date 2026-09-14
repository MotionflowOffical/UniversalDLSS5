#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
static std::string read(const std::filesystem::path&p){std::ifstream f(p,std::ios::binary);std::ostringstream s;s<<f.rdbuf();return s.str();}
static bool need(const std::string&s,const std::string&n,const char*m){if(s.find(n)==std::string::npos){std::cerr<<m<<"\n";return false;}return true;}
int main(){
 const std::filesystem::path root=UDLSS_SOURCE_DIR;
 const auto h=read(root/"src/compat/d3d10_frontend.hpp");
 const auto cpp=read(root/"src/compat/d3d10_frontend.cpp");
 const auto dxgi=read(root/"src/bridge/dxgi_hooks.cpp");
 const auto cm=read(root/"CMakeLists.txt");
 bool ok=true;
 ok&=need(h,"class D3D10Frontend","D3D10 frontend missing");
 ok&=need(cpp,"D3D10_RESOURCE_MISC_SHARED_KEYEDMUTEX","D3D10 shared keyed-mutex texture missing");
 ok&=need(cpp,"OpenSharedResource","D3D11 shared-resource open missing");
 ok&=need(cpp,"AcquireSync","D3D10/D3D11 keyed mutex synchronization missing");
 ok&=need(dxgi,"GraphicsApi::D3D10","DXGI D3D10 detection missing");
 ok&=need(dxgi,"RendererRoute::CompatD3D10","D3D10 route missing");
 ok&=need(cm,"d3d10_frontend.cpp","D3D10 frontend not built");
 ok&=need(cm,"d3d10","D3D10 library not linked");
 return ok?0:1;
}
