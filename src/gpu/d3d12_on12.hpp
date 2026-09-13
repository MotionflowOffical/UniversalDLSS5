#pragma once
#include "d3d11_pipeline.hpp"
#include "udlss/d3d12_backbuffer_policy.hpp"
#include <d3d11on12.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
namespace udlss::gpu {
class D3D12On12Pipeline {
public:
 D3D12On12Pipeline()=default;
 ~D3D12On12Pipeline();
 bool initialize(ID3D12CommandQueue* queue,const std::wstring& moduleDir,RuntimeStatus& status);
 bool process(IDXGISwapChain* swap,const Settings& settings,const std::wstring& runtime,RuntimeStatus& status,const SwapchainColorContext* colorContext=nullptr);
 void reset();
 void retryNeural();
private:
 struct CopyContext { Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator; Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> list; std::uint64_t fenceValue{}; };
 bool ensureCopyInfrastructure(RuntimeStatus& status);
 bool ensureOwnedColor(const D3D12_RESOURCE_DESC& backbufferDesc,RuntimeStatus& status);
 bool waitCopyContext(CopyContext& context,RuntimeStatus& status);
 bool submitBackbufferCopy(ID3D12Resource* backbuffer,bool intoOwned,RuntimeStatus& status);
 Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue_;
 Microsoft::WRL::ComPtr<ID3D12Device> d12_;
 Microsoft::WRL::ComPtr<ID3D11Device> d11_;
 Microsoft::WRL::ComPtr<ID3D11DeviceContext> ctx_;
 Microsoft::WRL::ComPtr<ID3D11On12Device> on12_;
 Microsoft::WRL::ComPtr<ID3D12Resource> ownedColor12_;
 Microsoft::WRL::ComPtr<ID3D11Texture2D> ownedColor11_;
 CopyContext preCopy_{},postCopy_{};
 Microsoft::WRL::ComPtr<ID3D12Fence> copyFence_;
 HANDLE copyFenceEvent_{};
 std::uint64_t nextCopyFenceValue_{1};
 D3D11Pipeline pipeline_;
 std::wstring moduleDir_;
 UINT width_{},height_{};
 DXGI_FORMAT format_{DXGI_FORMAT_UNKNOWN};
};
}
