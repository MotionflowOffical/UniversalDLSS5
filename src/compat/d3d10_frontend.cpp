#include "d3d10_frontend.hpp"
#include <d3d11.h>
#include <cwchar>
using Microsoft::WRL::ComPtr;
namespace udlss::compat {
namespace {
void setMessage(RuntimeStatus& status,const wchar_t* text,PipelineStage stage=PipelineStage::GuideResourcesFailed){status.failureStage=stage;wcscpy_s(status.message,text);}
}
bool D3D10Frontend::initialize(IDXGISwapChain* swapchain,RuntimeStatus& status){
    reset();if(!swapchain){setMessage(status,L"D3D10 compatibility frontend received a null swapchain");return false;}
    if(FAILED(swapchain->GetDevice(IID_PPV_ARGS(&d10_)))||!d10_){setMessage(status,L"DXGI swapchain is not backed by a D3D10 device",PipelineStage::SourceApiUnsupported);return false;}
    ComPtr<IDXGIDevice> dxgiDevice;if(FAILED(d10_.As(&dxgiDevice))){setMessage(status,L"D3D10 device does not expose IDXGIDevice");return false;}
    ComPtr<IDXGIAdapter> adapter;if(FAILED(dxgiDevice->GetAdapter(&adapter))){setMessage(status,L"Could not resolve the D3D10 adapter for D3D11 interop");return false;}
    static const D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_11_1,D3D_FEATURE_LEVEL_11_0,D3D_FEATURE_LEVEL_10_1,D3D_FEATURE_LEVEL_10_0};D3D_FEATURE_LEVEL chosen{};
    HRESULT hr=D3D11CreateDevice(adapter.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,0,levels,(UINT)_countof(levels),D3D11_SDK_VERSION,&d11_,&chosen,&d11Context_);
    if(hr==E_INVALIDARG)hr=D3D11CreateDevice(adapter.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,0,levels+1,(UINT)_countof(levels)-1,D3D11_SDK_VERSION,&d11_,&chosen,&d11Context_);
    if(FAILED(hr)||!d11_||!d11Context_){setMessage(status,L"Could not create same-adapter D3D11 device for D3D10 compatibility");return false;}
    swapchain_=swapchain;status.api=GraphicsApi::D3D10;status.rendererRoute=RendererRoute::CompatD3D10;status.compatInterop=CompatInterop::D3D10Shared;return true;
}
bool D3D10Frontend::ensureResources(ID3D10Texture2D* backbuffer,RuntimeStatus& status){
    if(!backbuffer||!d10_||!d11_)return false;D3D10_TEXTURE2D_DESC source{};backbuffer->GetDesc(&source);
    if(source.SampleDesc.Count!=1){setMessage(status,L"D3D10 compatibility currently requires a single-sample swapchain backbuffer; use application AA instead of swapchain MSAA");return false;}
    if(shared10_&&width_==source.Width&&height_==source.Height&&format_==source.Format)return true;
    shared10_.Reset();shared11_.Reset();mutex10_.Reset();mutex11_.Reset();
    D3D10_TEXTURE2D_DESC desc=source;desc.MipLevels=1;desc.ArraySize=1;desc.Usage=D3D10_USAGE_DEFAULT;desc.BindFlags=D3D10_BIND_RENDER_TARGET|D3D10_BIND_SHADER_RESOURCE;desc.CPUAccessFlags=0;desc.MiscFlags=D3D10_RESOURCE_MISC_SHARED_KEYEDMUTEX;desc.SampleDesc.Count=1;desc.SampleDesc.Quality=0;
    if(FAILED(d10_->CreateTexture2D(&desc,nullptr,&shared10_))){setMessage(status,L"D3D10 shared keyed-mutex texture creation failed");return false;}
    ComPtr<IDXGIResource> dxgiResource;if(FAILED(shared10_.As(&dxgiResource))){setMessage(status,L"D3D10 shared texture does not expose IDXGIResource");return false;}HANDLE handle{};if(FAILED(dxgiResource->GetSharedHandle(&handle))||!handle){setMessage(status,L"D3D10 shared texture did not provide a shared handle");return false;}
    if(FAILED(d11_->OpenSharedResource(handle,IID_PPV_ARGS(&shared11_)))||!shared11_){setMessage(status,L"D3D11 OpenSharedResource failed for the D3D10 color surface");return false;}
    if(FAILED(shared10_.As(&mutex10_))||FAILED(shared11_.As(&mutex11_))){setMessage(status,L"D3D10/D3D11 shared surface does not expose IDXGIKeyedMutex");return false;}
    width_=source.Width;height_=source.Height;format_=source.Format;++generation_;return true;
}
bool D3D10Frontend::beginFrame(CompatFrame& frame,RuntimeStatus& status){
    if(!swapchain_||!d10_||!d11_)return false;currentBackbuffer_.Reset();
    if(FAILED(swapchain_->GetBuffer(0,IID_PPV_ARGS(&currentBackbuffer_)))||!currentBackbuffer_){setMessage(status,L"Could not acquire D3D10 swapchain backbuffer");return false;}
    if(!ensureResources(currentBackbuffer_.Get(),status))return false;
    if(FAILED(mutex10_->AcquireSync(0,8))){setMessage(status,L"D3D10 shared color surface was busy before compatibility copy");return false;}
    d10_->CopyResource(shared10_.Get(),currentBackbuffer_.Get());mutex10_->ReleaseSync(1);
    if(FAILED(mutex11_->AcquireSync(1,8))){setMessage(status,L"D3D11 could not acquire the D3D10 shared color surface");return false;}
    d11Locked_=true;frame.color=shared11_.Get();frame.width=width_;frame.height=height_;frame.format=format_;frame.generation=generation_;frame.legacyCopies=0;return true;
}
bool D3D10Frontend::endFrame(const CompatFrame&,RuntimeStatus& status){
    if(!d11Locked_)return false;mutex11_->ReleaseSync(2);d11Locked_=false;
    if(FAILED(mutex10_->AcquireSync(2,8))){setMessage(status,L"D3D10 could not reacquire processed compatibility output");return false;}
    if(currentBackbuffer_)d10_->CopyResource(currentBackbuffer_.Get(),shared10_.Get());mutex10_->ReleaseSync(0);currentBackbuffer_.Reset();return true;
}
void D3D10Frontend::reset(){if(d11Locked_&&mutex11_){mutex11_->ReleaseSync(0);d11Locked_=false;}currentBackbuffer_.Reset();mutex11_.Reset();mutex10_.Reset();shared11_.Reset();shared10_.Reset();d11Context_.Reset();d11_.Reset();d10_.Reset();swapchain_.Reset();width_=height_=0;format_=DXGI_FORMAT_UNKNOWN;}
}
