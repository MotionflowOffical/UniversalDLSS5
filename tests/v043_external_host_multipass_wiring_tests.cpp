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
 const auto proto=read(root/"src/neural/external_host_protocol.hpp");
 const auto bridge=read(root/"src/neural/external_host.cpp");
 const auto host=read(root/"src/host/main.cpp");
 bool ok=true;
 ok&=need(proto,"kAbi=6","external-host IPC ABI was not bumped for multipass fields");
 ok&=need(proto,"nrPassesRequested","external-host protocol does not carry requested NR pass count");
 ok&=need(proto,"nrPassesExecuted","external-host protocol does not publish executed NR pass count");
 ok&=need(bridge,"shared_->nrPassesRequested","bridge does not send the selected NR pass count to NRHost");
 ok&=need(bridge,"st.neuralPassesExecuted=shared_->nrPassesExecuted","bridge does not publish host executed-pass diagnostics");
 ok&=need(host,"refinementFeature","NRHost has no separate reset-only refinement feature");
 ok&=need(host,"refinementScratch","NRHost has no ping-pong refinement scratch resource");
 ok&=need(host,"ensureRefinementFeature","NRHost does not lazily create the refinement feature");
 ok&=need(host,"for(std::uint32_t pass=2;pass<=requestedPasses;++pass)","NRHost does not execute same-frame refinement passes");
 ok&=need(host,"shared->nrPassesExecuted=executedPasses","NRHost does not report actual executed pass count");
 return ok?0:1;
}
