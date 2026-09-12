#include "d3d12_on12.hpp"
using Microsoft::WRL::ComPtr;
namespace udlss::gpu {
bool D3D12On12Pipeline::initialize(ID3D12CommandQueue*q,const std::wstring&dir,RuntimeStatus&st){if(!q)return false;queue_=q;moduleDir_=dir;if(FAILED(q->GetDevice(IID_PPV_ARGS(&d12_)))){wcscpy_s(st.message,L"Could not obtain D3D12 device from swapchain queue");return false;}IUnknown* queues[]={q};D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_12_1,D3D_FEATURE_LEVEL_12_0,D3D_FEATURE_LEVEL_11_1,D3D_FEATURE_LEVEL_11_0};D3D_FEATURE_LEVEL chosen{};HRESULT hr=D3D11On12CreateDevice(d12_.Get(),D3D11_CREATE_DEVICE_BGRA_SUPPORT,levels,_countof(levels),queues,1,0,&d11_,&ctx_,&chosen);if(FAILED(hr)||FAILED(d11_.As(&on12_))){wcscpy_s(st.message,L"D3D11On12CreateDevice failed");return false;}pipeline_.setNativeD3D12(d12_.Get(),queue_.Get());return pipeline_.initialize(d11_.Get(),ctx_.Get(),dir,st);}
void D3D12On12Pipeline::reset(){pipeline_.reset();wrappedBackbuffers_.clear();width_=height_=0;format_=DXGI_FORMAT_UNKNOWN;}
void D3D12On12Pipeline::retryNeural(){pipeline_.retryNeural();}
bool D3D12On12Pipeline::process(IDXGISwapChain*swap,const Settings&s,const std::wstring&runtime,RuntimeStatus&st){
 if(!swap||!on12_||!s.allowD3D11On12)return false;
 ComPtr<IDXGISwapChain3> s3;if(FAILED(swap->QueryInterface(IID_PPV_ARGS(&s3))))return false;
 const UINT i=s3->GetCurrentBackBufferIndex();
 ComPtr<ID3D12Resource> bb;if(FAILED(swap->GetBuffer(i,IID_PPV_ARGS(&bb))))return false;
 const auto desc=bb->GetDesc();
 const bool lifecycleChanged=width_ && (width_!=desc.Width || height_!=desc.Height || format_!=desc.Format);
 const bool resourceChanged=wrappedBackbuffers_.find(bb.Get())==wrappedBackbuffers_.end();
 if(shouldResetTemporalHistoryForBackbuffer(resourceChanged,lifecycleChanged)){
  pipeline_.reset();wrappedBackbuffers_.clear();
 }
 width_=(UINT)desc.Width;height_=desc.Height;format_=desc.Format;
 auto it=wrappedBackbuffers_.find(bb.Get());
 if(it==wrappedBackbuffers_.end()){
  WrappedBackbuffer entry{};entry.native=bb;
  D3D11_RESOURCE_FLAGS flags{};flags.BindFlags=D3D11_BIND_RENDER_TARGET;
  const HRESULT hr=on12_->CreateWrappedResource(bb.Get(),&flags,D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_PRESENT,IID_PPV_ARGS(&entry.wrapped));
  if(FAILED(hr)){wcscpy_s(st.message,L"CreateWrappedResource(backbuffer) failed");return false;}
  it=wrappedBackbuffers_.emplace(bb.Get(),std::move(entry)).first;
 }
 ID3D11Resource*r=it->second.wrapped.Get();
 on12_->AcquireWrappedResources(&r,1);
 const bool ok=pipeline_.process(it->second.wrapped.Get(),s,runtime,st);
 on12_->ReleaseWrappedResources(&r,1);
 ctx_->Flush();
 return ok;
}
}
