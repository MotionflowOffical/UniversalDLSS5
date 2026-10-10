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
 const auto guides=read(root/"src/gpu/game_temporal_guides.cpp");
 bool ok=true;
 ok&=need(guides,"captureNgx12(ID3D12GraphicsCommandList* list","NGX D3D12 observation hook is missing");
 ok&=forbid(guides,"snapshotVolatileD3D12Guide(depth","NGX depth still mutates a game command list");
 ok&=forbid(guides,"snapshotVolatileD3D12Guide(motion","NGX motion still mutates a game command list");
 ok&=need(guides,"GameGuideSource::Ngx","NGX recovery snapshots are not represented as provider-owned captures");
 ok&=need(guides,"queuePendingProviderGuides","NGX recovery inputs are not queued for post-submit owned snapshots");
 ok&=forbid(guides,"if(mode!=GameTemporalGuideCaptureMode::Full)return","NGX recovery capture is still disabled outside Full mode");
 return ok?0:1;
}
