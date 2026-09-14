#include <fstream>
#include <sstream>
#include <string>
#include <iostream>
#ifndef UDLSS_SOURCE_DIR
#define UDLSS_SOURCE_DIR "."
#endif
static std::string read(const char* rel){std::ifstream f(std::string(UDLSS_SOURCE_DIR)+"/"+rel);std::ostringstream s;s<<f.rdbuf();return s.str();}
static bool need(const std::string& s,const char* n,const char* m){if(s.find(n)!=std::string::npos)return true;std::cerr<<m<<"\n";return false;}
int main(){
    const auto h=read("src/bridge/d3d9_hooks.hpp");
    const auto c=read("src/bridge/d3d9_hooks.cpp");
    const auto m=read("src/bridge/bridge_main.cpp");
    bool ok=true;
    ok&=need(h,"ensureD3D9HooksInstalled","D3D9 hook API has no idempotent late-install entry point");
    ok&=need(c,"d3d9HooksInstalled","D3D9 hook installation is not tracked/idempotent");
    ok&=need(m,"ensureD3D9HooksInstalled","Bridge loop never retries D3D9 hook installation after initial attach");
    return ok?0:1;
}
