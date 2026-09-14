#include "legacy_copy_ring.hpp"
#include <algorithm>
#include <cstring>
#include <cwchar>
using Microsoft::WRL::ComPtr;
namespace udlss::compat {
namespace {
DXGI_FORMAT mapFormat(D3DFORMAT f){switch(f){case D3DFMT_A8R8G8B8:return DXGI_FORMAT_B8G8R8A8_UNORM;case D3DFMT_X8R8G8B8:return DXGI_FORMAT_B8G8R8X8_UNORM;case D3DFMT_A2R10G10B10:return DXGI_FORMAT_R10G10B10A2_UNORM;default:return DXGI_FORMAT_UNKNOWN;}}
void fail(RuntimeStatus& s,const wchar_t* m){s.failureStage=PipelineStage::GuideResourcesFailed;wcscpy_s(s.message,m);}
}
bool LegacyCopyRing::initialize(IDirect3DDevice9* d3d9,ID3D11Device* d3d11,ID3D11DeviceContext* context,RuntimeStatus& status){reset();if(!d3d9||!d3d11||!context){fail(status,L"Classic D3D9 transfer ring requires D3D9 and D3D11 devices");return false;}d9_=d3d9;d11_=d3d11;context_=context;return true;}
bool LegacyCopyRing::ensure(IDirect3DSurface9* backbuffer,RuntimeStatus& status){
    if(!backbuffer||!d9_||!d11_)return false;D3DSURFACE_DESC d{};if(FAILED(backbuffer->GetDesc(&d))){fail(status,L"Could not inspect classic D3D9 backbuffer");return false;}const auto fmt=mapFormat(d.Format);if(fmt==DXGI_FORMAT_UNKNOWN){fail(status,L"Classic D3D9 compatibility supports 32-bit RGB/RGBA swapchain formats only");return false;}
    if(canonical_&&desc_.Width==d.Width&&desc_.Height==d.Height&&desc_.Format==d.Format&&desc_.MultiSampleType==d.MultiSampleType)return true;
    resolve9_.Reset();canonical_.Reset();for(auto& slot:slots_){slot.systemMemory.Reset();slot.gpuUpload.Reset();slot.readback.Reset();}
    for(auto& slot:slots_){
        if(FAILED(d9_->CreateOffscreenPlainSurface(d.Width,d.Height,d.Format,D3DPOOL_SYSTEMMEM,&slot.systemMemory,nullptr))){fail(status,L"Could not allocate classic D3D9 system-memory transfer surface");reset();return false;}
        if(FAILED(d9_->CreateOffscreenPlainSurface(d.Width,d.Height,d.Format,D3DPOOL_DEFAULT,&slot.gpuUpload,nullptr))){fail(status,L"Could not allocate classic D3D9 GPU upload surface");reset();return false;}
    }
    if(d.MultiSampleType!=D3DMULTISAMPLE_NONE){if(FAILED(d9_->CreateRenderTarget(d.Width,d.Height,d.Format,D3DMULTISAMPLE_NONE,0,FALSE,&resolve9_,nullptr))){fail(status,L"Could not allocate D3D9 MSAA resolve target");reset();return false;}}
    D3D11_TEXTURE2D_DESC cd{};cd.Width=d.Width;cd.Height=d.Height;cd.MipLevels=1;cd.ArraySize=1;cd.Format=fmt;cd.SampleDesc.Count=1;cd.Usage=D3D11_USAGE_DEFAULT;cd.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET|D3D11_BIND_UNORDERED_ACCESS;
    if(FAILED(d11_->CreateTexture2D(&cd,nullptr,&canonical_))){fail(status,L"Could not allocate full-resolution D3D11 compatibility color surface");reset();return false;}
    D3D11_TEXTURE2D_DESC rd=cd;rd.Usage=D3D11_USAGE_STAGING; /* UDLSS_LEGACY_D3D9_TRANSFER */ rd.BindFlags=0;rd.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    for(auto& slot:slots_){if(FAILED(d11_->CreateTexture2D(&rd,nullptr,&slot.readback))){fail(status,L"Could not allocate classic D3D9 processed-output transfer surface");reset();return false;}}
    desc_=d;dxgiFormat_=fmt;cursor_={};++generation_;return true;
}
bool LegacyCopyRing::capture(IDirect3DSurface9* backbuffer,ID3D11Texture2D*& canonical,std::uint32_t& width,std::uint32_t& height,DXGI_FORMAT& format,RuntimeStatus& status){
    canonical=nullptr;if(!ensure(backbuffer,status))return false;auto& slot=slots_[cursor_.current()];IDirect3DSurface9* source=backbuffer;
    if(resolve9_){if(FAILED(d9_->StretchRect(backbuffer,nullptr,resolve9_.Get(),nullptr,D3DTEXF_NONE))){fail(status,L"D3D9 MSAA resolve failed before legacy transfer");return false;}source=resolve9_.Get();}
    if(FAILED(d9_->GetRenderTargetData(source,slot.systemMemory.Get()))){ /* UDLSS_LEGACY_D3D9_TRANSFER */ fail(status,L"D3D9 GetRenderTargetData failed for the isolated legacy compatibility route");return false;}
    D3DLOCKED_RECT locked{};if(FAILED(slot.systemMemory->LockRect(&locked,nullptr,D3DLOCK_READONLY))){fail(status,L"Could not lock classic D3D9 transfer surface");return false;}context_->UpdateSubresource(canonical_.Get(),0,nullptr,locked.pBits,(UINT)locked.Pitch,0);slot.systemMemory->UnlockRect();
    canonical=canonical_.Get();width=desc_.Width;height=desc_.Height;format=dxgiFormat_;return true;
}
bool LegacyCopyRing::present(IDirect3DSurface9* backbuffer,RuntimeStatus& status){
    if(!backbuffer||!canonical_)return false;auto& slot=slots_[cursor_.current()];context_->CopyResource(slot.readback.Get(),canonical_.Get());context_->Flush();D3D11_MAPPED_SUBRESOURCE mapped{};
    if(FAILED(context_->Map(slot.readback.Get(),0,D3D11_MAP_READ,0,&mapped))){fail(status,L"Could not map classic D3D9 processed-output transfer surface");return false;}
    D3DLOCKED_RECT locked{};if(FAILED(slot.systemMemory->LockRect(&locked,nullptr,0))){context_->Unmap(slot.readback.Get(),0);fail(status,L"Could not lock D3D9 output transfer surface");return false;}
    const std::size_t rowBytes=(std::size_t)desc_.Width*4u;for(UINT y=0;y<desc_.Height;++y)std::memcpy((std::byte*)locked.pBits+(std::size_t)y*locked.Pitch,(const std::byte*)mapped.pData+(std::size_t)y*mapped.RowPitch,rowBytes);
    slot.systemMemory->UnlockRect();context_->Unmap(slot.readback.Get(),0);
    if(FAILED(d9_->UpdateSurface(slot.systemMemory.Get(),nullptr,slot.gpuUpload.Get(),nullptr))){fail(status,L"D3D9 UpdateSurface failed for processed legacy output");return false;}
    if(FAILED(d9_->StretchRect(slot.gpuUpload.Get(),nullptr,backbuffer,nullptr,D3DTEXF_NONE))){fail(status,L"D3D9 processed output copy-back failed");return false;}
    cursor_.advance();return true;
}
void LegacyCopyRing::reset(){resolve9_.Reset();canonical_.Reset();for(auto& slot:slots_){slot.systemMemory.Reset();slot.gpuUpload.Reset();slot.readback.Reset();}context_.Reset();d11_.Reset();d9_.Reset();desc_={};dxgiFormat_=DXGI_FORMAT_UNKNOWN;cursor_={};}
}
