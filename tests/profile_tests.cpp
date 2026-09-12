#include <cassert>
#include <string>
#include "udlss/profile.hpp"
int main(){
    udlss::Settings s = udlss::defaultSettings();
    s.sharpness = 0.42f; s.flowSearchRadius = 9; s.protectUI = false; s.nrIntensity=0.67f; s.nrTone=0.25f; s.nrStructure=1.26f; s.staticMotionDeadzone=0.75f; s.depthMode=udlss::DepthGuideMode::ForceNormal; s.debugView=udlss::DebugView::Split;
    auto text = udlss::encodeProfile(s);
    udlss::Settings out{};
    assert(udlss::decodeProfile(text, out));
    assert(out.sharpness > 0.419f && out.sharpness < 0.421f);
    assert(out.flowSearchRadius == 9);
    assert(!out.protectUI);
    assert(out.backend == udlss::BackendMode::InGameNR);
    assert(out.nrIntensity > 0.669f && out.nrIntensity < 0.671f);
    assert(out.nrTone > 0.249f && out.nrTone < 0.251f);
    assert(out.nrStructure > 1.259f && out.nrStructure < 1.261f);
    assert(out.staticMotionDeadzone > 0.749f && out.staticMotionDeadzone < 0.751f);
    assert(out.depthMode == udlss::DepthGuideMode::ForceNormal);
    assert(out.debugView == udlss::DebugView::Split);
    // Historical profiles used backend=0 for the neural route. v0.2.8 makes
    // the direct in-game mount the preferred neural route, so backend=0 migrates
    // to InGameNR while an explicit backend=2 remains ExternalHostNR.
    udlss::Settings legacy{};
    const std::string legacyText = "version=3\nbackend=0\nsharpness=0.2\n";
    assert(udlss::decodeProfile(legacyText, legacy));
    assert(legacy.backend == udlss::BackendMode::InGameNR);


    udlss::Settings legacyHost{};
    assert(udlss::decodeProfile("version=5\nbackend=2\n", legacyHost));
    assert(legacyHost.backend == udlss::BackendMode::ExternalHostNR);

    // Explicit passthrough remains passthrough across migration.
    udlss::Settings legacyPass{};
    assert(udlss::decodeProfile("version=3\nbackend=1\n", legacyPass));
    assert(legacyPass.backend == udlss::BackendMode::Passthrough);
    return 0;
}
