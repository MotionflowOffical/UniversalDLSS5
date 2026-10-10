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
static bool forbid(const std::string&s,const std::string&n,const char*m){if(s.find(n)!=std::string::npos){std::cerr<<m<<"\n";return false;}return true;}
int main(){
 const std::filesystem::path root=UDLSS_SOURCE_DIR;
 const auto guidesH=read(root/"src/gpu/game_temporal_guides.hpp");
 const auto guides=read(root/"src/gpu/game_temporal_guides.cpp");
 const auto on12=read(root/"src/gpu/d3d12_on12.cpp");
 const auto dxgi=read(root/"src/bridge/dxgi_hooks.cpp");
 bool ok=true;
 ok&=need(guidesH,"captureSequence","Captured guides have no generation identity");
 ok&=need(guidesH,"consumeCapturedGameTemporalGuides","No generation-aware guide consume API exists");
 ok&=need(guides,"shouldConsumeGuideGeneration","Guide consume implementation is not generation-aware");
 ok&=need(on12,"consumeCapturedGameTemporalGuides(captured.captureSequence)","Recovery does not consume only the guide generation it actually staged");
 ok&=forbid(dxgi,"if(recoveryMode)gpu::releaseCapturedGameTemporalGuides();","Recovery still blindly clears all captures after every Present");
 return ok?0:1;
}
