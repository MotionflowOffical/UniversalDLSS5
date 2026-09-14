#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
static std::string read(const std::filesystem::path&p){std::ifstream f(p,std::ios::binary);std::ostringstream s;s<<f.rdbuf();return s.str();}
static bool need(const std::string&s,const std::string&n,const char*m){if(s.find(n)==std::string::npos){std::cerr<<m<<"\n";return false;}return true;}
int main(){
 const std::filesystem::path root=UDLSS_SOURCE_DIR;
 const auto hooks=read(root/"src/bridge/opengl_hooks.cpp");
 const auto front=read(root/"src/compat/opengl_frontend.cpp");
 const auto bridge=read(root/"src/bridge/bridge_main.cpp");
 const auto cm=read(root/"CMakeLists.txt");
 const auto audit=read(root/"tools/audit_no_readback.py");
 bool ok=true;
 ok&=need(hooks,"SwapBuffers","OpenGL SwapBuffers hook missing");
 ok&=need(hooks,"wglMakeCurrent","WGL context tracking missing");
 ok&=need(hooks,"HookCallScope","OpenGL hooks are not quiescence scoped");
 ok&=need(front,"wglDXOpenDeviceNV","WGL_NV_DX_interop2 device path missing");
 ok&=need(front,"wglDXRegisterObjectNV","WGL/D3D object registration missing");
 ok&=need(front,"WglNvDxInterop","OpenGL preferred interop diagnostics missing");
 ok&=need(front,"OpenGlPboTransfer","OpenGL PBO fallback diagnostics missing");
 ok&=need(front,"UDLSS_OPENGL_PBO_TRANSFER","OpenGL CPU fallback is not isolated/marked");
 ok&=need(bridge,"installOpenGLHooks","OpenGL hooks not installed by bridge");
 ok&=need(cm,"opengl_frontend.cpp","OpenGL frontend not built");
 ok&=need(audit,"UDLSS_OPENGL_PBO_TRANSFER","audit does not isolate OpenGL PBO fallback");
 return ok?0:1;
}
