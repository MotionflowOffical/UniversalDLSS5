#pragma once
#include "d3d11_pipeline.hpp"
#include "udlss/d3d12_backbuffer_policy.hpp"
#include <d3d11on12.h>
#include <d3d11_4.h>
#include <dxgi1_6.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <vector>
#include <cstdint>
namespace udlss::gpu {
struct D3D12QueueWaitPoint {
 Microsoft::WRL::ComPtr<ID3D12Fence> fence;
 std::uint64_t value{};
 std::uintptr_t queueId{};
};
class D3D12On12Pipeline {
public:
 D3D12On12Pipeline()=default;
 ~D3D12On12Pipeline();
 bool initialize(ID3D12CommandQueue* queue,const std::wstring& moduleDir,RuntimeStatus& status,bool recoveryMode=false);
 bool process(IDXGISwapChain* swap,const Settings& settings,const std::wstring& runtime,RuntimeStatus& status,const SwapchainColorContext* colorContext=nullptr,bool synchronizeBeforePresent=false,const std::vector<D3D12QueueWaitPoint>* recoveryWaits=nullptr);
 void reset();
 void retryNeural();
private:
 struct CopyContext { Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator; Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> list; std::uint64_t fenceValue{}; };
 struct RecoveryGuideSurface {
  Microsoft::WRL::ComPtr<ID3D12Resource> resource12;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> texture11;
  UINT width{},height{};
  DXGI_FORMAT resourceFormat{DXGI_FORMAT_UNKNOWN};
  DXGI_FORMAT viewFormat{DXGI_FORMAT_UNKNOWN};
 };
 bool ensureCopyInfrastructure(RuntimeStatus& status);
 bool ensureOwnedColor(const D3D12_RESOURCE_DESC& backbufferDesc,RuntimeStatus& status);
 bool initializeRecoveryD3D11(RuntimeStatus& status);
 bool waitRecoveryD3D11(RuntimeStatus& status);
 bool waitCopyContext(CopyContext& context,RuntimeStatus& status);
 bool submitBackbufferCopy(ID3D12Resource* backbuffer,bool intoOwned,RuntimeStatus& status);
 bool stageRecoveryGuide(ID3D12Resource* source,D3D12_RESOURCE_STATES sourceState,DXGI_FORMAT viewFormat,bool depth,RecoveryGuideSurface& surface,RuntimeStatus& status);
 bool enqueueRecoveryWaits(const std::vector<D3D12QueueWaitPoint>* waits,RuntimeStatus& status);
 Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue_;
 Microsoft::WRL::ComPtr<ID3D12Device> d12_;
 Microsoft::WRL::ComPtr<ID3D11Device> d11_;
 Microsoft::WRL::ComPtr<ID3D11DeviceContext> ctx_;
 Microsoft::WRL::ComPtr<ID3D11On12Device> on12_;
 Microsoft::WRL::ComPtr<ID3D11Device1> recoveryD11Device1_;
 Microsoft::WRL::ComPtr<ID3D11Device5> recoveryD11Device5_;
 Microsoft::WRL::ComPtr<ID3D11DeviceContext4> recoveryD11Context4_;
 Microsoft::WRL::ComPtr<ID3D11Fence> recoveryD11Fence_;
 HANDLE recoveryD11FenceEvent_{};
 std::uint64_t recoveryD11FenceValue_{1};
 Microsoft::WRL::ComPtr<ID3D12Resource> ownedColor12_;
 Microsoft::WRL::ComPtr<ID3D11Texture2D> ownedColor11_;
 RecoveryGuideSurface recoveryDepthGuide_{},recoveryMotionGuide_{};
 CopyContext preCopy_{},postCopy_{},guideCopy_{};
 Microsoft::WRL::ComPtr<ID3D12Fence> copyFence_;
 HANDLE copyFenceEvent_{};
 std::uint64_t nextCopyFenceValue_{1};
 D3D11Pipeline pipeline_;
 std::wstring moduleDir_;
 UINT width_{},height_{};
 DXGI_FORMAT format_{DXGI_FORMAT_UNKNOWN};
 bool recoveryMode_{};
};
}
