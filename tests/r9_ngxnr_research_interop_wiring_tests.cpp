#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#ifndef UDLSS_SOURCE_DIR
#error UDLSS_SOURCE_DIR required
#endif
static std::string read(const std::filesystem::path& p) {
    std::ifstream f(p,std::ios::binary);std::ostringstream s;s<<f.rdbuf();return s.str();
}
int main() {
    const auto root=std::filesystem::path(UDLSS_SOURCE_DIR);
    const auto src=read(root/"src/gpu/d3d12_on12.cpp");
    const auto hdr=read(root/"src/gpu/d3d12_on12.hpp");
    const auto flags=read(root/"src/gpu/shared_guide_texture.hpp");
    bool ok=true;
    auto need=[&](const std::string&s,const char*token){if(s.find(token)==std::string::npos){std::cerr<<token<<" missing\n";ok=false;}};
    need(src,"for(unsigned candidate=0;candidate<4");
    need(src,"sharedGuideTextureFlags(format,candidate%2!=0)");
    need(flags,"D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS");
    need(src,"OpenSharedResource1(shared");
    need(src,"surface.failureOperation=3");
    need(src,"surface.candidateAttempts");
    need(src,"stage:D f%u hr=%08X");
    need(hdr,"DXGI_FORMAT triedFormat");
    return ok?0:1;
}
