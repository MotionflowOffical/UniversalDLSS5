#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
static std::string read(const std::filesystem::path&p){std::ifstream f(p,std::ios::binary);std::ostringstream s;s<<f.rdbuf();return s.str();}
static bool need(const std::string&s,const std::string&n,const char*m){if(s.find(n)==std::string::npos){std::cerr<<m<<"\n";return false;}return true;}
int main(){
 const std::filesystem::path root=UDLSS_SOURCE_DIR;
 const auto hooks=read(root/"src/bridge/d3d9_hooks.cpp");
 const auto front=read(root/"src/compat/d3d9_frontend.cpp");
 const auto ring=read(root/"src/compat/legacy_copy_ring.cpp");
 const auto bridge=read(root/"src/bridge/bridge_main.cpp");
 const auto audit=read(root/"tools/audit_no_readback.py");
 const auto cm=read(root/"CMakeLists.txt");
 bool ok=true;
 ok&=need(hooks,"IDirect3DDevice9::Present","D3D9 Present bootstrap/hook marker missing");
 ok&=need(hooks,"kPresentVtableIndex=17","D3D9 Present vtable index missing");
 ok&=need(hooks,"kResetVtableIndex=16","D3D9 Reset vtable index missing");
 ok&=need(hooks,"kPresentExVtableIndex=121","D3D9Ex PresentEx hook missing");
 ok&=need(hooks,"kResetExVtableIndex=132","D3D9Ex ResetEx hook missing");
 ok&=need(front,"D3D9_RESOURCE_MISC","D3D9Ex shared-resource route marker missing");
 ok&=need(front,"RendererRoute::CompatD3D9Classic","classic D3D9 fallback route missing");
 ok&=need(ring,"UDLSS_LEGACY_D3D9_TRANSFER","legacy D3D9 readback is not explicitly isolated");
 ok&=need(ring,"GetRenderTargetData","classic D3D9 full-resolution transfer missing");
 ok&=need(bridge,"ensureD3D9HooksInstalled","D3D9 hook family not installed by bridge");
 ok&=need(audit,"UDLSS_LEGACY_D3D9_TRANSFER","readback audit has no legacy D3D9 exception marker");
 ok&=need(cm,"d3d9_frontend.cpp","D3D9 frontend not in bridge build");
 return ok?0:1;
}
