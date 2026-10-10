#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <filesystem>
#ifndef UDLSS_SOURCE_DIR
#error UDLSS_SOURCE_DIR is required
#endif
static std::string read(const std::filesystem::path& p){std::ifstream f(p,std::ios::binary);std::ostringstream s;s<<f.rdbuf();return s.str();}
static bool need(const std::string&s,const char*t,const char*why){if(s.find(t)!=std::string::npos)return true;std::cerr<<why<<"\n";return false;}
int main(){
 const auto root=std::filesystem::path(UDLSS_SOURCE_DIR);
 const auto dxgi=read(root/"src/bridge/dxgi_hooks.cpp");
 bool ok=true;
 ok&=need(dxgi,"udlss/present_processing_policy.hpp","Present mutation policy is not included in DXGI hooks");
 ok&=need(dxgi,"DXGI_PRESENT_TEST","DXGI_PRESENT_TEST is not filtered before backbuffer mutation");
 ok&=need(dxgi,"DXGI_PRESENT_DO_NOT_WAIT","DXGI_PRESENT_DO_NOT_WAIT is not filtered before backbuffer mutation");
 ok&=need(dxgi,"shouldProcessPresentBeforeMutation","Present hook does not gate processing on consumption safety");
 ok&=need(dxgi,"presentMutationSuppressed","Present hook does not carry a resynchronization suppression state");
 ok&=need(dxgi,"resetPipelinesForPresentResync","Present resynchronization does not reset stale neural/history surfaces");
 return ok?0:1;
}
