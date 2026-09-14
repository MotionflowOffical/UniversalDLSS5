#include "canonical_surface.hpp"
namespace udlss::compat {
bool CanonicalSurface::ensure(ID3D11Device* device,std::uint32_t width,std::uint32_t height,DXGI_FORMAT format,UINT bindFlags,UINT miscFlags){
    if(!device||!width||!height||format==DXGI_FORMAT_UNKNOWN)return false;
    if(texture_&&device_.Get()==device&&width_==width&&height_==height&&format_==format&&bindFlags_==bindFlags&&miscFlags_==miscFlags)return true;
    reset();
    D3D11_TEXTURE2D_DESC d{};d.Width=width;d.Height=height;d.MipLevels=1;d.ArraySize=1;d.Format=format;d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=bindFlags;d.MiscFlags=miscFlags;
    if(FAILED(device->CreateTexture2D(&d,nullptr,&texture_)))return false;
    device_=device;width_=width;height_=height;format_=format;bindFlags_=bindFlags;miscFlags_=miscFlags;++generation_;return true;
}
void CanonicalSurface::reset(){texture_.Reset();device_.Reset();width_=height_=0;format_=DXGI_FORMAT_UNKNOWN;bindFlags_=miscFlags_=0;}
}
