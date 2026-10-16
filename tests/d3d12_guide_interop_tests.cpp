#include "../src/gpu/shared_guide_texture.hpp"
#include <d3d11_1.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <iostream>
using Microsoft::WRL::ComPtr;

int main(){
    ComPtr<IDXGIFactory4> factory;
    if(FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))return 1;
    ComPtr<IDXGIAdapter1> adapter;
    ComPtr<ID3D12Device> d12;
    for(UINT i=0;factory->EnumAdapters1(i,&adapter)!=DXGI_ERROR_NOT_FOUND;++i){
        DXGI_ADAPTER_DESC1 desc{};adapter->GetDesc1(&desc);
        if(!(desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)&&SUCCEEDED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&d12))))break;
        adapter.Reset();
    }
    if(!d12){std::cout<<"SKIP: no D3D12 hardware adapter\n";return 77;}
    ComPtr<ID3D11Device> d11;
    if(FAILED(D3D11CreateDevice(adapter.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&d11,nullptr,nullptr)))return 1;
    ComPtr<ID3D11Device1> d11v1;if(FAILED(d11.As(&d11v1)))return 1;
    bool ok=true;
    // Cyberpunk's depth and RGBA velocity, plus the other typed guide families.
    for(auto format:{DXGI_FORMAT_R32_FLOAT,DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_R16_UNORM,DXGI_FORMAT_R16G16_FLOAT}){
        D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width=64;desc.Height=32;desc.DepthOrArraySize=1;desc.MipLevels=1;desc.Format=format;desc.SampleDesc.Count=1;
        desc.Flags=udlss::gpu::sharedGuideTextureFlags(format);
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;heap.CreationNodeMask=heap.VisibleNodeMask=1;
        ComPtr<ID3D12Resource> resource;
        HRESULT hr=d12->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_SHARED,&desc,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&resource));
        HANDLE handle{};
        if(SUCCEEDED(hr))hr=d12->CreateSharedHandle(resource.Get(),nullptr,GENERIC_ALL,nullptr,&handle);
        ComPtr<ID3D11Texture2D> texture;
        if(SUCCEEDED(hr))hr=d11v1->OpenSharedResource1(handle,IID_PPV_ARGS(&texture));
        if(handle)CloseHandle(handle);
        ComPtr<ID3D11ShaderResourceView> srv;
        if(SUCCEEDED(hr))hr=d11->CreateShaderResourceView(texture.Get(),nullptr,&srv);
        if(FAILED(hr)){std::cerr<<"Guide format "<<(int)format<<" D3D12 -> D3D11 SRV failed: 0x"<<std::hex<<(unsigned)hr<<std::dec<<"\n";ok=false;}
        else std::cout<<"Guide format "<<(int)format<<" shared and shader-readable\n";
    }
    return ok?0:1;
}
