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
 const auto s=read(std::filesystem::path(UDLSS_SOURCE_DIR)/"src/neural/ngx_nr.cpp");
 assert(s.find("DLSSNR.ControlMask")!=std::string::npos);
 assert(s.find("DLSSNR.SkinStructureStrength")!=std::string::npos);
 assert(s.find("DLSSNR.UICorrection")!=std::string::npos);
 assert(s.find("DLSS.Indicator.Invert.X.Axis")!=std::string::npos);
 assert(s.find("DLSS.Indicator.Invert.Y.Axis")!=std::string::npos);
 assert(s.find("NVSDK_NGX_Parameter_SetF(params_,kPaperWhite")==std::string::npos);
 assert(s.find("NVSDK_NGX_Parameter_SetF(params_,kTransferStrength")==std::string::npos);
 assert(s.find("NVSDK_NGX_Parameter_SetF(params_,kColorStrength")==std::string::npos);
 assert(s.find("frame.motionScaleX")!=std::string::npos);
 assert(s.find("frame.motionScaleY")!=std::string::npos);
 assert(s.find("frame.depthInverted")!=std::string::npos);
 assert(s.find("frame.resetHistory")!=std::string::npos);
 assert(s.find("settings.nrIntensity")!=std::string::npos);
 assert(s.find("settings.nrTone")!=std::string::npos);
 assert(s.find("settings.nrStructure")!=std::string::npos);
 assert(s.find("settings.nrSkinStructure")!=std::string::npos);
 assert(s.find("settings.nrUiCorrection")!=std::string::npos);
 assert(s.find("slot.controlMask")!=std::string::npos);
}
