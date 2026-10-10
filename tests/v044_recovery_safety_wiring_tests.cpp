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
 ok&=forbid(dxgi,"recordRecoveryQueueSubmission(e.owner,q)","recovery still injects fence signals into game queues");
 ok&=forbid(on12,"queue_->Wait(wait.fence.Get(),wait.value)","recovery still injects cross-queue waits that can form dependency cycles");
 ok&=forbid(on12,"WaitForSingleObject(recoveryD11FenceEvent_,5000)","recovery still blocks Present for five seconds on D3D11 completion");
 ok&=need(on12H,"recoveryD11PendingValue_","recovery has no non-fatal pending D3D11 completion state");
 ok&=need(on12,"Recovery D3D11 frame still in flight; bypassing this frame safely","recovery cannot bypass while D3D11 work is still outstanding");
 ok&=forbid(guides,"CopyResource(snapshot.Get(),source)","recovery still mutates game command lists to snapshot temporal guides");
 ok&=need(guidesH,"producerQueueId","captured guides do not remember the provider queue");
 ok&=need(guidesH,"providerCommandListId","captured guides do not remember the provider command list");
 ok&=need(guides,"noteGameTemporalGuideCommandListExecuted","guide capture cannot associate Streamline evaluation command lists with their executing queue");
 ok&=need(on12,"recoveryGuideUsable(","recovery guide admission is not using the owned-snapshot safety policy");
 ok&=forbid(on12,"captured.producerQueueId==recoveryQueueId_","owned snapshots are still hard-bound to the recovery queue");
 ok&=need(on12,"shouldConsumeRecoveryGuide(","recovery capture lifetime does not preserve unresolved game-owned captures");
 ok&=need(guides,"tag.lifecycle==sl::eValidUntilPresent","recovery no longer recognizes Streamline's explicit valid-until-Present lifetime");
 return ok?0:1;
}
