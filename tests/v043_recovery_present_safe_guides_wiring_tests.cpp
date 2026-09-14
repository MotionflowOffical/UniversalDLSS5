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
 const auto dxgi=read(root/"src/bridge/dxgi_hooks.cpp");
 const auto guidesH=read(root/"src/gpu/game_temporal_guides.hpp");
 const auto guides=read(root/"src/gpu/game_temporal_guides.cpp");
 const auto on12H=read(root/"src/gpu/d3d12_on12.hpp");
 const auto on12=read(root/"src/gpu/d3d12_on12.cpp");
 bool ok=true;
 ok&=need(guidesH,"GameTemporalGuideCaptureMode","recovery-safe temporal-guide capture mode is missing");
 ok&=need(guidesH,"PresentSafeOnly","Present-safe-only guide capture mode is missing");
 ok&=need(guides,"GameGuideSource::Streamline","recovery capture is not limited to a provider with an explicit Present lifetime contract");
 ok&=need(guides,"incoming.validUntilPresent","recovery capture does not require explicit valid-until-Present lifetime");
 ok&=need(dxgi,"GameTemporalGuideCaptureMode::SnapshotOnly","D3D12 recovery does not select snapshot-isolated guide capture");
 ok&=need(dxgi,"probeGameGuideHooks(c)","D3D12 recovery never installs the safe Streamline tag hooks");
 ok&=need(dxgi,"setD3D12SemanticResourceTrackingEnabled(retainGameTemporalResources)","heuristic D3D12 resource retention isolation was removed");
 ok&=need(on12H,"RecoveryGuideSurface","injector-owned recovery guide staging surface is missing");
 ok&=need(on12,"stageRecoveryGuide","Present-safe game guides are not copied into injector-owned resources");
 ok&=need(on12,"captured.validUntilPresent||captured.snapshotOwned","staging does not recognize provider-safe or injector-owned guide lifetimes");
 ok&=need(dxgi,"if(recoveryMode)gpu::releaseCapturedGameTemporalGuides()","recovery does not release captured game-resource references before Present");
 ok&=need(on12,"&externalGuide","recovery neural processing does not receive the staged native guides");
 ok&=forbid(on12,"capturedPresentSafe&&!recoveryMode_","Present-safe captured guides are still hard-disabled in recovery mode");
 return ok?0:1;
}
