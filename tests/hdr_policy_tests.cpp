#include "udlss/hdr_policy.hpp"
#include <iostream>
using namespace udlss;
int main(){
    auto sdr=resolveHdrDecision(HdrMode::Auto,SwapchainPixelClass::Srgb8,SwapchainColorSpaceClass::Sdr709);
    if(sdr.hdrActive||!sdr.supported||sdr.encoding!=ColorEncoding::SdrSrgb){std::cerr<<"SDR sRGB detection failed\n";return 1;}
    auto ten=resolveHdrDecision(HdrMode::Auto,SwapchainPixelClass::Rgb10A2,SwapchainColorSpaceClass::Pq2020);
    if(!ten.hdrActive||!ten.supported||ten.encoding!=ColorEncoding::Hdr10Pq){std::cerr<<"HDR10 detection failed\n";return 2;}
    auto sc=resolveHdrDecision(HdrMode::Auto,SwapchainPixelClass::Float16,SwapchainColorSpaceClass::Linear709);
    if(!sc.hdrActive||!sc.supported||sc.encoding!=ColorEncoding::ScRgb){std::cerr<<"scRGB detection failed\n";return 3;}
    auto bad=resolveHdrDecision(HdrMode::Auto,SwapchainPixelClass::Unorm8,SwapchainColorSpaceClass::Pq2020);
    if(!bad.hdrActive||bad.supported||bad.encoding!=ColorEncoding::UnsupportedHdr){std::cerr<<"unsupported HDR contract not rejected\n";return 4;}
    auto forced=resolveHdrDecision(HdrMode::SDR,SwapchainPixelClass::Rgb10A2,SwapchainColorSpaceClass::Pq2020);
    if(forced.hdrActive||!forced.supported){std::cerr<<"forced SDR override failed\n";return 5;}
    ColorTransitionState t{};
    if(observeColorSignature(t,10)||colorPipelineStable(t)){std::cerr<<"initial transition state wrong\n";return 6;}
    observeColorSignature(t,10);observeColorSignature(t,10);observeColorSignature(t,10);
    if(!colorPipelineStable(t)){std::cerr<<"initial color state never stabilized\n";return 7;}
    if(!observeColorSignature(t,20)||colorPipelineStable(t)||t.transitionCount!=1){std::cerr<<"color-space transition not detected\n";return 8;}
    return 0;
}
