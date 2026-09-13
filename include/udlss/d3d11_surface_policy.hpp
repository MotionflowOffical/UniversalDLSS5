#pragma once

namespace udlss {

// Direct SRV/RTV view reuse is only considered safe for injector-owned
// D3D11On12 staging resources. Native D3D11 swapchain backbuffers remain on
// the conservative copy/fresh-RTV path because engines can expose shader-bind
// flags without guaranteeing that Present-time direct sampling/view retention
// is compatible with their backbuffer lifecycle.
constexpr bool allowDirectD3D11SurfaceReuse(bool injectorOwnedInterop) noexcept {
    return injectorOwnedInterop;
}

} // namespace udlss
