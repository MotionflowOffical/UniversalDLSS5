#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#ifndef UDLSS_SOURCE_ROOT
#error UDLSS_SOURCE_ROOT required
#endif
static std::string read(const std::filesystem::path&p){std::ifstream f(p);std::ostringstream s;s<<f.rdbuf();return s.str();}
int main(){
 const auto c=read(std::filesystem::path(UDLSS_SOURCE_ROOT)/"src/gpu/d3d12_on12.cpp");
 bool ok=true;
 if(c.find("captured.validUntilPresent")==std::string::npos){std::cerr<<"D3D12 captured guides are not gated by Present lifetime\n";ok=false;}
 if(c.find("s.motionSource==MotionSource::Auto")==std::string::npos){std::cerr<<"D3D12 game motion is touched even when native motion is not requested\n";ok=false;}
 if(c.find("observed, lifetime not Present-safe")==std::string::npos){std::cerr<<"Unsafe transient guide diagnostics are missing\n";ok=false;}
 return ok?0:1;
}
