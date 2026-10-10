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
 const auto guides=read(root/"src/gpu/game_temporal_guides.cpp");
 const auto on12=read(root/"src/gpu/d3d12_on12.cpp");
 bool ok=true;
 ok&=need(guides,"D3D12_HEAP_FLAG_SHARED","post-submit owned snapshots are not created shareable");
 ok&=need(on12,"openOwnedSnapshotDirect","recovery does not directly open completed owned snapshots on D3D11");
 ok&=need(on12,"providerGuideExtentCompatible","recovery still applies heuristic-resolution limits to explicit provider guides");
 ok&=need(on12,"stage:D","recovery diagnostics do not identify owned-snapshot staging failures");
 return ok?0:1;
}
