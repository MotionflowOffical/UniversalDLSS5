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
 const auto dxgi=read(root/"src/bridge/dxgi_hooks.cpp");
 bool ok=true;
 ok&=need(guidesH,"SnapshotOnly","recovery guide isolation mode is missing");
 ok&=need(guides,"tag.lifecycle==sl::eValidUntilPresent","Streamline Present-safe resources are no longer recognized");
 ok&=need(guides,"tag.lifecycle==sl::eOnlyValidNow || tag.lifecycle==sl::eValidUntilEvaluate","volatile Streamline lifetimes are no longer recognized for safe post-submit snapshots");
 ok&=need(guidesH,"snapshotPendingGameTemporalGuidesAfterExecute","volatile guide capture has no post-submit snapshot API");
 ok&=need(dxgi,"snapshotPendingGameTemporalGuidesAfterExecute(q,l[i],origExecute)","volatile guides are not snapshotted immediately after the provider command list submission");
 ok&=forbid(guides,"CopyResource(snapshot.Get(),source)","recovery still modifies the game's Streamline command list to snapshot volatile guides");
 ok&=forbid(guides,"snapshotVolatileD3D12Guide","unsafe volatile command-list snapshot helper is still present");
 ok&=need(guidesH,"providerCommandListId","guide capture lacks command-list provenance");
 ok&=need(guidesH,"producerQueueId","guide capture lacks queue provenance");
 return ok?0:1;
}
