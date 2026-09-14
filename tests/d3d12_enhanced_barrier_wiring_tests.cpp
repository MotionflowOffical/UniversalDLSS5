#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
static std::string read(const std::filesystem::path& p){std::ifstream f(p,std::ios::binary);std::ostringstream s;s<<f.rdbuf();return s.str();}
static bool need(const std::string&s,const std::string&n,const char*m){if(s.find(n)==std::string::npos){std::cerr<<m<<"\n";return false;}return true;}
int main(){
    const std::filesystem::path root=UDLSS_SOURCE_DIR;
    const auto h=read(root/"src/gpu/d3d12_resource_tracker.hpp");
    const auto cpp=read(root/"src/gpu/d3d12_resource_tracker.cpp");
    const auto dxgi=read(root/"src/bridge/dxgi_hooks.cpp");
    bool ok=true;
    ok&=need(h,"onEnhancedBarriers","D3D12 tracker does not expose enhanced-barrier observation");
    ok&=need(cpp,"ID3D12GraphicsCommandList7","CommandList7 capability probe is missing");
    ok&=need(cpp,"kCommandList7BarrierVtableIndex=80","Enhanced Barrier vtable slot is not explicitly guarded");
    ok&=need(cpp,"D3D12_BARRIER_GROUP","Enhanced barrier groups are not inspected");
    ok&=need(cpp,"D3D12_BARRIER_TYPE_TEXTURE","Texture barrier resource touches are missing");
    ok&=need(dxgi,"ModernD3D12Recovery","DXGI does not expose modern D3D12 recovery route");
    ok&=need(dxgi,"D3D12RecoveryEvidence","DXGI does not aggregate recovery evidence");
    ok&=need(dxgi,"queueProofConfidence","Queue proof confidence is not published");
    return ok?0:1;
}
