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
 const auto h=read(root/"src/gpu/game_temporal_guides.hpp");
 const auto cpp=read(root/"src/gpu/game_temporal_guides.cpp");
 const auto on12=read(root/"src/gpu/d3d12_on12.cpp");
 bool ok=true;
 ok&=need(h,"SnapshotOnly","recovery snapshot-only guide mode is missing");
 ok&=need(h,"snapshotOwned","captured guide does not record injector-owned snapshot lifetime");
 ok&=need(dxgi,"GameTemporalGuideCaptureMode::SnapshotOnly","D3D12 recovery does not select snapshot-only guide capture");
 ok&=need(cpp,"snapshotVolatileD3D12Guide","volatile Streamline resources are not snapshotted while valid");
 ok&=need(cpp,"reinterpret_cast<ID3D12GraphicsCommandList*>(cmd)","Streamline command buffer is not used for an in-order D3D12 snapshot");
 ok&=need(cpp,"CopyResource(snapshot.Get(),source)","volatile guide snapshot copy is missing");
 ok&=need(cpp,"g_snapshotLifetime","snapshot resources are not retained through Present processing");
 ok&=need(cpp,"GameTemporalGuideCaptureMode::SnapshotOnly && !incoming.snapshotOwned","snapshot-only mode can still retain a game-owned resource");
 ok&=need(cpp,"tag.lifecycle==sl::eOnlyValidNow","eOnlyValidNow Streamline resources are not accepted by snapshot mode");
 ok&=need(cpp,"tag.lifecycle==sl::eValidUntilEvaluate","eValidUntilEvaluate Streamline resources are not accepted by snapshot mode");
 ok&=need(on12,"captured.snapshotOwned","recovery does not recognize injector-owned volatile-guide snapshots");
 return ok?0:1;
}
