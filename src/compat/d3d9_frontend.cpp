#include "d3d9_frontend.hpp"
#include <d3d11.h>
#include <algorithm>
#include <cwchar>
using Microsoft::WRL::ComPtr;
namespace udlss::compat {
namespace {
// D3D9_RESOURCE_MISC: D3D9Ex exposes cross-API sharing through the pSharedHandle parameter on DEFAULT-pool render-target textures.
DXGI_FORMAT mapFormat(D3DFORMAT f){switch(f){case D3DFMT_A8R8G8B8:return DXGI_FORMAT_B8G8R8A8_UNORM;case D3DFMT_X8R8G8B8:return DXGI_FORMAT_B8G8R8X8_UNORM;case D3DFMT_A2R10G10B10:return DXGI_FORMAT_R10G10B10A2_UNORM;default:return DXGI_FORMAT_UNKNOWN;}}
void fail(RuntimeStatus&s,const wchar_t*m){s.failureStage=PipelineStage::GuideResourcesFailed;wcscpy_s(s.message,m);}
bool outputMatchesMonitor(IDXGIAdapter1* adapter,HMONITOR monitor){if(!adapter||!monitor)return false;for(UINT i=0;;++i){ComPtr<IDXGIOutput> out;if(adapter->EnumOutputs(i,&out)==DXGI_ERROR_NOT_FOUND)break;if(!out)continue;DXGI_OUTPUT_DESC od{};if(SUCCEEDED(out->GetDesc(&od))&&od.Monitor==monitor)return true;}return false;}
}
bool D3D9Frontend::createD3D11Device(RuntimeStatus& status){
    D3DDEVICE_CREATION_PARAMETERS cp{};if(FAILED(d9_->GetCreationParameters(&cp))){fail(status,L"Could not query D3D9 creation parameters");return false;}ComPtr<IDirect3D9> root;if(FAILED(d9_->GetDirect3D(&root))||!root){fail(status,L"Could not query the owning D3D9 interface");return false;}const HMONITOR monitor=root->GetAdapterMonitor(cp.AdapterOrdinal);
    ComPtr<IDXGIFactory1> factory;if(FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))){fail(status,L"Could not create DXGI factory for D3D9 adapter matching");return false;}ComPtr<IDXGIAdapter1> chosen;for(UINT i=0;;++i){ComPtr<IDXGIAdapter1>a;if(factory->EnumAdapters1(i,&a)==DXGI_ERROR_NOT_FOUND)break;if(outputMatchesMonitor(a.Get(),monitor)){chosen=a;break;}}
    static const D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_11_1,D3D_FEATURE_LEVEL_11_0,D3D_FEATURE_LEVEL_10_1,D3D_FEATURE_LEVEL_10_0};D3D_FEATURE_LEVEL level{};HRESULT hr=D3D11CreateDevice(chosen.Get(),chosen?D3D_DRIVER_TYPE_UNKNOWN:D3D_DRIVER_TYPE_HARDWARE,nullptr,0,levels,(UINT)_countof(levels),D3D11_SDK_VERSION,&d11_,&level,&d11Context_);if(hr==E_INVALIDARG)hr=D3D11CreateDevice(chosen.Get(),chosen?D3D_DRIVER_TYPE_UNKNOWN:D3D_DRIVER_TYPE_HARDWARE,nullptr,0,levels+1,(UINT)_countof(levels)-1,D3D11_SDK_VERSION,&d11_,&level,&d11Context_);
    if(FAILED(hr)||!d11_||!d11Context_){fail(status,L"Could not create D3D11 compatibility device on the D3D9 adapter");return false;}return true;
}
bool D3D9Frontend::tryInitializeExSharing(IDirect3DSurface9* backbuffer,RuntimeStatus& status){
    if(!d9ex_||!backbuffer||!d11_)return false;D3DSURFACE_DESC d{};if(FAILED(backbuffer->GetDesc(&d))||d.MultiSampleType!=D3DMULTISAMPLE_NONE)return false;const auto dx=mapFormat(d.Format);if(dx==DXGI_FORMAT_UNKNOWN)return false;
    HANDLE sharedHandle=nullptr;if(FAILED(d9ex_->CreateTexture(d.Width,d.Height,1,D3DUSAGE_RENDERTARGET,d.Format,D3DPOOL_DEFAULT,&sharedTexture9_,&sharedHandle))||!sharedTexture9_||!sharedHandle)return false;
    if(FAILED(sharedTexture9_->GetSurfaceLevel(0,&sharedSurface9_))||FAILED(d11_->OpenSharedResource(sharedHandle,IID_PPV_ARGS(&sharedTexture11_)))||!sharedTexture11_){sharedTexture9_.Reset();sharedSurface9_.Reset();sharedTexture11_.Reset();return false;}
    d9_->CreateQuery(D3DQUERYTYPE_EVENT,&eventQuery_);width_=d.Width;height_=d.Height;format_=dx;++generation_;return true;
}
bool D3D9Frontend::initialize(IDirect3DDevice9* device,RuntimeStatus& status){
    reset();if(!device){fail(status,L"D3D9 compatibility frontend received a null device");return false;}d9_=device;device->QueryInterface(IID_PPV_ARGS(&d9ex_));if(!createD3D11Device(status))return false;
    ComPtr<IDirect3DSurface9> bb;if(FAILED(d9_->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&bb))||!bb){fail(status,L"Could not acquire the D3D9 backbuffer during compatibility initialization");return false;}
    if(d9ex_&&tryInitializeExSharing(bb.Get(),status)){route_=RendererRoute::CompatD3D9Ex;return true;}
    sharedTexture9_.Reset();sharedSurface9_.Reset();sharedTexture11_.Reset();eventQuery_.Reset();route_=RendererRoute::CompatD3D9Classic;if(!legacyRing_.initialize(d9_.Get(),d11_.Get(),d11Context_.Get(),status))return false;return true;
}
bool D3D9Frontend::waitD3D9(RuntimeStatus& status){if(!eventQuery_)return true;if(FAILED(eventQuery_->Issue(D3DISSUE_END)))return false;const auto start=GetTickCount64();while(eventQuery_->GetData(nullptr,0,D3DGETDATA_FLUSH)==S_FALSE){if(GetTickCount64()-start>12){fail(status,L"Timed out synchronizing D3D9Ex shared color surface");return false;}YieldProcessor();}return true;}
bool D3D9Frontend::beginFrame(CompatFrame& frame,RuntimeStatus& status){
    if(!d9_||!d11_)return false;currentBackbuffer_.Reset();if(FAILED(d9_->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&currentBackbuffer_))||!currentBackbuffer_){fail(status,L"Could not acquire D3D9 backbuffer");return false;}
    status.api=GraphicsApi::D3D9;status.rendererRoute=route_;status.compatInterop=interop();
    if(route_==RendererRoute::CompatD3D9Ex){D3DSURFACE_DESC d{};currentBackbuffer_->GetDesc(&d);if(d.Width!=width_||d.Height!=height_||mapFormat(d.Format)!=format_){fail(status,L"D3D9Ex backbuffer changed; waiting for frontend recreation after Reset");return false;}if(FAILED(d9_->StretchRect(currentBackbuffer_.Get(),nullptr,sharedSurface9_.Get(),nullptr,D3DTEXF_NONE))||!waitD3D9(status))return false;frame.color=sharedTexture11_.Get();frame.width=width_;frame.height=height_;frame.format=format_;frame.colorEncoding=ColorEncoding::SdrSrgb;frame.generation=generation_;frame.legacyCopies=0;return true;}
    ID3D11Texture2D* color{};std::uint32_t w{},h{};DXGI_FORMAT fmt{};if(!legacyRing_.capture(currentBackbuffer_.Get(),color,w,h,fmt,status))return false;frame.color=color;frame.width=w;frame.height=h;frame.format=fmt;frame.colorEncoding=ColorEncoding::SdrSrgb;frame.generation=legacyRing_.generation();frame.legacyCopies=1;return true;
}
bool D3D9Frontend::endFrame(const CompatFrame&,RuntimeStatus& status){
    if(route_==RendererRoute::CompatD3D9Ex){d11Context_->Flush();if(!currentBackbuffer_||FAILED(d9_->StretchRect(sharedSurface9_.Get(),nullptr,currentBackbuffer_.Get(),nullptr,D3DTEXF_NONE))){fail(status,L"D3D9Ex shared processed-output copy failed");return false;}currentBackbuffer_.Reset();return true;}
    const bool ok=legacyRing_.present(currentBackbuffer_.Get(),status);currentBackbuffer_.Reset();return ok;
}
void D3D9Frontend::reset(){currentBackbuffer_.Reset();legacyRing_.reset();eventQuery_.Reset();sharedTexture11_.Reset();sharedSurface9_.Reset();sharedTexture9_.Reset();d11Context_.Reset();d11_.Reset();d9ex_.Reset();d9_.Reset();route_=RendererRoute::Unsupported;width_=height_=0;format_=DXGI_FORMAT_UNKNOWN;}
}
