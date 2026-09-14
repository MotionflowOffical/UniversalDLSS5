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
 const auto dxgi=read(root/"src/bridge/dxgi_hooks.cpp");
 const auto guidesH=read(root/"src/gpu/game_temporal_guides.hpp");
 const auto guides=read(root/"src/gpu/game_temporal_guides.cpp");
 const auto trackerH=read(root/"src/gpu/d3d12_resource_tracker.hpp");
 const auto tracker=read(root/"src/gpu/d3d12_resource_tracker.cpp");
 bool ok=true;
 ok&=need(dxgi,"const bool retainGameTemporalResources=provenQueue&&!recoveryMode","D3D12 guide retention is re-enabled before an exact/native route is proven");
 ok&=need(dxgi,"GameTemporalGuideCaptureMode::SnapshotOnly","D3D12 recovery does not isolate volatile temporal guides through injector-owned snapshots");
 ok&=need(dxgi,"setD3D12SemanticResourceTrackingEnabled(retainGameTemporalResources)","D3D12 recovery does not disable semantic game-resource retention");
 ok&=need(dxgi,"probeGameGuideHooks(c)","D3D12 recovery cannot observe Present-safe Streamline tags");
 ok&=need(guidesH,"GameTemporalGuideCaptureMode", "game-guide capture has no lifetime-isolation mode");
 ok&=need(guides,"SnapshotOnly", "game-guide capture does not distinguish snapshot-only recovery capture");
 ok&=need(guides,"g_capture={}", "disabling game-guide capture does not release retained game resources");
 ok&=need(trackerH,"setD3D12SemanticResourceTrackingEnabled", "D3D12 tracker has no queue-only recovery mode");
 ok&=need(tracker,"semanticResourceTrackingEnabled", "D3D12 semantic resource retention is not independently gated");
 ok&=need(tracker,"g.entries.clear()", "disabling D3D12 semantic tracking does not release retained game resources");
 return ok?0:1;
}
