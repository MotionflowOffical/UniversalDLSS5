#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

static std::string readFile(const std::filesystem::path& path){
    std::ifstream f(path,std::ios::binary);
    std::ostringstream s;s<<f.rdbuf();return s.str();
}
static bool need(bool cond,const char* message){if(!cond)std::cerr<<message<<"\n";return cond;}
#ifndef UDLSS_SOURCE_DIR
#define UDLSS_SOURCE_DIR "."
#endif
int main(){
    const std::filesystem::path root=UDLSS_SOURCE_DIR;
    const auto host=readFile(root/"src/host/main.cpp");
    const auto external=readFile(root/"src/neural/external_host.cpp");
    const auto d3d9=readFile(root/"src/compat/d3d9_frontend.cpp");
    const auto dispatch=readFile(root/"src/compat/compat_dispatch.cpp");
    bool ok=true;
    ok&=need(host.find("NVSDK_NGX_D3D12_AllocateParameters(&params)")!=std::string::npos,
             "NRHost must allocate the Feature-18 parameter block, not reuse capability parameters");
    ok&=need(host.find("NVSDK_NGX_D3D12_GetCapabilityParameters(&params)")==std::string::npos,
             "NRHost still uses capability parameters as its mutable Feature-18 parameter block");
    ok&=need(host.find("snInit(appId,shared->runtimePath,device.Get(),NVSDK_NGX_Version_API,nullptr)")!=std::string::npos,
             "NRHost signed-snippet Init_Ext must use runtime directory and a null parameter block");
    ok&=need(host.find("DLSS.Indicator.Invert.X.Axis")!=std::string::npos && host.find("DLSS.Indicator.Invert.Y.Axis")!=std::string::npos,
             "NRHost must explicitly bind both indicator-axis parameters");
    ok&=need(host.find("params->Set(kPaperWhite")==std::string::npos &&
             host.find("params->Set(kTransferStrength")==std::string::npos &&
             host.find("params->Set(kColorStrength")==std::string::npos,
             "NRHost must not write non-contract paper-white/transfer/color parameters into Feature 18");
    ok&=need(external.find("D3D11_RESOURCE_MISC_SHARED|D3D11_RESOURCE_MISC_SHARED_NTHANDLE")!=std::string::npos,
             "External host textures must use the known-good SHARED|SHARED_NTHANDLE contract");
    ok&=need(external.find("D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX")==std::string::npos,
             "External host textures must not use keyed-mutex sharing when fence synchronization is active");
    ok&=need(external.find("CreateSharedHandle(nullptr,GENERIC_ALL,nullptr,&hnd)")!=std::string::npos,
             "External host shared texture handles must be created with GENERIC_ALL");
    const auto signalPos=external.find("ctx11v4_->Signal(fence11_.Get(),inputValue)");
    const auto flushPos=external.find("ctx11_->Flush()",signalPos);
    ok&=need(signalPos!=std::string::npos && flushPos!=std::string::npos && flushPos-signalPos<500,
             "External host producer fence must be flushed before NRHost waits on it");
    ok&=need(external.find("STARTF_USESHOWWINDOW")!=std::string::npos && external.find("SW_HIDE")!=std::string::npos,
             "NRHost launch must explicitly request a hidden window");
    ok&=need(d3d9.find("frame.colorEncoding=ColorEncoding::SdrSrgb")!=std::string::npos,
             "D3D9 compatibility frames must be identified as display-referred SDR/sRGB");
    ok&=need(dispatch.find("frame.colorEncoding")!=std::string::npos && dispatch.find("&colorContext")!=std::string::npos,
             "Compatibility dispatcher must forward the frontend color encoding into the D3D11 pipeline");
    return ok?0:1;
}
