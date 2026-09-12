#pragma once
#include "udlss/settings.hpp"
#include "udlss/shared_control.hpp"
#include "udlss/game_guides_api.hpp"
#include <d3d11.h>
#include <wrl/client.h>
#include <string>

namespace udlss::gpu {
struct GuideProbeResult {
    Microsoft::WRL::ComPtr<ID3D11Texture2D> depth;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> motion;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> controlMask;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> normals;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> albedo;
    DXGI_FORMAT depthViewFormat{DXGI_FORMAT_UNKNOWN};
    bool depthInverted{true};
    bool depthConventionKnown{};
    bool cameraCut{};
    bool motionConventionValid{};
    bool controlMaskConventionValid{};
    float motionScaleX{1.0f}, motionScaleY{1.0f};
    bool fromAdapter{};
    std::wstring provider;
};

class D3D11GuideExtractor {
public:
    ~D3D11GuideExtractor();
    bool initialize(ID3D11Device* device,ID3D11DeviceContext* context);
    void probe(ID3D11Texture2D* backbuffer,const Settings& settings,GuideProbeResult& out,RuntimeStatus& status);
    bool prepareDepth(const GuideProbeResult& probe,UINT width,UINT height,ID3D11ShaderResourceView** srv,bool& inverted,RuntimeStatus& status);
    bool copyMotion(const GuideProbeResult& probe,ID3D11Texture2D* destination,UINT width,UINT height);
    bool copyControlMask(const GuideProbeResult& probe,ID3D11Texture2D* destination,UINT width,UINT height);
    bool adapterLoaded() const { return adapter_!=nullptr; }
private:
    void tryLoadAdapter();
    bool adoptDepth(ID3D11Texture2D* texture,DXGI_FORMAT viewFormat,UINT width,UINT height,GuideProbeResult& out);
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> depthCopy_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> depthSrv_;
    DXGI_FORMAT depthCopySource_{DXGI_FORMAT_UNKNOWN};
    UINT depthCopyWidth_{},depthCopyHeight_{};
    void* adapter_{};
    GameGuidesGetFrameV1 getFrame_{};
};
}
