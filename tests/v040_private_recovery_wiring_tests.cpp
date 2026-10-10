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
 const auto on12=read(root/"src/gpu/d3d12_on12.cpp");
 bool ok=true;
 ok&=need(dxgi,"noteQueueEvidence","recovery queue evidence collection is missing");
 ok&=need(dxgi,"presentTransition","recovery no longer requires real PRESENT transition evidence");
 ok&=forbid(dxgi,"recordRecoveryQueueSubmission(e.owner,q)","recovery still injects auxiliary fence signals into game queues");
 ok&=forbid(on12,"queue_->Wait(wait.fence.Get(),wait.value)","recovery still injects cross-queue waits that can form cycles");
 ok&=need(on12,"synchronizeBeforePresent && !waitCopyContext(postCopy_,st)","recovery does not prove its own final post-copy before Present");
 return ok?0:1;
}
