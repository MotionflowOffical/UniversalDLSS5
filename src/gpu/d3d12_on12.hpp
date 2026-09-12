#pragma once
#include "d3d11_pipeline.hpp"
#include "udlss/d3d12_backbuffer_policy.hpp"
#include <d3d11on12.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <unordered_map>
namespace udlss::gpu {
class D3D12On12Pipeline {
public:
 bool initialize(ID3D12CommandQueue* queue,const std::wstring& moduleDir,RuntimeStatus& status);
 bool process(IDXGISwapChain* swap,const Settings& settings,const std::wstring& runtime,RuntimeStatus& status);
 void reset();
 void retryNeural();
private:
 struct WrappedBackbuffer { Microsoft::WRL::ComPtr<ID3D12Resource> native; Microsoft::WRL::ComPtr<ID3D11Texture2D> wrapped; };
 Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue_;Microsoft::WRL::ComPtr<ID3D12Device> d12_;Microsoft::WRL::ComPtr<ID3D11Device> d11_;Microsoft::WRL::ComPtr<ID3D11DeviceContext> ctx_;Microsoft::WRL::ComPtr<ID3D11On12Device> on12_;std::unordered_map<void*,WrappedBackbuffer> wrappedBackbuffers_;D3D11Pipeline pipeline_;std::wstring moduleDir_;UINT width_{},height_{};DXGI_FORMAT format_{DXGI_FORMAT_UNKNOWN};
};
}
