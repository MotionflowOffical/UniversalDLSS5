#pragma once
#include <d3d12.h>

namespace udlss::gpu {
inline D3D12_RESOURCE_FLAGS sharedGuideTextureFlags(DXGI_FORMAT format, bool simultaneousAccess=true) {
    // Packed depth/stencil families cannot be render targets. Keep their
    // conservative allocation fallback; typed R32/R16 depth is ordinary color
    // data here, not a depth-stencil attachment.
    if(format==DXGI_FORMAT_R24G8_TYPELESS||format==DXGI_FORMAT_R32G8X24_TYPELESS)
        return D3D12_RESOURCE_FLAG_NONE;
    // D3D11 opening a D3D12 NT-shared texture requires a shareable bind layout.
    // SRV-only D3D12 allocations open with E_INVALIDARG on the tested driver;
    // ALLOW_SIMULTANEOUS_ACCESS alone does not provide the missing RT binding.
    return D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET|
        (simultaneousAccess?D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS:D3D12_RESOURCE_FLAG_NONE);
}
}
