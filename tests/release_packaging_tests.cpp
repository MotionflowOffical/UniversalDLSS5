#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#ifndef UDLSS_SOURCE_DIR
#error UDLSS_SOURCE_DIR must be defined
#endif

static std::string readFile(const std::filesystem::path& p){std::ifstream f(p,std::ios::binary);std::ostringstream s;s<<f.rdbuf();return s.str();}
static bool need(const std::string&s,const char*t){if(s.find(t)!=std::string::npos)return true;std::cerr<<"missing: "<<t<<"\n";return false;}
static bool forbid(const std::string&s,const char*t){if(s.find(t)==std::string::npos)return true;std::cerr<<"forbidden: "<<t<<"\n";return false;}

int main(){
    const std::filesystem::path root=UDLSS_SOURCE_DIR;
    const auto cmake=readFile(root/"CMakeLists.txt");
    const auto build=readFile(root/"BUILD_RELEASE.bat");
    const auto ui=readFile(root/"src/controller/ui.cpp");
    const auto importer=readFile(root/"src/controller/runtime_importer.cpp");
    const auto ngx=readFile(root/"src/neural/ngx_nr.cpp");
    const auto readme=readFile(root/"README.md");
    const auto thirdParty=readFile(root/"THIRD_PARTY.md");
    const auto notice=readFile(root/"NOTICE");
    const auto license=readFile(root/"LICENSE");
    const auto resource=readFile(root/"resources"/"UniversalDLSS5.rc");
    bool ok=true;
    ok&=std::filesystem::exists(root/"LICENSE");
    ok&=std::filesystem::exists(root/"NOTICE");
    ok&=std::filesystem::exists(root/"THIRD_PARTY_LICENSES"/"MinHook-BSD-2-Clause.txt");
    ok&=std::filesystem::exists(root/"THIRD_PARTY_LICENSES"/"Streamline-MIT.txt");
    ok&=std::filesystem::exists(root/"THIRD_PARTY_LICENSES"/"NVIDIA-RTX-SDK-NOTICE.txt");
    ok&=need(cmake,"include(CPack)");
    ok&=need(cmake,"CPACK_NSIS_MUI_ICON");
    ok&=need(cmake,"CPACK_RESOURCE_FILE_LICENSE");
    ok&=need(cmake,"${CMAKE_CURRENT_SOURCE_DIR}/LICENSE");
    ok&=need(cmake,"THIRD_PARTY_LICENSES");
    ok&=need(cmake,"NOTICE");
    ok&=need(cmake,"MinHook-UPSTREAM-LICENSE.txt");
    ok&=need(cmake,"NVIDIA-DLSS-SDK-LICENSE.txt");
    ok&=need(cmake,"NVIDIA-Streamline-UPSTREAM-LICENSE.txt");
    ok&=need(cmake,"UniversalDLSS5.ico");
    ok&=need(cmake,"install(TARGETS UniversalDLSS5 UniversalDLSS5.Injector UniversalDLSS5.Bridge");
    ok&=need(cmake,"runtime/README.txt");
    ok&=forbid(cmake,"install(DIRECTORY \"${CMAKE_CURRENT_SOURCE_DIR}/runtime\"");
    ok&=need(build,"cpack -G NSIS");
    ok&=need(build,"UniversalDLSS5-Setup-x64");
    ok&=need(build,"cpack -G ZIP");
    ok&=need(build,"UniversalDLSS5-Portable-x64");
    ok&=need(ui,"Import NVIDIA SDK");
    ok&=need(ui,"NVIDIA download");
    ok&=need(ui,"Some antivirus products may flag DLL injection");
    ok&=need(importer,"WinVerifyTrust");
    ok&=need(importer,"WTHelperGetProvSignerFromChain");
    ok&=need(importer,"NVIDIA");
    ok&=need(importer,"IMAGE_FILE_MACHINE_AMD64");
    ok&=need(importer,"runtimeImportKind");
    ok&=need(ngx,"bridgeModuleDirectory");
    ok&=forbid(ngx,"std::filesystem::path(runtime_).parent_path()/L\"nvngx.dll_UniversalDLSS5_NRForwarder.dll\"");
    ok&=need(readme,"Apache License 2.0");
    ok&=need(readme,"not affiliated with, sponsored by, or endorsed by NVIDIA");
    ok&=need(readme,"Copyright 2026 MotionflowOffical");
    ok&=need(license,"Copyright 2026 MotionflowOffical");
    ok&=need(license,"Apache License");
    ok&=need(license,"Copyright [yyyy] [name of copyright owner]");
    ok&=need(notice,"Copyright 2026 MotionflowOffical");
    ok&=need(cmake,"CPACK_PACKAGE_VENDOR \"MotionflowOffical\"");
    ok&=need(cmake,"Copyright 2026 MotionflowOffical");
    ok&=need(resource,"MotionflowOffical");
    ok&=need(resource,"Copyright 2026 MotionflowOffical");
    ok&=need(thirdParty,"THIRD_PARTY_LICENSES");
    return ok?0:1;
}
