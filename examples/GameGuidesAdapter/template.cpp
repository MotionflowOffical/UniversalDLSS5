#include "udlss/game_guides_api.hpp"
#include <d3d11.h>

extern "C" __declspec(dllexport)
bool UdlssGameGuides_GetFrameV1(void* device, void* context, void* backbuffer, udlss::GameGuideFrameV1* outFrame) {
    if(!outFrame || outFrame->abi!=udlss::kGameGuidesAbiV1 || outFrame->size<sizeof(*outFrame)) return false;
    // Engine-specific code may AddRef and assign depth/motion/control/normals/albedo textures here.
    // Do not scan arbitrary process memory; prefer documented engine/plugin graphics hooks.
    (void)device; (void)context; (void)backbuffer;
    return false;
}
