#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#ifndef UDLSS_SOURCE_DIR
#error UDLSS_SOURCE_DIR is required
#endif
static std::string read(const std::filesystem::path& p){std::ifstream f(p,std::ios::binary);std::ostringstream s;s<<f.rdbuf();return s.str();}
static bool need(const std::string&s,const char*t,const char*why){if(s.find(t)!=std::string::npos)return true;std::cerr<<why<<"\n";return false;}
static bool forbid(const std::string&s,const char*t,const char*why){if(s.find(t)==std::string::npos)return true;std::cerr<<why<<"\n";return false;}
int main(){
 const auto root=std::filesystem::path(UDLSS_SOURCE_DIR);
 const auto guides=read(root/"src/gpu/game_temporal_guides.cpp");
 const auto extractor=read(root/"src/gpu/d3d11_guide_extractor.cpp");
 const auto host=read(root/"src/host/main.cpp");
 const auto pipe=read(root/"src/gpu/d3d11_pipeline.cpp");
 const auto on12=read(root/"src/gpu/d3d12_on12.cpp");
 const auto policy=read(root/"include/udlss/provider_snapshot_policy.hpp");
 const auto shared=read(root/"include/udlss/shared_control.hpp");
 const auto ui=read(root/"src/controller/ui.cpp");
 bool ok=true;
 ok&=need(guides,"g_slConstantsByViewport","Streamline constants are still global instead of viewport-scoped");
 ok&=need(guides,"kBufferTypeScalingInputColor","render-input extent is not captured for DLSS motion-vector scale");
 ok&=need(guides,"providerViewportId","provider generations are not viewport-isolated");
 ok&=need(guides,"providerSnapshotResourceFormat","owned provider snapshots are not normalized for D3D11 sharing");
 ok&=need(guides,"preferred.Flags=D3D12_RESOURCE_FLAG_NONE","normalized snapshots retain engine-only depth/UAV flags");
 ok&=need(extractor,"explicitProvider?providerGuideExtentCompatible","provider depth is still re-rejected by the heuristic 45% extent gate");
 ok&=need(host,"DLSSNR.ColorSubrectBaseX","external host still omits Feature-18 color subrect base parameters");
 ok&=need(host,"DLSSNR.OutputSubrectBaseX","external host still omits Feature-18 output subrect base parameters");
 ok&=need(host,"DLSSNR.MVecSubrectBaseX","external host still omits Feature-18 motion subrect base parameters");
 ok&=need(host,"DLSSNR.DepthSubrectBaseX","external host still omits Feature-18 depth subrect base parameters");
 ok&=need(policy,"if(snapshotOwned) return false","owned rendered-frame guides are still consumed after one Present");
 ok&=need(guides,"markSnapshotCaptureConsumedLocked(out.captureSequence);g_capture={};","expired persistent snapshots do not retire their ring slots");
 ok&=need(pipe,"st.appliedDebugView=(std::uint32_t)settings.debugView","bridge does not report the actually applied debug mode");
 ok&=need(on12,"st.recoveryPostCopyCommitted=1","recovery does not prove the final D3D12 copy completed before Present");
 ok&=need(shared,"kControlAbi = 21","new output-proof status fields did not bump shared-control ABI");
 ok&=need(ui,"Recovery composite committed before Present","controller does not expose recovery output proof");
 return ok?0:1;
}
