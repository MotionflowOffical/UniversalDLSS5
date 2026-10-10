#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#ifndef UDLSS_SOURCE_DIR
#define UDLSS_SOURCE_DIR "."
#endif

static std::string read(const std::filesystem::path& p){
    std::ifstream f(p,std::ios::binary);std::ostringstream s;s<<f.rdbuf();return s.str();
}
static bool need(const std::string& text,const std::string& token,const char* message){
    if(text.find(token)!=std::string::npos)return true;
    std::cerr<<message<<"\n";return false;
}
static bool forbid(const std::string& text,const std::string& token,const char* message){
    if(text.find(token)==std::string::npos)return true;
    std::cerr<<message<<"\n";return false;
}

int main(){
    const auto root=std::filesystem::path(UDLSS_SOURCE_DIR);
    const auto guides=read(root/"src/gpu/game_temporal_guides.cpp");
    const auto header=read(root/"src/gpu/game_temporal_guides.hpp");
    const auto dxgi=read(root/"src/bridge/dxgi_hooks.cpp");
    bool ok=true;
    ok&=need(guides,"eOnlyValidNow","SnapshotOnly no longer recognizes volatile Streamline lifetimes");
    ok&=need(guides,"eValidUntilEvaluate","SnapshotOnly no longer recognizes evaluate-lifetime Streamline guides");
    ok&=need(guides,"PendingProviderGuides","Provider guides are not represented by the shared pending snapshot metadata");
    ok&=need(guides,"g_pendingProviders","Provider guides are not tracked per command-list identity");
    ok&=need(guides,"shouldBindStreamlineEvaluationCommandList","Present-safe Streamline captures are not rebound to the later evaluation command list");
    ok&=need(guides,"g_capture.providerCommandListId=id","Evaluation-list provenance is not written onto an unbound direct Streamline capture");
    ok&=need(guides,"pendingFromCapturedStreamline","A rebound Present-safe capture is not converted into an owned snapshot candidate");
    ok&=need(header,"pendingGameTemporalGuideSnapshotActive","Execute hook cannot detect a pending volatile guide snapshot");
    ok&=need(header,"snapshotPendingGameTemporalGuidesAfterExecute","No post-submit same-queue snapshot API exists");
    ok&=need(dxgi,"origExecute(q,1,&l[i])","D3D12 recovery does not submit provider lists individually while snapshots are pending");
    ok&=need(dxgi,"snapshotPendingGameTemporalGuidesAfterExecute(q,l[i],origExecute)","D3D12 ExecuteCommandLists does not append the guide snapshot after the provider list");
    ok&=need(dxgi,"pendingGameTemporalGuideSnapshotActive()","Execute submissions are not serialized while a volatile guide snapshot is pending");
    ok&=need(guides,"(post-submit owned snapshot)","Post-submit snapshots are not published as injector-owned guides");
    ok&=forbid(guides,"snapshotVolatileD3D12Guide(","Recovery regressed to mutating the game's Streamline command list");
    ok&=forbid(dxgi,"->Wait(","Recovery guide snapshot reintroduced a game-queue wait");
    return ok?0:1;
}
