#pragma once
#include "udlss/settings.hpp"
#include "udlss/shared_control.hpp"
#include "udlss/backend_policy.hpp"
#include "../neural/backend.hpp"
#include "nv_optical_flow.hpp"
#include "d3d11_guide_extractor.hpp"
#include "udlss/camera_motion_math.hpp"
#include "udlss/motion_route_policy.hpp"
#include "swapchain_color.hpp"
#include <d3d11_1.h>
#include <wrl/client.h>
#include <string>
#include <array>
namespace udlss::gpu {
class D3D11Pipeline {
public:
 D3D11Pipeline()=default;~D3D11Pipeline();
 bool initialize(ID3D11Device* device,ID3D11DeviceContext* context,const std::wstring& moduleDir,RuntimeStatus& status);
 void setNativeD3D12(ID3D12Device* device,ID3D12CommandQueue* queue);
 bool process(ID3D11Texture2D* backbuffer,const Settings& settings,const std::wstring& runtime,RuntimeStatus& status,const GuideProbeResult* externalGuide=nullptr,const SwapchainColorContext* colorContext=nullptr);
 void reset();
 void retryNeural();
private:
 struct Params {std::uint32_t width,height,downsample,radius;float exposure,motionScale,confidence,textProtection;float uiProtection,edgeThreshold,sharpness,reactive;float controlMaskStrength,historyClamp,disocclusion,temporal;std::uint32_t invertY,hasHistory,pad0,pad1;float staticDeadzone,motionScaleX,motionScaleY,debugSplit;std::uint32_t debugView,depthMode,sourceSrgb,useControlMask;std::uint32_t colorEncoding,hdrActive;float hdrPaperWhite,hdrMaxNits;};
 bool createShaders(RuntimeStatus&);bool runComputeWithConstants(ID3D11ComputeShader*,ID3D11ShaderResourceView*const*,UINT,ID3D11UnorderedAccessView*,UINT,UINT,ID3D11Buffer*);bool ensureResources(const D3D11_TEXTURE2D_DESC&,const Settings&,RuntimeStatus&);bool ensureBackend(const Settings&,const std::wstring&,RuntimeStatus&);bool runCompute(ID3D11ComputeShader*,ID3D11ShaderResourceView*const*,UINT,ID3D11UnorderedAccessView*,UINT,UINT);bool drawSrv(ID3D11ShaderResourceView*,ID3D11RenderTargetView*,UINT,UINT);bool blit(ID3D11Texture2D*,RuntimeStatus&);ID3D11ShaderResourceView* tryDirectSourceSrv(ID3D11Texture2D*,const D3D11_TEXTURE2D_DESC&);ID3D11RenderTargetView* getBlitRtv(ID3D11Texture2D*);void releaseViews();
 Microsoft::WRL::ComPtr<ID3D12Device> nativeD12_;Microsoft::WRL::ComPtr<ID3D12CommandQueue> nativeQueue12_;
 Microsoft::WRL::ComPtr<ID3D11Device> device_;Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;Microsoft::WRL::ComPtr<ID3D11Device1> device1_;Microsoft::WRL::ComPtr<ID3D11DeviceContext1> context1_;Microsoft::WRL::ComPtr<ID3DDeviceContextState> ownState_;
 Microsoft::WRL::ComPtr<ID3D11ComputeShader> convert_,downsample_,flow_,nvofUnpack_,motion_,nativeMotionConvert_,cameraMotion_,mask_,depthConvert_,post_;Microsoft::WRL::ComPtr<ID3D11VertexShader> vs_;Microsoft::WRL::ComPtr<ID3D11PixelShader> ps_;Microsoft::WRL::ComPtr<ID3D11Buffer> cb_,nativeMotionCb_,cameraCb_;Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_;
 Microsoft::WRL::ComPtr<ID3D11Texture2D> sourceCopy_,current_,history_,currentLow_,historyLow_,nrInput8_,nrOutput8_,postOut_,flowLow_,motionTex_,depthTex_,maskTex_,nrControlMaskTex_;Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> sourceSrv_,currentSrv_,historySrv_,currentLowSrv_,historyLowSrv_,nrInput8Srv_,nrOutput8Srv_,postSrv_,flowSrv_,motionSrv_,depthSrv_,maskSrv_,nrControlMaskSrv_;Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> currentUav_,historyUav_,currentLowUav_,historyLowUav_,nrInput8Uav_,nrOutput8Uav_,postUav_,flowUav_,motionUav_,depthUav_,maskUav_,nrControlMaskUav_;
 struct SourceSrvCacheEntry{Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;};
 struct BlitRtvCacheEntry{Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv;};
 std::array<SourceSrvCacheEntry,4> directSourceSrvs_{};std::array<BlitRtvCacheEntry,4> blitRtvs_{};std::uint32_t directSourceCursor_{},blitRtvCursor_{};
 NvOpticalFlowD3D11 nvof_;
 D3D11GuideExtractor guideExtractor_;
 bool forceResetNext_{true}; MotionRoute lastMotionRoute_{MotionRoute::Zero}; bool motionRouteValid_{}; bool hlslHistoryValid_{};
 std::wstring moduleDir_,runtime_;std::uint32_t width_{},height_{};DXGI_FORMAT backFormat_{DXGI_FORMAT_UNKNOWN};bool hasHistory_{};BackendMode backendMode_{BackendMode::Passthrough};bool attemptUnsupportedCached_{};neural::Backend* backend_{};
 bool backendFallback_{};std::wstring backendFallbackMessage_;std::int32_t backendFallbackResult_{};std::uint32_t backendFallbackRequiredTags_{},backendFallbackMissingTag_{};RuntimeFeatureDiagnostics backendFallbackFeature_{};PipelineStage backendFallbackFailureStage_{PipelineStage::None};std::uint64_t backendFallbackStageMask_{};NeuralExecutionApi backendFallbackNeuralApi_{NeuralExecutionApi::None};
};
}
