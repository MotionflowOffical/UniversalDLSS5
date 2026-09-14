#pragma once
#include <cstdint>
#include <string_view>

namespace udlss {

enum class RendererRoute : std::uint32_t {
    Unknown = 0,
    NativeD3D11,
    NativeD3D12,
    ModernD3D12Recovery,
    CompatD3D10,
    CompatD3D9Ex,
    CompatD3D9Classic,
    CompatOpenGL,
    CompatVulkan,
    Unsupported,
};

enum class CompatInterop : std::uint32_t {
    None = 0,
    D3D11Native,
    D3D12On12,
    D3D10Shared,
    D3D9ExShared,
    D3D9ClassicTransfer,
    WglNvDxInterop,
    OpenGlPboTransfer,
    VulkanExternalMemory,
};

enum class DetachMode : std::uint32_t {
    None = 0,
    Unloaded,
    DetachedResident,
};

inline constexpr bool isNativeRendererRoute(RendererRoute route) noexcept {
    return route == RendererRoute::NativeD3D11 || route == RendererRoute::NativeD3D12;
}

inline constexpr bool isCompatibilityRendererRoute(RendererRoute route) noexcept {
    switch (route) {
    case RendererRoute::CompatD3D10:
    case RendererRoute::CompatD3D9Ex:
    case RendererRoute::CompatD3D9Classic:
    case RendererRoute::CompatOpenGL:
    case RendererRoute::CompatVulkan:
        return true;
    default:
        return false;
    }
}

inline constexpr bool routeAllowsLegacyCopies(RendererRoute route) noexcept {
    return route == RendererRoute::CompatD3D9Classic;
}

inline constexpr std::wstring_view rendererRouteName(RendererRoute route) noexcept {
    switch (route) {
    case RendererRoute::NativeD3D11: return L"Native D3D11";
    case RendererRoute::NativeD3D12: return L"Native D3D12";
    case RendererRoute::ModernD3D12Recovery: return L"Modern D3D12 recovery";
    case RendererRoute::CompatD3D10: return L"D3D10 compatibility";
    case RendererRoute::CompatD3D9Ex: return L"D3D9Ex compatibility";
    case RendererRoute::CompatD3D9Classic: return L"classic D3D9 compatibility";
    case RendererRoute::CompatOpenGL: return L"OpenGL compatibility";
    case RendererRoute::CompatVulkan: return L"Vulkan compatibility";
    case RendererRoute::Unsupported: return L"Unsupported";
    default: return L"Unknown";
    }
}

inline constexpr std::wstring_view compatInteropName(CompatInterop interop) noexcept {
    switch (interop) {
    case CompatInterop::D3D11Native: return L"native D3D11";
    case CompatInterop::D3D12On12: return L"D3D11On12 staging";
    case CompatInterop::D3D10Shared: return L"D3D10/D3D11 shared keyed-mutex texture";
    case CompatInterop::D3D9ExShared: return L"D3D9Ex/D3D11 shared texture";
    case CompatInterop::D3D9ClassicTransfer: return L"full-resolution legacy transfer ring";
    case CompatInterop::WglNvDxInterop: return L"WGL_NV_DX_interop2";
    case CompatInterop::OpenGlPboTransfer: return L"OpenGL PBO transfer";
    case CompatInterop::VulkanExternalMemory: return L"Vulkan/D3D11 external memory";
    default: return L"none";
    }
}

inline constexpr std::wstring_view detachModeName(DetachMode mode) noexcept {
    switch (mode) {
    case DetachMode::Unloaded: return L"unloaded";
    case DetachMode::DetachedResident: return L"detached-resident";
    default: return L"active";
    }
}

} // namespace udlss
