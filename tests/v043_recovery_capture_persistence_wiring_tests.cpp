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
 const auto guides=read(root/"src/gpu/game_temporal_guides.cpp");
 const auto on12=read(root/"src/gpu/d3d12_on12.cpp");
 bool ok=true;
 ok&=need(guides,"incoming.source!=GameGuideSource::Streamline || !incoming.validUntilPresent","SnapshotOnly does not restrict recovery to Streamline Present-safe guides");
 ok&=need(guides,"captureSequence=g_nextCaptureSequence++","accepted captures do not receive generation identities");
 ok&=need(on12,"consumeCapturedGameTemporalGuides(captured.captureSequence)","recovery no longer consumes only the exact generation it staged");
 ok&=forbid(guides,"CopyResource(snapshot.Get(),source)","capture persistence still depends on command-list mutation snapshots");
 return ok?0:1;
}
