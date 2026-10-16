#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

static std::string readFile(const std::string& path){
    std::ifstream f(path,std::ios::binary);std::ostringstream s;s<<f.rdbuf();return s.str();
}
static bool require(const std::string& s,const std::string& token,const char* why){
    if(s.find(token)!=std::string::npos)return true;
    std::cerr<<why<<" (missing: "<<token<<")\n";return false;
}
static bool forbid(const std::string& s,const std::string& token,const char* why){
    if(s.find(token)==std::string::npos)return true;
    std::cerr<<why<<" (found: "<<token<<")\n";return false;
}
int main(){
    const std::string root=UDLSS_SOURCE_DIR;
    const auto host=readFile(root+"/src/neural/external_host.cpp")+readFile(root+"/src/neural/external_host_wait.hpp");
    const auto guides=readFile(root+"/src/gpu/game_temporal_guides.cpp");
    const auto dxgi=readFile(root+"/src/bridge/dxgi_hooks.cpp");
    bool ok=true;
    ok&=require(host,"nrHostOutputReady(","External host must validate exact frame completion before exposing output");
    ok&=require(host,"SetEventOnCompletion(outputValue", "External host must wait on the output fence before compositing");
    ok&=forbid(host,"ctx11v4_->Wait(fence11_.Get(),outputValue)","A queued GPU wait can let stale external-host output enter the later composite");
    ok&=require(guides,"g_slSetTagForFrameCalls", "Streamline setTagForFrame needs its own counter");
    ok&=require(guides,"g_slSetTagCalls", "Streamline legacy setTag needs its own counter");
    ok&=require(guides,"g_slSetConstantsCalls", "Streamline setConstants needs its own counter");
    ok&=require(guides,"g_slEvaluateCalls", "Streamline evaluate needs its own counter");
    ok&=require(guides,"g_slDepthTags", "Streamline depth-tag count is required");
    ok&=require(guides,"g_slMotionTags", "Streamline motion-tag count is required");
    ok&=require(guides,"kMaxSlHookTargets=8", "Streamline hooks need enough independent forwarding targets for plugin-heavy games");
    ok&=require(guides,"g_slSetTagForFrameTargets", "Frame-tag hook targets must be tracked independently");
    ok&=require(guides,"slGetPluginFunction", "Streamline plugin-gateway exports must be probed for frame-based resource tags");
    ok&=require(guides,"resolveStreamlineFunction", "Streamline callbacks must resolve through direct exports or the plugin gateway");
    ok&=require(guides,"tagF=", "Guide diagnostics must expose frame-tag calls first");
    ok&=require(dxgi,"QueryPerformanceCounter", "Present FPS must use a high-resolution timer");
    ok&=require(dxgi,"PresentCallDepthGuard", "Nested Present/Present1 dispatch must not double-count displayed frames");
    ok&=require(dxgi,"countSuccessfulPresent(s,flags,hr,presentDispatch.outermost)", "Only the outer DXGI Present dispatch may advance displayed-frame FPS accounting");
    ok&=forbid(dxgi,"++c.frames;updateFps(c)", "process() must not count an unconfirmed Present as a displayed frame");
    return ok?0:1;
}
