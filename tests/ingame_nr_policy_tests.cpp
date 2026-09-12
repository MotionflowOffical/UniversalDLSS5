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
 const auto ingame=read(root/"src/neural/ingame_nr.cpp");
 const auto ngx=read(root/"src/neural/ngx_nr.cpp");
 assert(ingame.find("createInGameNR")!=std::string::npos);
 assert(ingame.find("createStreamlineNR")!=std::string::npos);
 assert(ingame.find("createNgxNR")!=std::string::npos);
 assert(ingame.find("createExternalHostNR")==std::string::npos);
 assert(ngx.find("nvngx.dll_UniversalDLSS5_NRForwarder.dll")!=std::string::npos);
 assert(ngx.find("UdlssNrFwdInitExt")!=std::string::npos);
 assert(ngx.find("UdlssNrFwdCreate")!=std::string::npos);
 assert(ngx.find("UdlssNrFwdEvaluate")!=std::string::npos);
}
