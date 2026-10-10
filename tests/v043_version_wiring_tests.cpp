#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#ifndef UDLSS_SOURCE_DIR
#define UDLSS_SOURCE_DIR "."
#endif
static std::string read(const std::filesystem::path&p){std::ifstream f(p,std::ios::binary);std::ostringstream s;s<<f.rdbuf();return s.str();}
int main(){
 const std::filesystem::path root=UDLSS_SOURCE_DIR;
 const auto release=read(root/"docs/V0.4.3_RELEASE.md");
 if(release.find("# UniversalDLSS5 v0.4.3")==std::string::npos){std::cerr<<"Historical 0.4.3 release notes are missing or mislabeled\n";return 1;}
 return 0;
}
