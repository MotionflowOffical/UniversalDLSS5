#include <cassert>
#include <cmath>
#include "udlss/settings.hpp"
using namespace udlss;
static bool nearf(float a,float b){return std::fabs(a-b)<1e-6f;}
int main(){
    auto browser = settingsForPreset(TuningPreset::Browser);
    assert(browser.protectUI);
    assert(browser.protectCursor);
    assert(browser.textProtection >= 0.90f);
    assert(browser.uiProtection >= 0.90f);
    assert(browser.temporalStrength <= 0.75f);
    assert(browser.latencyMode == LatencyMode::UltraLow);

    auto game2d = settingsForPreset(TuningPreset::Game2D);
    assert(game2d.temporalStrength > browser.temporalStrength);
    assert(game2d.reactiveStrength >= browser.reactiveStrength);

    auto video = settingsForPreset(TuningPreset::Video);
    assert(video.protectUI);
    assert(video.sharpness <= 0.30f);
    assert(video.latencyMode == LatencyMode::Balanced);

    auto aggressive = settingsForPreset(TuningPreset::Aggressive);
    assert(aggressive.temporalStrength >= 0.90f);
    assert(aggressive.sharpness >= 0.30f);

    Settings custom = defaultSettings();
    custom.exposure = 3.0f;
    applyPreset(custom, TuningPreset::Browser);
    assert(!nearf(custom.exposure, 3.0f));
    assert(custom.structVersion == kSettingsVersion);
    return 0;
}
