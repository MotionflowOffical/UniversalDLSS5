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
 const auto h=read(root/"src/gpu/d3d12_on12.hpp");
 const auto cpp=read(root/"src/gpu/d3d12_on12.cpp");
 bool ok=true;
 ok&=need(cpp,"D3D12_HEAP_FLAG_SHARED","recovery color transport is not a D3D12 shared resource");
 ok&=need(cpp,"OpenSharedResource1","recovery color is not opened on a standard D3D11 device");
 ok&=need(cpp,"D3D11CreateDevice","recovery path does not create an independent D3D11 device");
 ok&=need(cpp,"waitRecoveryD3D11","recovery path does not prove D3D11 completion before D3D12 reads the shared surface");
 ok&=need(cpp,"setForceExternalHost(recoveryMode_)","recovery path no longer isolates Feature 18 in NRHost");
 ok&=need(h,"recoveryD11Fence_","recovery path has no D3D11 completion fence");
 if(cpp.find("private D3D12 recovery queue")!=std::string::npos){std::cerr<<"recovery still creates/labels a private presentation queue\n";ok=false;}
 return ok?0:1;
}
