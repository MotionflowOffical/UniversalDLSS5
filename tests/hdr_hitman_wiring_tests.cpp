#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

static std::string readFile(const std::filesystem::path& p){
    std::ifstream in(p,std::ios::binary);
    std::ostringstream ss; ss<<in.rdbuf(); return ss.str();
}
static bool need(const std::string& hay,const char* needle,const char* what){
    if(hay.find(needle)==std::string::npos){std::cerr<<"missing "<<what<<": "<<needle<<"\n";return false;}return true;
}
int main(){
    const auto root=std::filesystem::path(UDLSS_SOURCE_ROOT);
    const auto dxgi=readFile(root/"src/bridge/dxgi_hooks.cpp");
    const auto color=readFile(root/"src/gpu/swapchain_color.hpp");
    const auto post=readFile(root/"shaders/post.hlsl");
    const auto conv=readFile(root/"shaders/convert.hlsl");
    const auto nr=readFile(root/"src/neural/ingame_nr.cpp");
    const auto driverPolicy=readFile(root/"include/udlss/nvidia_driver_policy.hpp");
    const auto ui=readFile(root/"src/controller/ui.cpp");
    bool ok=true;
    if(color.find("->GetColorSpace1(")!=std::string::npos){std::cerr<<"invalid IDXGISwapChain3::GetColorSpace1 call remains\n";ok=false;}
    ok&=need(dxgi,"trackedColorSpace","tracked SetColorSpace1 state");
    ok&=need(dxgi,"colorSpaceKnown","tracked color-space validity");
    ok&=need(color,"GetDesc1","IDXGIOutput6 fallback color-space inference");
    ok&=need(dxgi,"hookSetColorSpace","SetColorSpace1 transition hook");
    ok&=need(dxgi,"hookSetHdrMeta","HDR metadata transition hook");
    ok&=need(dxgi,"GetDeviceRemovedReason","device-removal diagnostics");
    ok&=need(conv,"PqToNits","HDR10 PQ decode");
    ok&=need(post,"NitsToPq","HDR10 PQ encode");
    ok&=need(post,"HdrPaperWhite","HDR proxy reconstruction");
    ok&=need(driverPolicy,"knownDirectFeature18CrashRisk","known-risk NVIDIA driver guard");
    ok&=need(nr,"directFeature18Risk","driver-risk enforcement in in-game backend");
    ok&=need(ui,"CH_HDR","HDR mode UI control");
    return ok?0:1;
}
