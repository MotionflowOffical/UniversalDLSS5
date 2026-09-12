#pragma once
#include "settings.hpp"
#include <charconv>
#include <cmath>
#include <sstream>
#include <string>
#include <string_view>

namespace udlss {
inline std::string encodeProfile(const Settings& in) {
    Settings s=in; normalize(s);
    std::ostringstream o;
    o << "version=" << s.structVersion << '\n'
      << "enabled=" << s.enabled << '\n'
      << "protectUI=" << s.protectUI << '\n'
      << "protectCursor=" << s.protectCursor << '\n'
      << "useControlMask=" << s.useControlMask << '\n'
      << "invertMotionY=" << s.invertMotionY << '\n'
      << "processSecondarySwapchains=" << s.processSecondarySwapchains << '\n'
      << "attachProcessTree=" << s.attachProcessTree << '\n'
      << "allowD3D11On12=" << s.allowD3D11On12 << '\n'
      << "attemptUnsupportedHardware=" << s.attemptUnsupportedHardware << '\n'
      << "resetOnTemporalGap=" << s.resetOnTemporalGap << '\n'
      << "useGameDepth=" << s.useGameDepth << '\n'
      << "loadGameGuideAdapter=" << s.loadGameGuideAdapter << '\n'
      << "nrAutoMask=" << s.nrAutoMask << '\n'
      << "nrUiCorrection=" << s.nrUiCorrection << '\n' 
      << "backend=" << static_cast<unsigned>(s.backend) << '\n'
      << "motionSource=" << static_cast<unsigned>(s.motionSource) << '\n'
      << "latencyMode=" << static_cast<unsigned>(s.latencyMode) << '\n'
      << "hdrMode=" << static_cast<unsigned>(s.hdrMode) << '\n'
      << "depthMode=" << static_cast<unsigned>(s.depthMode) << '\n'
      << "debugView=" << static_cast<unsigned>(s.debugView) << '\n' 
      << "sharpness=" << s.sharpness << '\n'
      << "exposure=" << s.exposure << '\n'
      << "temporalStrength=" << s.temporalStrength << '\n'
      << "motionScale=" << s.motionScale << '\n'
      << "motionScaleX=" << s.motionScaleX << '\n'
      << "motionScaleY=" << s.motionScaleY << '\n'
      << "staticMotionDeadzone=" << s.staticMotionDeadzone << '\n' 
      << "flowConfidenceThreshold=" << s.flowConfidenceThreshold << '\n'
      << "disocclusionThreshold=" << s.disocclusionThreshold << '\n'
      << "textProtection=" << s.textProtection << '\n'
      << "uiProtection=" << s.uiProtection << '\n'
      << "controlMaskStrength=" << s.controlMaskStrength << '\n'
      << "historyClamp=" << s.historyClamp << '\n'
      << "reactiveStrength=" << s.reactiveStrength << '\n'
      << "edgeThreshold=" << s.edgeThreshold << '\n'
      << "nrIntensity=" << s.nrIntensity << '\n'
      << "nrTone=" << s.nrTone << '\n'
      << "nrStructure=" << s.nrStructure << '\n'
      << "nrSkinStructure=" << s.nrSkinStructure << '\n'
      << "nrPaperWhite=" << s.nrPaperWhite << '\n'
      << "nrTransferStrength=" << s.nrTransferStrength << '\n'
      << "nrColorStrength=" << s.nrColorStrength << '\n'
      << "debugSplit=" << s.debugSplit << '\n'
      << "nrStyle=" << s.nrStyle << '\n'
      << "nrPreset=" << s.nrPreset << '\n' 
      << "flowSearchRadius=" << s.flowSearchRadius << '\n'
      << "flowDownsample=" << s.flowDownsample << '\n'
      << "maxFramesInFlight=" << s.maxFramesInFlight << '\n';
    return o.str();
}

inline bool decodeProfile(std::string_view text, Settings& out) {
    Settings s=defaultSettings();
    std::istringstream in{std::string(text)};
    std::string line;
    bool any=false;
    std::uint32_t storedVersion=0;
    bool backendSeen=false;
    auto setBool=[&](std::string_view v,bool& x){ x=(v=="1"||v=="true"||v=="TRUE"); };
    auto setF=[&](std::string_view v,float& x){ try{x=std::stof(std::string(v));}catch(...){}};
    auto setU=[&](std::string_view v,std::uint32_t& x){ try{x=static_cast<std::uint32_t>(std::stoul(std::string(v)));}catch(...){}};
    while(std::getline(in,line)){
        if(line.empty()||line[0]=='#') continue;
        auto p=line.find('='); if(p==std::string::npos) continue;
        std::string_view k(line.data(),p), v(line.data()+p+1,line.size()-p-1); any=true;
        if(k=="version") setU(v,storedVersion);
        else if(k=="enabled") setBool(v,s.enabled); else if(k=="protectUI") setBool(v,s.protectUI);
        else if(k=="protectCursor") setBool(v,s.protectCursor); else if(k=="useControlMask") setBool(v,s.useControlMask);
        else if(k=="invertMotionY") setBool(v,s.invertMotionY); else if(k=="processSecondarySwapchains") setBool(v,s.processSecondarySwapchains);
        else if(k=="attachProcessTree") setBool(v,s.attachProcessTree); else if(k=="allowD3D11On12") setBool(v,s.allowD3D11On12); else if(k=="attemptUnsupportedHardware") setBool(v,s.attemptUnsupportedHardware);
        else if(k=="resetOnTemporalGap") setBool(v,s.resetOnTemporalGap); else if(k=="useGameDepth") setBool(v,s.useGameDepth); else if(k=="loadGameGuideAdapter") setBool(v,s.loadGameGuideAdapter); else if(k=="nrAutoMask") setBool(v,s.nrAutoMask); else if(k=="nrUiCorrection") setBool(v,s.nrUiCorrection);
        else if(k=="backend"){std::uint32_t x=0;setU(v,x);s.backend=static_cast<BackendMode>(x>2?2:x);backendSeen=true;} 
        else if(k=="motionSource"){std::uint32_t x=0;setU(v,x);s.motionSource=static_cast<MotionSource>(x>2?0:x);} 
        else if(k=="latencyMode"){std::uint32_t x=0;setU(v,x);s.latencyMode=static_cast<LatencyMode>(x>2?0:x);} 
        else if(k=="hdrMode"){std::uint32_t x=0;setU(v,x);s.hdrMode=static_cast<HdrMode>(x>2?0:x);} else if(k=="depthMode"){std::uint32_t x=0;setU(v,x);s.depthMode=static_cast<DepthGuideMode>(x>3?0:x);} else if(k=="debugView"){std::uint32_t x=0;setU(v,x);s.debugView=static_cast<DebugView>(x>8?0:x);} 
        else if(k=="sharpness") setF(v,s.sharpness); else if(k=="exposure") setF(v,s.exposure);
        else if(k=="temporalStrength") setF(v,s.temporalStrength); else if(k=="motionScale") setF(v,s.motionScale); else if(k=="motionScaleX") setF(v,s.motionScaleX); else if(k=="motionScaleY") setF(v,s.motionScaleY); else if(k=="staticMotionDeadzone") setF(v,s.staticMotionDeadzone);
        else if(k=="flowConfidenceThreshold") setF(v,s.flowConfidenceThreshold); else if(k=="disocclusionThreshold") setF(v,s.disocclusionThreshold);
        else if(k=="textProtection") setF(v,s.textProtection); else if(k=="uiProtection") setF(v,s.uiProtection);
        else if(k=="controlMaskStrength") setF(v,s.controlMaskStrength); else if(k=="historyClamp") setF(v,s.historyClamp);
        else if(k=="reactiveStrength") setF(v,s.reactiveStrength); else if(k=="edgeThreshold") setF(v,s.edgeThreshold);
        else if(k=="nrIntensity") setF(v,s.nrIntensity); else if(k=="nrTone") setF(v,s.nrTone); else if(k=="nrStructure") setF(v,s.nrStructure); else if(k=="nrSkinStructure") setF(v,s.nrSkinStructure); else if(k=="nrPaperWhite") setF(v,s.nrPaperWhite); else if(k=="nrTransferStrength") setF(v,s.nrTransferStrength); else if(k=="nrColorStrength") setF(v,s.nrColorStrength); else if(k=="debugSplit") setF(v,s.debugSplit); else if(k=="nrStyle") setU(v,s.nrStyle); else if(k=="nrPreset") setU(v,s.nrPreset);
        else if(k=="flowSearchRadius") setU(v,s.flowSearchRadius); else if(k=="flowDownsample") setU(v,s.flowDownsample);
        else if(k=="maxFramesInFlight") setU(v,s.maxFramesInFlight);
    }
    // Historical profiles stored backend=0 for the neural route. v0.2.8 makes
    // the direct in-game mount the preferred route. Preserve explicit backend=2
    // external-host profiles, but migrate backend=0 to InGameNR.
    if(backendSeen && storedVersion < kSettingsVersion && s.backend == BackendMode::StreamlineDLSS5)
        s.backend = BackendMode::InGameNR;
    if(storedVersion < 5) { s.useControlMask=true; s.resetOnTemporalGap=true; }
    // v0.2.8/v6 shipped experimental, non-neutral Feature-18 defaults.  Only
    // migrate values that still match those exact defaults; custom tuning is
    // preserved.  This makes existing profiles use the same neutral baseline
    // as a fresh profile without wiping user-selected values.
    if(storedVersion > 0 && storedVersion < 7) {
        auto oldDefault=[](float value,float oldValue){return std::fabs(value-oldValue)<1e-5f;};
        if(oldDefault(s.temporalStrength,0.80f)) s.temporalStrength=1.00f;
        if(oldDefault(s.nrIntensity,0.85f)) s.nrIntensity=1.00f;
        if(oldDefault(s.nrTone,0.45f)) s.nrTone=1.00f;
        // -1 is the runtime's documented/observed auto value for skin structure.
        if(storedVersion==6) { s.nrAutoMask=false; s.nrUiCorrection=false; }
    }
    // v7 briefly used 0 as the fresh-profile skin default. Restore the
    // runtime's auto sentinel without touching user-customized values.
    if(storedVersion==7 && std::fabs(s.nrSkinStructure-0.0f)<1e-5f) s.nrSkinStructure=-1.0f;
    normalize(s); out=s; return any;
}

bool loadProfileFile(const std::wstring& path, Settings& out);
bool saveProfileFile(const std::wstring& path, const Settings& s);
}
