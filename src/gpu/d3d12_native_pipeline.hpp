#pragma once
#include "game_temporal_guides.hpp"
#include "swapchain_color.hpp"
#include "../neural/backend.hpp"
#include <filesystem>
#include <array>
#include <memory>
namespace udlss::gpu {
// Same-queue D3D12 path. It admits completed owned snapshots only; unsupported
// inputs retain the existing D3D11 path. Host input/output never cross D3D11.
class D3D12NativePipeline {
public:
 D3D12NativePipeline();
 ~D3D12NativePipeline();
 bool eligible(const D3D12_RESOURCE_DESC&,const CapturedGameGuides&,const Settings&) const;
 bool process(ID3D12Device*,ID3D12CommandQueue*,ID3D12Resource*,const CapturedGameGuides&,const Settings&,const std::wstring& runtime,const std::wstring& moduleDir,RuntimeStatus&,const SwapchainColorContext*,const std::shared_ptr<neural::Backend>& sharedHost={});
 void reset();
 void retryNeural();
 void invalidateHistory(){historyValid_=false;}
 void releaseObsoleteHost(const Settings&,const std::wstring& runtime);
 static D3D12_RESOURCE_BARRIER barrier(ID3D12Resource*,D3D12_RESOURCE_STATES,D3D12_RESOURCE_STATES);
private:
 struct Retirement;
 std::unique_ptr<Retirement> retirement_;
 void retireInFlight();
 struct Texture {Microsoft::WRL::ComPtr<ID3D12Resource> r;DXGI_FORMAT format{};D3D12_RESOURCE_STATES state{D3D12_RESOURCE_STATE_COMMON};};
 bool initialize(ID3D12Device*,ID3D12CommandQueue*,DXGI_FORMAT,const std::wstring&,RuntimeStatus&);
 bool resources(const D3D12_RESOURCE_DESC&);
 bool begin();bool submit();bool finish();
 bool make(Texture&,UINT,UINT,DXGI_FORMAT,bool shared=false);
 void state(Texture&,D3D12_RESOURCE_STATES);
 void bind(std::initializer_list<Texture*>,Texture*,bool graphics=false);
 void dispatch(unsigned,std::initializer_list<Texture*>,Texture&,const void*,UINT);
 Microsoft::WRL::ComPtr<ID3D12Device> device_;
 Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue_;
 Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator_;
 Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> list_;
 Microsoft::WRL::ComPtr<ID3D12RootSignature> root_;
 Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptors_,rtv_;
 std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>,6> compute_;
 Microsoft::WRL::ComPtr<ID3D12PipelineState> blit_;
 Microsoft::WRL::ComPtr<ID3D12Fence> fence_;HANDLE event_{};UINT64 value_{};
 Texture source_,current_,history_,input_,output_,motion_,depth_,mask_,post_;
 CapturedGameGuides guideHold_{};
 TemporalFrame previousTemporal_{};
 std::shared_ptr<neural::Backend> backend_;
 std::wstring runtime_;UINT width_{},height_{},stride_{},cursor_{};DXGI_FORMAT format_{};
 bool historyValid_{},failed_{},unsupportedCached_{};
};
}
