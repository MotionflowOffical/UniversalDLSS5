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
 const auto guides=read(root/"src/gpu/game_temporal_guides.cpp");
 const auto dxgi=read(root/"src/bridge/dxgi_hooks.cpp");
 const auto on12=read(root/"src/gpu/d3d12_on12.cpp");
 bool ok=true;
 ok&=need(guides,"CreateToolhelp32Snapshot","guide hooks do not enumerate loaded modules when preferred DLL names miss the active provider");
 ok&=need(guides,"Module32FirstW","loaded-module guide hook scan is not wired");
 ok&=need(guides,"resolveHookTarget","guide hooks do not resolve short export/jump thunks before falling back from MinHook");
 ok&=need(guides,"gameTemporalGuideHookDiagnostics","guide hook activity cannot be surfaced in diagnostics");
 ok&=need(guides,"g_ngx12HookHits","NGX D3D12 hook hit telemetry is missing");
 ok&=need(guides,"g_streamlineHookHits","Streamline hook hit telemetry is missing");
 ok&=need(guides,"g_acceptedCaptures","accepted game-guide capture telemetry is missing");
 const auto d12=dxgi.find("if(SUCCEEDED(s->GetDevice(IID_PPV_ARGS(&d12)))){");
 const auto safe=dxgi.find("safeAttachReady(c.safeAttach",d12);
 const auto probe=dxgi.find("probeGameGuideHooks(c)",d12);
 if(d12==std::string::npos||safe==std::string::npos||probe==std::string::npos||probe>safe){std::cerr<<"D3D12 guide hooks are still installed only after safe-attach, missing earlier game guide calls\n";ok=false;}
 ok&=need(on12,"gameTemporalGuideHookDiagnostics()","D3D12 diagnostics do not expose provider-hook activity when guides are missing");
 return ok?0:1;
}
