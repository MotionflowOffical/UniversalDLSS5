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
 const auto on12=read(root/"src/gpu/d3d12_on12.cpp");
 const auto extractor=read(root/"src/gpu/d3d11_guide_extractor.cpp");
 const auto motionShader=read(root/"shaders/native_motion_convert.hlsl");
 const auto host=read(root/"src/host/main.cpp");
 const auto ext=read(root/"src/neural/external_host.cpp");
 const auto extPolicy=read(root/"include/udlss/external_host_policy.hpp");
 bool ok=true;
 ok&=need(guides,"DXGI_FORMAT_R16G16B16A16_FLOAT","Cyberpunk RGBA16F velocity snapshots are not admitted");
 ok&=need(extractor,"DXGI_FORMAT_R16G16B16A16_FLOAT","Cyberpunk RGBA16F motion is still rejected by the D3D11 extractor");
 ok&=need(motionShader,"Texture2D<float4> NativeMotion","native motion shader does not accept RGBA velocity textures");
 ok&=need(motionShader,".xy","native motion shader does not explicitly consume the velocity xy channels");
 ok&=need(guides,"return DXGI_FORMAT_R32_FLOAT","R32 Cyberpunk depth snapshots are not normalized to a typed share-friendly format");
 ok&=need(on12,"d.Format=format","recovery staging still creates engine-style typeless resources instead of typed interop resources");
 ok&=need(on12,"CreateSharedHandle(probe12.Get()","typed recovery guide staging is not exported to D3D11");
 ok&=need(on12,"lastInteropHr","guide-staging HRESULT diagnostics are missing");
 ok&=need(on12,"interopPath","guide-staging path diagnostics are missing");
 ok&=forbid(on12,"createRecoveryInteropSurface","invalid D3D11-created reverse interop path is still present");
 ok&=need(host,"D3D12_COMMAND_QUEUE_PRIORITY_HIGH","NRHost does not request a high-priority GPU queue under foreground load");
 ok&=need(host,"ABOVE_NORMAL_PRIORITY_CLASS","NRHost process is not protected from foreground scheduling starvation");
 ok&=need(ext,"waitForExactOutput(shared_,fence11_.Get(),outputFenceEvent_,hostProcess_,pendingSeq_","Synchronized mode does not drain an outstanding exact neural frame");
 ok&=need(extPolicy,"FramePacingMode::Synchronized: return 250","Synchronized host budget is still too short for foreground GPU scheduling shifts");
 return ok?0:1;
}
