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
 const auto cm=read(root/"CMakeLists.txt"), bw=read(root/"BUILD_WINDOWS.bat"), br=read(root/"BUILD_RELEASE.bat"), ui=read(root/"src/controller/ui.cpp"), rc=read(root/"resources/UniversalDLSS5.rc"), host=read(root/"src/host/main.cpp"), ngx=read(root/"src/neural/ngx_nr.cpp"), sl=read(root/"src/neural/streamline_nr.cpp"), ver=read(root/"include/udlss/version.hpp"), log=read(root/"src/bridge/attach_logger.cpp"), readme=read(root/"README.md"), runtime=read(root/"runtime/README.txt"), overwrite=read(root/"OVERWRITE_INSTALL.txt"), release=read(root/"docs/V0.4.3_RELEASE.md");
 bool ok=true;
 ok&=need(cm,"project(UniversalDLSS5 VERSION 0.4.3","CMake version is not v0.4.3");
 ok&=need(bw,"UniversalDLSS5 v0.4.3","Windows build banner is stale");
 ok&=need(br,"UniversalDLSS5 v0.4.3","release build banner is stale");
 ok&=need(ui,"v0.4.3","controller UI version is stale");
 ok&=need(rc,"FILEVERSION 0,4,3,0","Windows numeric file version is stale");
 ok&=need(rc,"\"0.4.3.0\\0\"","Windows string version is stale");
 ok&=need(host,"0.4.3","NRHost engine version is stale");
 ok&=need(ngx,"0.4.3","NGX engine version is stale");
 ok&=need(sl,"UniversalDLSS5-0.4.3","Streamline engine version is stale");
 ok&=need(ver,"kVersion=\"0.4.3\"","version.hpp short version is stale");
 ok&=need(ver,"kVersionLong=\"0.4.3.0\"","version.hpp long version is stale");
 ok&=need(ver,"kVersionPatch=3","version.hpp patch version is stale");
 ok&=need(log,"UniversalDLSS5 v0.4.3","attach log version is stale");
 ok&=need(readme,"**Current version:** v0.4.3","README current version is stale");
 ok&=need(runtime,"UniversalDLSS5 v0.4.3","runtime README version is stale");
 ok&=need(overwrite,"UniversalDLSS5 v0.4.3","overwrite instructions version is stale");
 ok&=need(release,"# UniversalDLSS5 v0.4.3","v0.4.3 release notes are missing");
 return ok?0:1;
}
