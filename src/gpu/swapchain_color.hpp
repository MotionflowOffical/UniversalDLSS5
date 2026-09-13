#pragma once
#include "udlss/hdr_policy.hpp"
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <cstdint>

namespace udlss::gpu {

struct SwapchainColorContext {
    DXGI_FORMAT format{DXGI_FORMAT_UNKNOWN};
    DXGI_COLOR_SPACE_TYPE colorSpace{DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709};
    HdrDecision decision{};
    float paperWhiteNits{203.0f};
    float maxNits{1000.0f};
    bool metadataPresent{};
    bool colorSpaceInferred{};
};

inline SwapchainPixelClass classifyPixelFormat(DXGI_FORMAT f){
    switch(f){
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB: return SwapchainPixelClass::Srgb8;
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8X8_UNORM: return SwapchainPixelClass::Unorm8;
    case DXGI_FORMAT_R10G10B10A2_UNORM: return SwapchainPixelClass::Rgb10A2;
    case DXGI_FORMAT_R16G16B16A16_FLOAT: return SwapchainPixelClass::Float16;
    default:return SwapchainPixelClass::Unknown;
    }
}

inline SwapchainColorSpaceClass classifyColorSpace(DXGI_COLOR_SPACE_TYPE c){
    switch(c){
    case DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020:
    case DXGI_COLOR_SPACE_RGB_STUDIO_G2084_NONE_P2020:
        return SwapchainColorSpaceClass::Pq2020;
    case DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709:
        return SwapchainColorSpaceClass::Linear709;
#ifdef DXGI_COLOR_SPACE_YCBCR_STUDIO_GHLG_TOPLEFT_P2020
    case DXGI_COLOR_SPACE_YCBCR_STUDIO_GHLG_TOPLEFT_P2020:
    case DXGI_COLOR_SPACE_YCBCR_FULL_GHLG_TOPLEFT_P2020:
        return SwapchainColorSpaceClass::Hlg2020;
#endif
    case DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709:
    case DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P709:
    case DXGI_COLOR_SPACE_RGB_STUDIO_G24_NONE_P709:
        return SwapchainColorSpaceClass::Sdr709;
    default:return SwapchainColorSpaceClass::Unknown;
    }
}

inline DXGI_COLOR_SPACE_TYPE inferOutputColorSpace(IDXGISwapChain* swap){
    if(!swap)return DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
    Microsoft::WRL::ComPtr<IDXGIOutput> output;
    if(FAILED(swap->GetContainingOutput(&output))||!output)return DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
    Microsoft::WRL::ComPtr<IDXGIOutput6> output6;
    if(FAILED(output.As(&output6))||!output6)return DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
    DXGI_OUTPUT_DESC1 desc{};
    if(FAILED(output6->GetDesc1(&desc)))return DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
    return desc.ColorSpace;
}

inline SwapchainColorContext querySwapchainColor(IDXGISwapChain* swap,HdrMode requested,
                                                  DXGI_COLOR_SPACE_TYPE trackedColorSpace,
                                                  bool colorSpaceKnown,
                                                  float metadataMaxNits=0.0f,
                                                  bool metadataPresent=false){
    SwapchainColorContext out{};
    if(!swap)return out;
    DXGI_SWAP_CHAIN_DESC desc{};
    if(SUCCEEDED(swap->GetDesc(&desc)))out.format=desc.BufferDesc.Format;
    if(colorSpaceKnown){
        out.colorSpace=trackedColorSpace;
    }else{
        // DXGI exposes SetColorSpace1 but has no matching GetColorSpace1 API.
        // For swapchains that existed before injection, infer the initial mode
        // from the containing output until we observe the game's next
        // SetColorSpace1 call.  The hook then becomes authoritative.
        out.colorSpace=inferOutputColorSpace(swap);
        out.colorSpaceInferred=true;
    }
    const auto pixelClass=classifyPixelFormat(out.format);
    const auto colorClass=classifyColorSpace(out.colorSpace);
    out.decision=resolveHdrDecision(requested,pixelClass,colorClass);
    // Some games/drivers briefly expose an SDR/unknown color-space value while
    // HDR10 metadata is already active during fullscreen/Alt-Tab transitions.
    // HDR metadata on a 10-bit swapchain is strong evidence of HDR10 unless the
    // user explicitly forced SDR. Treat it as PQ and let the transition-stability
    // gate wait before processing.
    if(requested!=HdrMode::SDR && metadataPresent && pixelClass==SwapchainPixelClass::Rgb10A2 &&
       colorClass!=SwapchainColorSpaceClass::Pq2020){
        out.decision={true,true,ColorEncoding::Hdr10Pq};
    }
    out.metadataPresent=metadataPresent;
    if(metadataMaxNits>0.0f)out.maxNits=metadataMaxNits;
    return out;
}

inline std::uint64_t colorSignature(const SwapchainColorContext& c){
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(c.format))<<32) |
           (static_cast<std::uint64_t>(static_cast<std::uint32_t>(c.colorSpace))<<8) |
           static_cast<std::uint64_t>(static_cast<std::uint32_t>(c.decision.encoding));
}

inline const wchar_t* colorEncodingLabel(ColorEncoding e){
    switch(e){
    case ColorEncoding::SdrLinear:return L"SDR linear/UNORM";
    case ColorEncoding::SdrSrgb:return L"SDR sRGB";
    case ColorEncoding::Hdr10Pq:return L"HDR10 PQ / BT.2020";
    case ColorEncoding::ScRgb:return L"scRGB linear";
    case ColorEncoding::UnsupportedHdr:return L"unsupported HDR contract";
    default:return L"unknown";
    }
}

} // namespace udlss::gpu
