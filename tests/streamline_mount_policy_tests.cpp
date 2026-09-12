#include <cassert>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#ifndef UDLSS_SOURCE_DIR
#error UDLSS_SOURCE_DIR required
#endif
static std::string read(const std::filesystem::path&p){std::ifstream f(p);std::ostringstream s;s<<f.rdbuf();return s.str();}
int main(){
 const auto s=read(std::filesystem::path(UDLSS_SOURCE_DIR)/"src/neural/streamline_nr.cpp");
 assert(s.find("sl::RenderAPI::eD3D12")!=std::string::npos);
 assert(s.find("slSetD3DDevice")!=std::string::npos);
 assert(s.find("slGetNewFrameToken")!=std::string::npos);
 assert(s.find("slSetConstants")!=std::string::npos);
 assert(s.find("sl::kFeatureDLSS_NR")!=std::string::npos);
 assert(s.find("sl::kBufferTypeUpliftInputColor")!=std::string::npos);
 assert(s.find("sl::kBufferTypeUpliftOutputColor")!=std::string::npos);
 assert(s.find("sl::kBufferTypeMotionVectors")!=std::string::npos);
 assert(s.find("sl::kBufferTypeDepth")!=std::string::npos);
 assert(s.find("ID3D12GraphicsCommandList")!=std::string::npos);
 assert(s.find("D3D12_RESOURCE_STATE_COMMON")!=std::string::npos);
 assert(s.find("OpenSharedHandle")!=std::string::npos);
 assert(s.find("reinterpret_cast<sl::CommandBuffer*>(slot.list.Get())")!=std::string::npos);
 assert(s.find("reinterpret_cast<sl::CommandBuffer*>(context)")==std::string::npos);
}
