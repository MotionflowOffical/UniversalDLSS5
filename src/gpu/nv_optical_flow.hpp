#pragma once
#include "udlss/settings.hpp"
#include "udlss/shared_control.hpp"
#include <d3d11.h>
#include <wrl/client.h>
#include <cstdint>

#ifdef UDLSS_WITH_NVOF
#include <nvOpticalFlowD3D11.h>
#endif

namespace udlss::gpu {
class NvOpticalFlowD3D11 {
public:
    NvOpticalFlowD3D11() = default;
    ~NvOpticalFlowD3D11();
    NvOpticalFlowD3D11(const NvOpticalFlowD3D11&) = delete;
    NvOpticalFlowD3D11& operator=(const NvOpticalFlowD3D11&) = delete;

    bool configure(ID3D11Device* device, ID3D11DeviceContext* context,
                   std::uint32_t width, std::uint32_t height,
                   std::uint32_t desiredGrid, LatencyMode latency,
                   RuntimeStatus& status);
    void resetHistory();
    void shutdown();
    bool available() const { return ready_; }
    ID3D11RenderTargetView* currentInputRtv() const;
    bool execute(RuntimeStatus& status);
    ID3D11ShaderResourceView* vectorSrv() const { return vectorSrv_.Get(); }
    ID3D11ShaderResourceView* costSrv() const { return costSrv_.Get(); }
    std::uint32_t gridSize() const { return grid_; }
    bool hasCost() const { return hasCost_; }
    bool hasHistory() const { return havePrevious_; }
private:
#ifdef UDLSS_WITH_NVOF
    bool loadApi(RuntimeStatus& status);
    bool createResources(RuntimeStatus& status);
    bool supportsFormat(NV_OF_BUFFER_USAGE usage, DXGI_FORMAT format) const;
    bool queryGrid(std::uint32_t desiredGrid, RuntimeStatus& status);
    bool registerResource(ID3D11Texture2D* texture, NvOFGPUBufferHandle& handle, RuntimeStatus& status);
    void unregisterAll();

    HMODULE module_{};
    NV_OF_D3D11_API_FUNCTION_LIST api_{};
    NvOFHandle session_{};
    NvOFGPUBufferHandle inputHandles_[2]{};
    NvOFGPUBufferHandle vectorHandle_{};
    NvOFGPUBufferHandle costHandle_{};
#endif
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> inputs_[2];
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> inputRtvs_[2];
    Microsoft::WRL::ComPtr<ID3D11Texture2D> vectorTex_, costTex_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> vectorSrv_, costSrv_;
    std::uint32_t width_{}, height_{}, grid_{}, desiredGrid_{};
    LatencyMode latency_{LatencyMode::Balanced};
    std::uint32_t writeIndex_{};
    std::uint32_t previousIndex_{};
    bool havePrevious_{};
    bool hasCost_{};
    bool ready_{};
    bool attempted_{};
    ID3D11Device* attemptedDevice_{};
    std::uint32_t attemptedWidth_{}, attemptedHeight_{}, attemptedGrid_{};
    LatencyMode attemptedLatency_{LatencyMode::Balanced};
};
} // namespace udlss::gpu
