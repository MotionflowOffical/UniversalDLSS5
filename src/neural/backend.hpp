#pragma once
#include "udlss/settings.hpp"
#include "udlss/shared_control.hpp"
#include <d3d11.h>
#include <d3d12.h>
#include <string>

namespace udlss::neural {
struct BackendInitContext {
    ID3D11Device* d3d11Device{};
    ID3D11DeviceContext* d3d11Context{};
    ID3D12Device* d3d12Device{};
    ID3D12CommandQueue* d3d12Queue{};
};
struct FrameResources {
    ID3D11Texture2D* input{};
    ID3D11Texture2D* output{};
    ID3D11Texture2D* motion{};
    ID3D11Texture2D* depth{};
    ID3D11Texture2D* controlMask{};
    std::uint32_t width{},height{};
    DXGI_FORMAT inputFormat{DXGI_FORMAT_R16G16B16A16_FLOAT};
    DXGI_FORMAT motionFormat{DXGI_FORMAT_R16G16_FLOAT};
    DXGI_FORMAT depthFormat{DXGI_FORMAT_R32_FLOAT};
    DXGI_FORMAT controlMaskFormat{DXGI_FORMAT_R8G8B8A8_UNORM};
    bool depthInverted{true};
    bool resetHistory{};
    float motionScaleX{1.0f}, motionScaleY{1.0f};
};
class Backend {
public:
    virtual ~Backend()=default;
    virtual bool initialize(const BackendInitContext& context,const std::wstring& runtime,const Settings& settings,RuntimeStatus& status)=0;
    virtual bool evaluate(ID3D11DeviceContext* ctx,const FrameResources&,const Settings&,RuntimeStatus& status)=0;
    virtual void reset()=0;
    virtual const wchar_t* name() const=0;
};
Backend* createPassthrough();
Backend* createInGameNR();
Backend* createNgxNR();
Backend* createExternalHostNR();
Backend* createStreamlineNR();
void destroyBackend(Backend* b);
}
