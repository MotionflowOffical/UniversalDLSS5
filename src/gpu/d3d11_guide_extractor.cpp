#include "d3d11_guide_extractor.hpp"
#include "d3d11_resource_tracker.hpp"
#include "udlss/resource_extraction_policy.hpp"
#include <windows.h>
#include <filesystem>

using Microsoft::WRL::ComPtr;
namespace fs=std::filesystem;
namespace udlss::gpu {
namespace {
bool depthFormats(DXGI_FORMAT view,DXGI_FORMAT& resourceFmt,DXGI_FORMAT& srvFmt){
    switch(view){
    case DXGI_FORMAT_D32_FLOAT: case DXGI_FORMAT_R32_FLOAT: case DXGI_FORMAT_R32_TYPELESS:
        resourceFmt=DXGI_FORMAT_R32_TYPELESS;srvFmt=DXGI_FORMAT_R32_FLOAT;return true;
    case DXGI_FORMAT_D24_UNORM_S8_UINT: case DXGI_FORMAT_R24G8_TYPELESS: case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
        resourceFmt=DXGI_FORMAT_R24G8_TYPELESS;srvFmt=DXGI_FORMAT_R24_UNORM_X8_TYPELESS;return true;
    case DXGI_FORMAT_D16_UNORM: case DXGI_FORMAT_R16_TYPELESS: case DXGI_FORMAT_R16_UNORM:
        resourceFmt=DXGI_FORMAT_R16_TYPELESS;srvFmt=DXGI_FORMAT_R16_UNORM;return true;
    default:return false;
    }
}
bool exactTexture(ID3D11Texture2D* t,UINT w,UINT h,DXGI_FORMAT fmt){
    if(!t)return false;D3D11_TEXTURE2D_DESC d{};t->GetDesc(&d);return d.Width==w&&d.Height==h&&d.SampleDesc.Count==1&&d.Format==fmt;
}
std::wstring gameDir(){wchar_t p[32768]{};DWORD n=GetModuleFileNameW(nullptr,p,_countof(p));return n?fs::path(std::wstring(p,n)).parent_path().wstring():L"";}
}
D3D11GuideExtractor::~D3D11GuideExtractor(){if(adapter_)FreeLibrary((HMODULE)adapter_);}
bool D3D11GuideExtractor::initialize(ID3D11Device* d,ID3D11DeviceContext* c){device_=d;context_=c;return device_&&context_;}
void D3D11GuideExtractor::tryLoadAdapter(){
    if(adapter_||getFrame_)return;const auto dir=gameDir();if(dir.empty())return;
    const auto path=fs::path(dir)/L"UniversalDLSS5.GameGuides.dll";
    std::error_code ec;if(!fs::is_regular_file(path,ec))return;
    HMODULE m=LoadLibraryW(path.c_str());if(!m)return;
    auto fn=reinterpret_cast<GameGuidesGetFrameV1>(GetProcAddress(m,kGameGuidesExportV1));
    if(!fn){FreeLibrary(m);return;}adapter_=m;getFrame_=fn;
}
bool D3D11GuideExtractor::adoptDepth(ID3D11Texture2D* t,DXGI_FORMAT view,UINT w,UINT h,GuideProbeResult& out){
    if(!t)return false;D3D11_TEXTURE2D_DESC d{};t->GetDesc(&d);DXGI_FORMAT rf{},sf{};
    DepthCandidateMeta meta{d.Width,d.Height,d.SampleDesc.Count,depthFormats(view,rf,sf),true};
    if(!depthCandidateUsable(meta,w,h))return false;out.depth=t;out.depthViewFormat=view;return true;
}
void D3D11GuideExtractor::probe(ID3D11Texture2D* backbuffer,const Settings& settings,GuideProbeResult& out,RuntimeStatus& status){
    out={};if(!backbuffer||!device_||!context_)return;D3D11_TEXTURE2D_DESC bb{};backbuffer->GetDesc(&bb);
    if(settings.loadGameGuideAdapter){tryLoadAdapter();if(getFrame_){GameGuideFrameV1 raw{};if(getFrame_(device_.Get(),context_.Get(),backbuffer,&raw)&&raw.abi==kGameGuidesAbiV1&&raw.size>=sizeof(GameGuideFrameV1)){
        auto take=[&](void* p,ComPtr<ID3D11Texture2D>& dst){if(p)dst.Attach(static_cast<ID3D11Texture2D*>(p));};
        take(raw.depthTexture,out.depth);take(raw.motionTexture,out.motion);take(raw.controlMaskTexture,out.controlMask);take(raw.normalsTexture,out.normals);take(raw.albedoTexture,out.albedo);
        out.depthViewFormat=(DXGI_FORMAT)raw.depthFormat;out.depthConventionKnown=(raw.flags&GameGuide_DepthConventionKnown)!=0;if(out.depthConventionKnown)out.depthInverted=(raw.flags&GameGuide_DepthInverted)!=0;out.cameraCut=(raw.flags&GameGuide_CameraCut)!=0;out.motionConventionValid=(raw.flags&GameGuide_MotionPixelCurrentToPrevious)!=0;out.controlMaskConventionValid=(raw.flags&GameGuide_ControlMaskIsNrApplication)!=0;out.motionScaleX=raw.motionScaleX;out.motionScaleY=raw.motionScaleY;out.fromAdapter=true;out.provider=raw.providerName[0]?raw.providerName:L"UniversalDLSS5.GameGuides.dll";
        if(out.depth&&!adoptDepth(out.depth.Get(),out.depthViewFormat,bb.Width,bb.Height,out))out.depth.Reset();
    }}}
    if(!out.depth&&settings.useGameDepth){
        const auto tracked=globalD3D11ResourceTracker().bestDepthCandidate(device_.Get(),bb.Width,bb.Height);
        if(tracked.texture&&adoptDepth(tracked.texture.Get(),tracked.viewFormat,bb.Width,bb.Height,out)){
            out.depthConventionKnown=tracked.clearKnown&&(tracked.clearDepth<=0.25f||tracked.clearDepth>=0.75f);
            out.depthInverted=tracked.clearKnown?tracked.clearDepth<=0.25f:true;
            out.provider=L"Tracked dominant depth-stencil";
        }
    }
    if(!out.depth&&settings.useGameDepth){ComPtr<ID3D11RenderTargetView> rtv;ComPtr<ID3D11DepthStencilView> dsv;ID3D11RenderTargetView* r{};ID3D11DepthStencilView* d{};context_->OMGetRenderTargets(1,&r,&d);rtv.Attach(r);dsv.Attach(d);if(dsv){D3D11_DEPTH_STENCIL_VIEW_DESC vd{};dsv->GetDesc(&vd);ComPtr<ID3D11Resource> res;dsv->GetResource(&res);ComPtr<ID3D11Texture2D> tex;if(res&&SUCCEEDED(res.As(&tex))&&adoptDepth(tex.Get(),vd.Format,bb.Width,bb.Height,out)){out.depthInverted=true;out.depthConventionKnown=false;out.provider=L"D3D11 current depth-stencil (auto assumes reversed-Z)";}}}
    if(out.depth)wcsncpy_s(status.depthName,out.provider.c_str(),_TRUNCATE);else wcscpy_s(status.depthName,L"Synthetic far depth");
    if(out.fromAdapter)wcsncpy_s(status.guideAdapterName,out.provider.empty()?L"UniversalDLSS5.GameGuides.dll":out.provider.c_str(),_TRUNCATE);
    if(out.fromAdapter){
        std::wstring fields;
        auto add=[&](const wchar_t* name,bool present){if(!present)return;if(!fields.empty())fields+=L", ";fields+=name;};
        add(L"depth",!!out.depth);add(L"motion",!!out.motion);add(L"control-mask",!!out.controlMask);add(L"normals",!!out.normals);add(L"albedo",!!out.albedo);
        if(fields.empty())fields=L"adapter loaded; no guide resources this frame";
        wcsncpy_s(status.guideFields,fields.c_str(),_TRUNCATE);
    }else if(out.depth){
        wcscpy_s(status.guideFields,L"D3D11 bound depth-stencil");
    }else{
        wcscpy_s(status.guideFields,L"synthetic guides only");
    }
}
bool D3D11GuideExtractor::prepareDepth(const GuideProbeResult& p,UINT w,UINT h,ID3D11ShaderResourceView** srv,bool& inverted,RuntimeStatus& status){
    if(srv)*srv=nullptr;if(!p.depth)return false;D3D11_TEXTURE2D_DESC src{};p.depth->GetDesc(&src);DXGI_FORMAT resourceFmt{},srvFmt{};if(!depthFormats(p.depthViewFormat,resourceFmt,srvFmt))return false;
    if(src.Width!=w||src.Height!=h||src.SampleDesc.Count!=1)return false;
    if(!depthCopy_||depthCopyWidth_!=w||depthCopyHeight_!=h||depthCopySource_!=p.depthViewFormat){
        depthCopy_.Reset();depthSrv_.Reset();D3D11_TEXTURE2D_DESC d=src;d.Format=resourceFmt;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;d.CPUAccessFlags=0;d.Usage=D3D11_USAGE_DEFAULT;d.MiscFlags=0;
        if(FAILED(device_->CreateTexture2D(&d,nullptr,&depthCopy_)))return false;D3D11_SHADER_RESOURCE_VIEW_DESC sd{};sd.Format=srvFmt;sd.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;sd.Texture2D.MipLevels=1;
        if(FAILED(device_->CreateShaderResourceView(depthCopy_.Get(),&sd,&depthSrv_))){depthCopy_.Reset();return false;}depthCopyWidth_=w;depthCopyHeight_=h;depthCopySource_=p.depthViewFormat;
    }
    context_->CopyResource(depthCopy_.Get(),p.depth.Get());inverted=p.depthInverted;if(srv){*srv=depthSrv_.Get();if(*srv)(*srv)->AddRef();}return true;
}
bool D3D11GuideExtractor::copyMotion(const GuideProbeResult& p,ID3D11Texture2D* dst,UINT w,UINT h){if(!p.motion||!p.motionConventionValid||!exactTexture(p.motion.Get(),w,h,DXGI_FORMAT_R16G16_FLOAT)||!dst)return false;context_->CopyResource(dst,p.motion.Get());return true;}
bool D3D11GuideExtractor::copyControlMask(const GuideProbeResult& p,ID3D11Texture2D* dst,UINT w,UINT h){if(!p.controlMask||!p.controlMaskConventionValid||!exactTexture(p.controlMask.Get(),w,h,DXGI_FORMAT_R8G8B8A8_UNORM)||!dst)return false;context_->CopyResource(dst,p.controlMask.Get());return true;}
}
