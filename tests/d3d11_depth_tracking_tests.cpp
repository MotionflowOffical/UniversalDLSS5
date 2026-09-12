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
 const std::filesystem::path root=UDLSS_SOURCE_DIR;
 const auto h=read(root/"src/gpu/d3d11_resource_tracker.hpp");
 const auto c=read(root/"src/gpu/d3d11_resource_tracker.cpp");
 const auto e=read(root/"src/gpu/d3d11_guide_extractor.cpp");
 assert(h.find("TrackedDepthCandidate")!=std::string::npos);
 assert(h.find("bestDepthCandidate")!=std::string::npos);
 assert(h.find("onClearDepthStencil")!=std::string::npos);
 assert(c.find("ClearDepthStencilView")!=std::string::npos);
 assert(c.find("v[53]")!=std::string::npos);
 assert(c.find("depthDraws")!=std::string::npos);
 assert(c.find("clearDepth")!=std::string::npos);
 assert(e.find("bestDepthCandidate")!=std::string::npos);
 assert(e.find("clearDepth")!=std::string::npos);
 assert(e.find("Tracked dominant depth-stencil")!=std::string::npos);
}
