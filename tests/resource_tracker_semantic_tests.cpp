#include <cassert>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#ifndef UDLSS_SOURCE_DIR
#error UDLSS_SOURCE_DIR required
#endif
static std::string read(const std::filesystem::path& p){std::ifstream f(p);std::ostringstream s;s<<f.rdbuf();return s.str();}
int main(){
    const std::filesystem::path root=UDLSS_SOURCE_DIR;
    const auto h=read(root/"src/gpu/d3d11_resource_tracker.hpp");
    const auto c=read(root/"src/gpu/d3d11_resource_tracker.cpp");
    assert(h.find("onShaderCreated")!=std::string::npos);
    assert(h.find("onShaderBound")!=std::string::npos);
    assert(c.find("D3DReflect")!=std::string::npos);
    assert(c.find("cameramotionvectors")!=std::string::npos);
    assert(c.find("velocity")!=std::string::npos);
    assert(c.find("semanticMotion=true")!=std::string::npos || c.find("semanticMotion = true")!=std::string::npos);
    assert(c.find("UnityUvPreviousToCurrent")!=std::string::npos);
    assert(c.find("CreatePixelShader")!=std::string::npos || c.find("hkCreatePS")!=std::string::npos);
    assert(c.find("CreateComputeShader")!=std::string::npos || c.find("hkCreateCS")!=std::string::npos);
    assert(c.find("PSSetShader")!=std::string::npos || c.find("hkPSShader")!=std::string::npos);
    assert(c.find("CSSetShader")!=std::string::npos || c.find("hkCSShader")!=std::string::npos);
    assert(c.find("onShaderResources(c,D3D11ResourceTracker::ShaderStage::Pixel,s,n,r)")!=std::string::npos);
    assert(c.find("onShaderResources(c,D3D11ResourceTracker::ShaderStage::Compute,s,n,r)")!=std::string::npos);
}
