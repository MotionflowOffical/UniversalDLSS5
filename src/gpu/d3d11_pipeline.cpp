#include "d3d11_pipeline.hpp"
#include "d3d11_resource_tracker.hpp"
#include "d3d11_camera_tracker.hpp"
#include "udlss/motion_route_policy.hpp"
#include "udlss/neural_scheduler_policy.hpp"
#include "udlss/camera_motion_math.hpp"
#include <d3dcompiler.h>
#include <filesystem>
#include <array>
#include <chrono>
#include <cstring>
#include <algorithm>

using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;

namespace udlss::gpu {
namespace {
bool compileFile(const fs::path& p,const char* entry,const char* target,ComPtr<ID3DBlob>& blob,std::wstring& err){
    ComPtr<ID3DBlob> e; UINT flags=D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    flags|=D3DCOMPILE_DEBUG|D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    flags|=D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif
    const HRESULT hr=D3DCompileFromFile(p.c_str(),nullptr,D3D_COMPILE_STANDARD_FILE_INCLUDE,entry,target,flags,0,&blob,&e);
    if(FAILED(hr)){
        if(e){std::string s((char*)e->GetBufferPointer(),e->GetBufferSize());err.assign(s.begin(),s.end());}
        else err=L"D3DCompileFromFile failed";
        return false;
    }
    return true;
}
template<class T> bool makeSrv(ID3D11Device*d,ID3D11Texture2D*t,ComPtr<T>&o){return SUCCEEDED(d->CreateShaderResourceView(t,nullptr,&o));}
template<class T> bool makeUav(ID3D11Device*d,ID3D11Texture2D*t,ComPtr<T>&o){return SUCCEEDED(d->CreateUnorderedAccessView(t,nullptr,&o));}
struct NativeMotionParams {
    std::uint32_t width{},height{},encoding{},invertY{};
    float motionScale{1.0f};
    float inputToPixelScaleX{1.0f};
    float inputToPixelScaleY{1.0f};
    float pad{};
};
struct CameraMotionParams {
    float currentClipToPreviousClip[16]{};
    std::uint32_t width{},height{};
    float motionScaleX{1.0f},motionScaleY{1.0f};
};
Matrix4 matrixFromArray(const std::array<float,16>& a){
    Matrix4 m{}; for(int r=0;r<4;r++)for(int c=0;c<4;c++)m.m[r][c]=a[r*4+c]; return m;
}
bool isSrgbFormat(DXGI_FORMAT format){
    return format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB ||
           format==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB ||
           format==DXGI_FORMAT_B8G8R8X8_UNORM_SRGB;
}
MotionRoute fallbackMotionRoute(MotionSource source,bool nvofAvailable){
    if(nvofAvailable) return MotionRoute::Nvof;
    return source==MotionSource::SynthesizedOpticalFlow ? MotionRoute::Hlsl : MotionRoute::Zero;
}
}

D3D11Pipeline::~D3D11Pipeline(){ if(backend_) neural::destroyBackend(backend_); }

void D3D11Pipeline::setNativeD3D12(ID3D12Device* device,ID3D12CommandQueue* queue){
    nativeD12_=device; nativeQueue12_=queue;
}

void D3D11Pipeline::releaseViews(){
    sourceCopy_.Reset(); current_.Reset(); history_.Reset(); currentLow_.Reset(); historyLow_.Reset(); nrInput8_.Reset(); nrOutput8_.Reset(); postOut_.Reset(); flowLow_.Reset(); motionTex_.Reset(); depthTex_.Reset(); maskTex_.Reset(); nrControlMaskTex_.Reset();
    sourceSrv_.Reset(); currentSrv_.Reset(); historySrv_.Reset(); currentLowSrv_.Reset(); historyLowSrv_.Reset(); nrInput8Srv_.Reset(); nrOutput8Srv_.Reset(); postSrv_.Reset(); flowSrv_.Reset(); motionSrv_.Reset(); depthSrv_.Reset(); maskSrv_.Reset(); nrControlMaskSrv_.Reset();
    currentUav_.Reset(); historyUav_.Reset(); currentLowUav_.Reset(); historyLowUav_.Reset(); nrInput8Uav_.Reset(); nrOutput8Uav_.Reset(); postUav_.Reset(); flowUav_.Reset(); motionUav_.Reset(); depthUav_.Reset(); maskUav_.Reset(); nrControlMaskUav_.Reset();
    width_=height_=0; backFormat_=DXGI_FORMAT_UNKNOWN; hasHistory_=false; motionRouteValid_=false; nvof_.resetHistory();
}

void D3D11Pipeline::reset(){ forceResetNext_=true; releaseViews(); if(backend_) backend_->reset(); }

void D3D11Pipeline::retryNeural(){
    if(backend_){ neural::destroyBackend(backend_); backend_=nullptr; }
    runtime_.clear();
    forceResetNext_=true;
    backendFallback_=false;
    backendFallbackMessage_.clear();
    backendFallbackResult_=0;
    backendFallbackRequiredTags_=0;
    backendFallbackMissingTag_=0;
    backendFallbackFeature_={};
    backendFallbackFailureStage_=PipelineStage::None; backendFallbackStageMask_=0; backendFallbackNeuralApi_=NeuralExecutionApi::None;
}

bool D3D11Pipeline::initialize(ID3D11Device*d,ID3D11DeviceContext*c,const std::wstring&dir,RuntimeStatus&st){
    device_=d; context_=c; moduleDir_=dir;
    if(FAILED(d->QueryInterface(IID_PPV_ARGS(&device1_)))||FAILED(c->QueryInterface(IID_PPV_ARGS(&context1_)))){
        wcscpy_s(st.message,L"D3D11.1 context state API unavailable"); return false;
    }
    D3D_FEATURE_LEVEL fl=d->GetFeatureLevel(),chosen{};
    if(FAILED(device1_->CreateDeviceContextState(0,&fl,1,D3D11_SDK_VERSION,__uuidof(ID3D11Device),&chosen,&ownState_))){
        wcscpy_s(st.message,L"CreateDeviceContextState failed"); return false;
    }
    auto makeDynamicCb=[&](UINT bytes,ComPtr<ID3D11Buffer>& out){D3D11_BUFFER_DESC bd{};bd.ByteWidth=(bytes+15u)&~15u;bd.Usage=D3D11_USAGE_DYNAMIC;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;bd.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;return SUCCEEDED(d->CreateBuffer(&bd,nullptr,&out));};
    if(!makeDynamicCb(sizeof(Params),cb_)||!makeDynamicCb(sizeof(NativeMotionParams),nativeMotionCb_)||!makeDynamicCb(sizeof(CameraMotionParams),cameraCb_)){wcscpy_s(st.message,L"constant buffer creation failed");return false;}
    D3D11_SAMPLER_DESC sd{};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sd.MaxLOD=D3D11_FLOAT32_MAX;
    if(FAILED(d->CreateSamplerState(&sd,&sampler_))) return false;
    if(!guideExtractor_.initialize(d,c)){wcscpy_s(st.message,L"guide extractor initialization failed");return false;}
    return createShaders(st);
}

bool D3D11Pipeline::createShaders(RuntimeStatus&st){
    std::wstring err;
    auto cs=[&](const wchar_t*file,ComPtr<ID3D11ComputeShader>&out){ComPtr<ID3DBlob>b;if(!compileFile(fs::path(moduleDir_)/L"shaders"/file,"CSMain","cs_5_0",b,err))return false;return SUCCEEDED(device_->CreateComputeShader(b->GetBufferPointer(),b->GetBufferSize(),nullptr,&out));};
    if(!cs(L"convert.hlsl",convert_)||!cs(L"downsample.hlsl",downsample_)||!cs(L"flow.hlsl",flow_)||!cs(L"nvof_unpack.hlsl",nvofUnpack_)||!cs(L"motion.hlsl",motion_)||!cs(L"native_motion_convert.hlsl",nativeMotionConvert_)||!cs(L"camera_motion.hlsl",cameraMotion_)||!cs(L"mask.hlsl",mask_)||!cs(L"depth_convert.hlsl",depthConvert_)||!cs(L"post.hlsl",post_)){
        wcsncpy_s(st.message,err.c_str(),_TRUNCATE);return false;
    }
    ComPtr<ID3DBlob>v,p;
    if(!compileFile(fs::path(moduleDir_)/L"shaders"/L"blit.hlsl","VSMain","vs_5_0",v,err)||!compileFile(fs::path(moduleDir_)/L"shaders"/L"blit.hlsl","PSMain","ps_5_0",p,err)){
        wcsncpy_s(st.message,err.c_str(),_TRUNCATE);return false;
    }
    return SUCCEEDED(device_->CreateVertexShader(v->GetBufferPointer(),v->GetBufferSize(),nullptr,&vs_))&&SUCCEEDED(device_->CreatePixelShader(p->GetBufferPointer(),p->GetBufferSize(),nullptr,&ps_));
}

bool D3D11Pipeline::ensureResources(const D3D11_TEXTURE2D_DESC&bb,const Settings&s,RuntimeStatus&st){
    if(width_==bb.Width&&height_==bb.Height&&backFormat_==bb.Format&&flowLow_){
        D3D11_TEXTURE2D_DESC fd{};flowLow_->GetDesc(&fd);
        if(fd.Width==(bb.Width+s.flowDownsample-1)/s.flowDownsample){ markPipelineStage(st.stageMask,PipelineStage::GuideResourcesReady); return true; }
    }
    releaseViews();forceResetNext_=true;width_=bb.Width;height_=bb.Height;backFormat_=bb.Format;
    D3D11_TEXTURE2D_DESC src=bb;src.BindFlags=D3D11_BIND_SHADER_RESOURCE;src.CPUAccessFlags=0;src.Usage=D3D11_USAGE_DEFAULT;src.MiscFlags=0;src.ArraySize=1;src.MipLevels=1;src.SampleDesc.Count=1;src.SampleDesc.Quality=0;
    if(FAILED(device_->CreateTexture2D(&src,nullptr,&sourceCopy_))||!makeSrv(device_.Get(),sourceCopy_.Get(),sourceSrv_)){wcscpy_s(st.message,L"backbuffer working copy unsupported");return false;}
    auto tex=[&](UINT w,UINT h,DXGI_FORMAT f,ComPtr<ID3D11Texture2D>&t,ComPtr<ID3D11ShaderResourceView>&srv,ComPtr<ID3D11UnorderedAccessView>&uav){
        D3D11_TEXTURE2D_DESC d{};d.Width=w;d.Height=h;d.MipLevels=1;d.ArraySize=1;d.Format=f;d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;
        return SUCCEEDED(device_->CreateTexture2D(&d,nullptr,&t))&&makeSrv(device_.Get(),t.Get(),srv)&&makeUav(device_.Get(),t.Get(),uav);
    };
    if(!tex(width_,height_,DXGI_FORMAT_R16G16B16A16_FLOAT,current_,currentSrv_,currentUav_)||!tex(width_,height_,DXGI_FORMAT_R16G16B16A16_FLOAT,history_,historySrv_,historyUav_)){wcscpy_s(st.message,L"RGBA16F temporal resources failed");return false;}
    if(!tex(width_,height_,DXGI_FORMAT_R8G8B8A8_UNORM,nrInput8_,nrInput8Srv_,nrInput8Uav_)||
       !tex(width_,height_,DXGI_FORMAT_R8G8B8A8_UNORM,nrOutput8_,nrOutput8Srv_,nrOutput8Uav_)||
       !tex(width_,height_,DXGI_FORMAT_R16G16B16A16_FLOAT,postOut_,postSrv_,postUav_)) return false;
    const UINT lw=(width_+s.flowDownsample-1)/s.flowDownsample,lh=(height_+s.flowDownsample-1)/s.flowDownsample;
    if(!tex(lw,lh,DXGI_FORMAT_R16_FLOAT,currentLow_,currentLowSrv_,currentLowUav_)||
       !tex(lw,lh,DXGI_FORMAT_R16_FLOAT,historyLow_,historyLowSrv_,historyLowUav_)||
       !tex(lw,lh,DXGI_FORMAT_R16G16B16A16_FLOAT,flowLow_,flowSrv_,flowUav_)||
       !tex(width_,height_,DXGI_FORMAT_R16G16_FLOAT,motionTex_,motionSrv_,motionUav_)||
       !tex(width_,height_,DXGI_FORMAT_R32_FLOAT,depthTex_,depthSrv_,depthUav_)||
       !tex(width_,height_,DXGI_FORMAT_R8G8B8A8_UNORM,maskTex_,maskSrv_,maskUav_)||
       !tex(width_,height_,DXGI_FORMAT_R8G8B8A8_UNORM,nrControlMaskTex_,nrControlMaskSrv_,nrControlMaskUav_)){
        wcscpy_s(st.message,L"guide resource creation failed");return false;
    }
    hasHistory_=false;if(backend_)backend_->reset();markPipelineStage(st.stageMask,PipelineStage::GuideResourcesReady);return true;
}

bool D3D11Pipeline::ensureBackend(const Settings&s,const std::wstring&runtime,RuntimeStatus&st){
    auto applyFallback=[&](){
        const auto label=backendDisplayName(backendMode_,true);
        wcsncpy_s(st.backendName,label.data(),_TRUNCATE);
        wcsncpy_s(st.message,backendFallbackMessage_.c_str(),_TRUNCATE);
        st.lastResult=backendFallbackResult_;
        st.requiredTagCount=backendFallbackRequiredTags_;
        st.missingRequiredTag=backendFallbackMissingTag_;
        st.feature=backendFallbackFeature_;
        st.failureStage=backendFallbackFailureStage_;
        st.stageMask|=backendFallbackStageMask_;
        st.neuralApi=backendFallbackNeuralApi_;
        st.neuralLocation=NeuralExecutionLocation::Unknown;
        st.neuralBackendKind=NeuralBackendKind::Passthrough;
    };
    if(backend_&&backendMode_==s.backend&&runtime_==runtime&&attemptUnsupportedCached_==s.attemptUnsupportedHardware){
        if(backendFallback_) applyFallback(); else wcsncpy_s(st.backendName,backend_->name(),_TRUNCATE);
        return true;
    }
    if(backend_){neural::destroyBackend(backend_);backend_=nullptr;}
    backendMode_=s.backend;runtime_=runtime;attemptUnsupportedCached_=s.attemptUnsupportedHardware;
    backendFallback_=false;backendFallbackMessage_.clear();backendFallbackResult_=0;backendFallbackRequiredTags_=0;backendFallbackMissingTag_=0;backendFallbackFeature_={};backendFallbackFailureStage_=PipelineStage::None;backendFallbackStageMask_=0;backendFallbackNeuralApi_=NeuralExecutionApi::None;
    neural::BackendInitContext init{device_.Get(),context_.Get(),nativeD12_.Get(),nativeQueue12_.Get()};

    if(s.backend==BackendMode::InGameNR){
        const RuntimeStatus preInit=st;
        backend_=neural::createInGameNR();
        RuntimeStatus directStatus=preInit;
        if(backend_ && backend_->initialize(init,runtime,s,directStatus)){
            st=directStatus;wcsncpy_s(st.backendName,backend_->name(),_TRUNCATE);return true;
        }
        if(backend_){neural::destroyBackend(backend_);backend_=nullptr;}

        backend_=neural::createExternalHostNR();
        RuntimeStatus hostStatus=preInit;
        if(backend_ && backend_->initialize(init,runtime,s,hostStatus)){
            st=hostStatus;
            wcsncpy_s(st.backendName,L"External NR Host (direct mount fallback)",_TRUNCATE);
            std::wstring message=L"Direct in-game NR failed; external host fallback active";
            if(directStatus.message[0]){message+=L". Direct failure: ";message+=directStatus.message;}
            wcsncpy_s(st.message,message.c_str(),_TRUNCATE);
            return true;
        }
        if(backend_){neural::destroyBackend(backend_);backend_=nullptr;}

        backendFallback_=true;
        std::wstring message=L"Direct in-game NR and external host both failed";
        if(directStatus.message[0]){message+=L". Direct: ";message+=directStatus.message;}
        if(hostStatus.message[0]){message+=L". Host: ";message+=hostStatus.message;}
        backendFallbackMessage_=message;
        backendFallbackResult_=hostStatus.lastResult?hostStatus.lastResult:directStatus.lastResult;
        backendFallbackRequiredTags_=hostStatus.requiredTagCount?hostStatus.requiredTagCount:directStatus.requiredTagCount;
        backendFallbackMissingTag_=hostStatus.missingRequiredTag?hostStatus.missingRequiredTag:directStatus.missingRequiredTag;
        backendFallbackFeature_=hostStatus.feature.requiredTags?hostStatus.feature:directStatus.feature;
        backendFallbackFailureStage_=hostStatus.failureStage!=PipelineStage::None?hostStatus.failureStage:directStatus.failureStage;
        backendFallbackStageMask_=directStatus.stageMask|hostStatus.stageMask;
        backendFallbackNeuralApi_=hostStatus.neuralApi!=NeuralExecutionApi::None?hostStatus.neuralApi:directStatus.neuralApi;
        backend_=neural::createPassthrough();RuntimeStatus ignored{};backend_->initialize(init,runtime,s,ignored);applyFallback();return true;
    }

    backend_=s.backend==BackendMode::ExternalHostNR?neural::createExternalHostNR():neural::createPassthrough();
    if(!backend_->initialize(init,runtime,s,st)){
        if(s.backend==BackendMode::ExternalHostNR){
            backendFallback_=true;backendFallbackMessage_=st.message;backendFallbackResult_=st.lastResult;backendFallbackRequiredTags_=st.requiredTagCount;backendFallbackMissingTag_=st.missingRequiredTag;backendFallbackFeature_=st.feature;backendFallbackFailureStage_=st.failureStage;backendFallbackStageMask_=st.stageMask;backendFallbackNeuralApi_=st.neuralApi;
            neural::destroyBackend(backend_);backend_=neural::createPassthrough();RuntimeStatus ignored{};backend_->initialize(init,runtime,s,ignored);applyFallback();return true;
        }
        return false;
    }
    wcsncpy_s(st.backendName,backend_->name(),_TRUNCATE);return true;
}

bool D3D11Pipeline::runComputeWithConstants(ID3D11ComputeShader*sh,ID3D11ShaderResourceView*const*srvs,UINT n,ID3D11UnorderedAccessView*uav,UINT w,UINT h,ID3D11Buffer* constants){
    if(!sh||!uav||!constants)return false;
    context_->CSSetShader(sh,nullptr,0);ID3D11Buffer* c=constants;context_->CSSetConstantBuffers(0,1,&c);context_->CSSetShaderResources(0,n,srvs);context_->CSSetUnorderedAccessViews(0,1,&uav,nullptr);context_->Dispatch((w+7)/8,(h+7)/8,1);
    std::array<ID3D11ShaderResourceView*,8> nulls{};ID3D11UnorderedAccessView*nu=nullptr;context_->CSSetShaderResources(0,(UINT)nulls.size(),nulls.data());context_->CSSetUnorderedAccessViews(0,1,&nu,nullptr);return true;
}
bool D3D11Pipeline::runCompute(ID3D11ComputeShader*sh,ID3D11ShaderResourceView*const*srvs,UINT n,ID3D11UnorderedAccessView*uav,UINT w,UINT h){
    return runComputeWithConstants(sh,srvs,n,uav,w,h,cb_.Get());
}

bool D3D11Pipeline::drawSrv(ID3D11ShaderResourceView* srv,ID3D11RenderTargetView* rtv,UINT width,UINT height){
    if(!srv||!rtv) return false;
    ID3D11RenderTargetView*r=rtv;context_->OMSetRenderTargets(1,&r,nullptr);D3D11_VIEWPORT vp{0,0,(FLOAT)width,(FLOAT)height,0,1};context_->RSSetViewports(1,&vp);
    context_->IASetInputLayout(nullptr);context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);context_->VSSetShader(vs_.Get(),nullptr,0);context_->PSSetShader(ps_.Get(),nullptr,0);context_->PSSetShaderResources(0,1,&srv);ID3D11SamplerState*sam=sampler_.Get();context_->PSSetSamplers(0,1,&sam);context_->Draw(3,0);
    ID3D11ShaderResourceView*n=nullptr;context_->PSSetShaderResources(0,1,&n);return true;
}

bool D3D11Pipeline::blit(ID3D11Texture2D*bb,RuntimeStatus&st){
    ComPtr<ID3D11RenderTargetView>rtv;if(FAILED(device_->CreateRenderTargetView(bb,nullptr,&rtv))){wcscpy_s(st.message,L"backbuffer RTV creation failed");return false;}
    return drawSrv(postSrv_.Get(),rtv.Get(),width_,height_);
}

bool D3D11Pipeline::process(ID3D11Texture2D*bb,const Settings&settings,const std::wstring&runtime,RuntimeStatus&st,const GuideProbeResult* externalGuide){
    if(!bb||!settings.enabled)return false;
    Settings effective=settings;const auto flowTuning=effectiveFlowTuning(settings);effective.flowDownsample=flowTuning.downsample;effective.flowSearchRadius=flowTuning.searchRadius;
    D3D11_TEXTURE2D_DESC desc{};bb->GetDesc(&desc);

    // Probe and snapshot while the game's own D3D11 state is still current.
    GuideProbeResult guide{};guideExtractor_.probe(bb,settings,guide,st);
    if(externalGuide){
        const auto externalPriority=guideSourcePriority(externalGuide->source);
        const auto localPriority=guideSourcePriority(guide.source);
        if(externalGuide->depth && (!guide.depth || externalPriority>=localPriority)){guide.depth=externalGuide->depth;guide.depthViewFormat=externalGuide->depthViewFormat;guide.depthInverted=externalGuide->depthInverted;guide.depthConventionKnown=externalGuide->depthConventionKnown;guide.depthWidth=externalGuide->depthWidth;guide.depthHeight=externalGuide->depthHeight;guide.depthConfidence=externalGuide->depthConfidence;}
        if(externalGuide->motion && externalGuide->motionConventionValid && (!guide.motion || externalPriority>=localPriority)){guide.motion=externalGuide->motion;guide.motionConventionValid=true;guide.motionEncoding=externalGuide->motionEncoding;guide.motionScaleX=externalGuide->motionScaleX;guide.motionScaleY=externalGuide->motionScaleY;guide.motionWidth=externalGuide->motionWidth;guide.motionHeight=externalGuide->motionHeight;guide.motionConfidence=externalGuide->motionConfidence;}
        if(externalGuide->cameraCut)guide.cameraCut=true;
        if((guide.depth||guide.motion)&&externalPriority>=localPriority){guide.source=externalGuide->source;guide.provider=externalGuide->provider;wcsncpy_s(st.guideAdapterName,guide.provider.c_str(),_TRUNCATE);std::wstring fields;if(guide.motion)fields=L"game motion";if(guide.depth){if(!fields.empty())fields+=L", ";fields+=L"game depth";}wcsncpy_s(st.guideFields,fields.c_str(),_TRUNCATE);if(guide.depth)wcsncpy_s(st.depthName,guide.provider.c_str(),_TRUNCATE);}
    }
    const auto trackedMotion=globalD3D11ResourceTracker().bestMotionCandidate(device_.Get(),desc.Width,desc.Height);
    const auto camera=globalD3D11CameraTracker().snapshot(device_.Get());
    st.nativeMotionCandidateId=trackedMotion.stableId;
    st.nativeMotionCandidateScore=trackedMotion.score;
    st.guideMotionWidth=guide.motion?guide.motionWidth:0u;
    st.guideMotionHeight=guide.motion?guide.motionHeight:0u;
    st.guideMotionConfidence=guide.motion?guide.motionConfidence:0u;
    st.guideDepthWidth=guide.depth?guide.depthWidth:0u;
    st.guideDepthHeight=guide.depth?guide.depthHeight:0u;
    st.guideDepthConfidence=guide.depth?guide.depthConfidence:0u;
    st.cameraCurrentValid=camera.currentValid?1u:0u;
    st.cameraPreviousValid=camera.previousValid?1u:0u;
    st.cameraConfidence=camera.confidence;

    // NVOFA is prepared as the universal fallback. Native game motion and
    // camera+depth motion still take priority in Auto mode.
    bool nvofAvailable=false;
    if(settings.motionSource!=MotionSource::Zero){
        const auto requestedGrid=std::min<std::uint32_t>(4,effective.flowDownsample);
        nvofAvailable=nvof_.configure(device_.Get(),context_.Get(),desc.Width,desc.Height,requestedGrid,settings.latencyMode,st);
        if(nvofAvailable) effective.flowDownsample=nvof_.gridSize();
    }

    if(!ensureResources(desc,effective,st)||!ensureBackend(settings,runtime,st))return false;
    ComPtr<ID3DDeviceContextState>previous;context1_->SwapDeviceContextState(ownState_.Get(),&previous);
    auto restore=[&](){ComPtr<ID3DDeviceContextState>mine;context1_->SwapDeviceContextState(previous.Get(),&mine);ownState_=mine;};
    auto t0=std::chrono::high_resolution_clock::now();bool ok=true;
    if(desc.SampleDesc.Count>1)context_->ResolveSubresource(sourceCopy_.Get(),0,bb,0,desc.Format);else context_->CopyResource(sourceCopy_.Get(),bb);

    Params p{};p.width=width_;p.height=height_;p.downsample=effective.flowDownsample;p.radius=effective.flowSearchRadius;p.exposure=settings.exposure;p.motionScale=settings.motionScale;p.confidence=settings.flowConfidenceThreshold;p.textProtection=settings.protectUI?settings.textProtection:0.0f;p.uiProtection=settings.protectUI?settings.uiProtection:0.0f;p.edgeThreshold=settings.edgeThreshold;p.sharpness=settings.sharpness;p.reactive=settings.reactiveStrength;p.controlMaskStrength=settings.controlMaskStrength;p.historyClamp=settings.historyClamp;p.disocclusion=settings.disocclusionThreshold;p.temporal=settings.temporalStrength;p.invertY=settings.invertMotionY?1u:0u;p.hasHistory=hasHistory_?1u:0u;p.pad0=0;p.pad1=0;p.staticDeadzone=settings.staticMotionDeadzone;p.motionScaleX=settings.motionScaleX;p.motionScaleY=settings.motionScaleY;p.debugSplit=settings.debugSplit;p.debugView=(std::uint32_t)settings.debugView;p.depthMode=(std::uint32_t)settings.depthMode;p.sourceSrgb=isSrgbFormat(desc.Format)?1u:0u;p.useControlMask=settings.useControlMask?1u:0u;
    auto uploadMain=[&](){D3D11_MAPPED_SUBRESOURCE m{};if(FAILED(context_->Map(cb_.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&m)))return false;memcpy(m.pData,&p,sizeof(p));context_->Unmap(cb_.Get(),0);return true;};
    auto uploadBuffer=[&](ID3D11Buffer* buffer,const void* data,size_t bytes){D3D11_MAPPED_SUBRESOURCE m{};if(!buffer||FAILED(context_->Map(buffer,0,D3D11_MAP_WRITE_DISCARD,0,&m)))return false;memcpy(m.pData,data,bytes);context_->Unmap(buffer,0);return true;};
    if(!uploadMain()){restore();return false;}
    {ID3D11ShaderResourceView*s[]={sourceSrv_.Get()};ok&=runCompute(convert_.Get(),s,1,currentUav_.Get(),width_,height_);}
    // Feature 18's verified SDR contract consumes stored UNORM SDR values.
    // A *_SRGB source SRV is implicitly decoded by D3D11 when building
    // current_, so re-encode only for the RGBA8 neural proxy.
    p.exposure=1.0f;p.pad0=1u;p.pad1=p.sourceSrgb;uploadMain();
    {ID3D11ShaderResourceView*s[]={currentSrv_.Get()};ok&=runCompute(convert_.Get(),s,1,nrInput8Uav_.Get(),width_,height_);}
    p.pad0=0u;p.pad1=0u;uploadMain();

    const UINT lw=(width_+effective.flowDownsample-1)/effective.flowDownsample;
    const UINT lh=(height_+effective.flowDownsample-1)/effective.flowDownsample;
    // Refresh the low-resolution luminance history every frame, even when a
    // native/camera motion route is active. If a later frame falls back to
    // optical flow, historyLow_ must represent the immediately previous frame.
    {ID3D11ShaderResourceView*s[]={currentSrv_.Get()};ok&=runCompute(downsample_.Get(),s,1,currentLowUav_.Get(),lw,lh);}

    // Depth is prepared before selecting motion because geometric camera motion
    // requires the actual game depth buffer.
    bool depthInverted=true,realDepth=false;
    ComPtr<ID3D11ShaderResourceView> gameDepthSrv;
    if(settings.depthMode!=DepthGuideMode::SyntheticFar && guideExtractor_.prepareDepth(guide,width_,height_,&gameDepthSrv,depthInverted,st)){
        if(settings.depthMode==DepthGuideMode::ForceNormal)depthInverted=false;
        else if(settings.depthMode==DepthGuideMode::ForceInverted)depthInverted=true;
        ID3D11ShaderResourceView*s[]={gameDepthSrv.Get()};ok&=runCompute(depthConvert_.Get(),s,1,depthUav_.Get(),width_,height_);realDepth=true;
        if(st.depthName[0]==0)wcscpy_s(st.depthName,L"Game depth buffer");
    }else{
        const float farDepth[4]={0,0,0,0};context_->ClearUnorderedAccessViewFloat(depthUav_.Get(),farDepth);depthInverted=true;wcscpy_s(st.depthName,L"Synthetic far depth");
    }

    Matrix4 currentClipToPreviousClip{};
    const bool cameraDepthReady=realDepth&&camera.currentValid&&camera.previousValid&&camera.confidence>=50&&
        buildCurrentClipToPreviousClip(matrixFromArray(camera.currentViewProjection),matrixFromArray(camera.previousViewProjection),currentClipToPreviousClip);
    MotionRouteAvailability availability{};
    if(settings.motionSource==MotionSource::Auto){
        availability.adapterNative=guide.motion&&guide.motionConventionValid;
        availability.trackedNative=trackedMotion.texture!=nullptr;
        availability.cameraDepth=cameraDepthReady;
    }
    availability.nvof=settings.motionSource!=MotionSource::Zero&&nvofAvailable;
    availability.hlsl=settings.motionSource==MotionSource::SynthesizedOpticalFlow;
    MotionRoute route=settings.motionSource==MotionSource::Zero?MotionRoute::Zero:chooseMotionRoute(availability);
    bool trustedMotion=false;
    float frameScaleX=settings.motionScaleX,frameScaleY=settings.motionScaleY;

    if(route==MotionRoute::AdapterNative){
        ComPtr<ID3D11ShaderResourceView> nativeSrv;
        if(!guide.motion||!guide.motionConventionValid||FAILED(device_->CreateShaderResourceView(guide.motion.Get(),nullptr,&nativeSrv))){route=cameraDepthReady?MotionRoute::CameraDepth:fallbackMotionRoute(settings.motionSource,nvofAvailable);}
        else{
            NativeMotionParams np{};np.width=width_;np.height=height_;np.encoding=(std::uint32_t)guide.motionEncoding;np.invertY=settings.invertMotionY?1u:0u;np.motionScale=settings.motionScale;np.inputToPixelScaleX=guide.motionScaleX;np.inputToPixelScaleY=guide.motionScaleY;
            ok&=uploadBuffer(nativeMotionCb_.Get(),&np,sizeof(np));ID3D11ShaderResourceView*s[]={nativeSrv.Get()};ok&=runComputeWithConstants(nativeMotionConvert_.Get(),s,1,motionUav_.Get(),width_,height_,nativeMotionCb_.Get());
            trustedMotion=true;frameScaleX=settings.motionScaleX;frameScaleY=settings.motionScaleY;
            const float reliable[4]={0,0,1,0};context_->ClearUnorderedAccessViewFloat(flowUav_.Get(),reliable);
        }
    }

    if(route==MotionRoute::TrackedNative){
        ComPtr<ID3D11ShaderResourceView> nativeSrv;
        if(!trackedMotion.texture||FAILED(device_->CreateShaderResourceView(trackedMotion.texture.Get(),nullptr,&nativeSrv))){route=cameraDepthReady?MotionRoute::CameraDepth:fallbackMotionRoute(settings.motionSource,nvofAvailable);}
        else{
            NativeMotionParams np{};np.width=width_;np.height=height_;np.encoding=(std::uint32_t)trackedMotion.meta.encoding;np.invertY=settings.invertMotionY?1u:0u;np.motionScale=settings.motionScale;np.inputToPixelScaleX=1.0f;np.inputToPixelScaleY=1.0f;
            ok&=uploadBuffer(nativeMotionCb_.Get(),&np,sizeof(np));ID3D11ShaderResourceView*s[]={nativeSrv.Get()};ok&=runComputeWithConstants(nativeMotionConvert_.Get(),s,1,motionUav_.Get(),width_,height_,nativeMotionCb_.Get());
            trustedMotion=true;const float reliable[4]={0,0,1,0};context_->ClearUnorderedAccessViewFloat(flowUav_.Get(),reliable);
        }
    }

    if(route==MotionRoute::CameraDepth){
        if(!cameraDepthReady){route=fallbackMotionRoute(settings.motionSource,nvofAvailable);}
        else{
            CameraMotionParams cp{};for(int r=0;r<4;r++)for(int c=0;c<4;c++)cp.currentClipToPreviousClip[r*4+c]=currentClipToPreviousClip.m[r][c];cp.width=width_;cp.height=height_;cp.motionScaleX=settings.motionScale;cp.motionScaleY=settings.motionScale*(settings.invertMotionY?-1.0f:1.0f);
            ok&=uploadBuffer(cameraCb_.Get(),&cp,sizeof(cp));ID3D11ShaderResourceView*s[]={depthSrv_.Get()};ok&=runComputeWithConstants(cameraMotion_.Get(),s,1,motionUav_.Get(),width_,height_,cameraCb_.Get());
            trustedMotion=true;const float reliable[4]={0,0,1,0};context_->ClearUnorderedAccessViewFloat(flowUav_.Get(),reliable);
        }
    }

    if(route==MotionRoute::Nvof){
        ok&=drawSrv(currentSrv_.Get(),nvof_.currentInputRtv(),width_,height_);
        const bool hadPrevious=nvof_.hasHistory();const bool produced=nvof_.execute(st);
        if(hadPrevious&&produced){
            p.downsample=nvof_.gridSize();p.pad1=nvof_.hasCost()?1u:0u;uploadMain();const UINT nw=(width_+p.downsample-1)/p.downsample,nh=(height_+p.downsample-1)/p.downsample;ID3D11ShaderResourceView*s[]={nvof_.vectorSrv(),nvof_.costSrv()};ok&=runCompute(nvofUnpack_.Get(),s,2,flowUav_.Get(),nw,nh);
        }else if(!hadPrevious){
            const float initialFlow[4]={0,0,0,1};context_->ClearUnorderedAccessViewFloat(flowUav_.Get(),initialFlow);
        }else{
            // Auto is fail-safe: a failed hardware-flow frame becomes zero
            // guidance, while the explicit Optical Flow mode may use HLSL.
            route=fallbackMotionRoute(settings.motionSource,false);
        }
    }
    if(route==MotionRoute::Hlsl){
        p.downsample=effective.flowDownsample;p.pad1=0;uploadMain();ID3D11ShaderResourceView*s[]={currentLowSrv_.Get(),historyLowSrv_.Get()};ok&=runCompute(flow_.Get(),s,2,flowUav_.Get(),lw,lh);
    }
    if(route==MotionRoute::Nvof||route==MotionRoute::Hlsl){
        p.pad0=0;uploadMain();ID3D11ShaderResourceView*s[]={flowSrv_.Get()};ok&=runCompute(motion_.Get(),s,1,motionUav_.Get(),width_,height_);
    }else if(route==MotionRoute::Zero){
        const float zero[4]={0,0,1,0};context_->ClearUnorderedAccessViewFloat(flowUav_.Get(),zero);context_->ClearUnorderedAccessViewFloat(motionUav_.Get(),zero);nvof_.resetHistory();
        frameScaleX=frameScaleY=1.0f;
    }else nvof_.resetHistory();

    // Record continuity after all runtime fallbacks have resolved.
    if(motionRouteValid_&&route!=lastMotionRoute_&&settings.resetOnTemporalGap)forceResetNext_=true;
    lastMotionRoute_=route;motionRouteValid_=true;
    wcsncpy_s(st.flowName,motionRouteLabel(route),_TRUNCATE);
    p.motionScaleX=frameScaleX;p.motionScaleY=frameScaleY;p.pad1=trustedMotion?2u:0u;uploadMain();
    {ID3D11ShaderResourceView*s[]={currentSrv_.Get(),historySrv_.Get(),flowSrv_.Get(),motionSrv_.Get()};ok&=runCompute(mask_.Get(),s,4,maskUav_.Get(),width_,height_);}

    // Keep the synthetic mask for our own post-composite only.  Feature 18's
    // known-good baseline does not bind a ControlMask, and a mask generated
    // from approximate flow can turn motion errors into visible speckle.
    bool explicitControlMask=false;
    if(settings.useControlMask&&guide.controlMask&&guide.controlMaskConventionValid)
        explicitControlMask=guideExtractor_.copyControlMask(guide,nrControlMaskTex_.Get(),width_,height_);

    const auto resolvedMotionSource=route==MotionRoute::Zero?MotionSource::Zero:settings.motionSource;
    const bool weakGuideReset=shouldForceTemporalResetForWeakGuides(resolvedMotionSource,realDepth,trustedMotion,cameraDepthReady);
    neural::FrameResources fr{};fr.input=nrInput8_.Get();fr.output=nrOutput8_.Get();fr.motion=motionTex_.Get();fr.depth=depthTex_.Get();fr.controlMask=explicitControlMask?nrControlMaskTex_.Get():nullptr;fr.width=width_;fr.height=height_;fr.inputFormat=DXGI_FORMAT_R8G8B8A8_UNORM;fr.depthInverted=depthInverted;fr.resetHistory=forceResetNext_||guide.cameraCut||!hasHistory_||weakGuideReset;fr.motionScaleX=frameScaleX;fr.motionScaleY=frameScaleY;
    // Synthesized/camera/native converter paths already baked global MotionScale
    // and Y inversion into the texture. Adapter-native motion did not, so its
    // complete scale is carried through frameScaleX/Y above.
    if(route!=MotionRoute::AdapterNative){fr.motionScaleX=settings.motionScaleX;fr.motionScaleY=settings.motionScaleY;p.motionScaleX=fr.motionScaleX;p.motionScaleY=fr.motionScaleY;}
    st.gameDepthActive=realDepth?1u:0u;st.depthInverted=depthInverted?1u:0u;st.controlMaskActive=(settings.useControlMask&&fr.controlMask)?1u:0u;st.temporalResetThisFrame=fr.resetHistory?1u:0u;
    st.temporalHistoryValid=fr.resetHistory?0u:1u;
    if(guide.cameraCut) wcscpy_s(st.temporalReason,L"Game/adapter camera cut");
    else if(weakGuideReset) wcscpy_s(st.temporalReason,L"Weak guides: zero motion + synthetic depth; frame-local NR history");
    else if(!hasHistory_) wcscpy_s(st.temporalReason,L"First frame / no previous history");
    else if(forceResetNext_) wcscpy_s(st.temporalReason,L"Motion/backend continuity reset");
    else wcscpy_s(st.temporalReason,L"Continuous history");
    const bool neuralEval=backend_->evaluate(context_.Get(),fr,settings,st);
    if(settings.backend==BackendMode::InGameNR&&!backendFallback_&&neuralEval)st.neuralActive=1u;else st.neuralActive=0u;
    if(!neuralEval){context_->CopyResource(nrOutput8_.Get(),nrInput8_.Get());if(settings.resetOnTemporalGap)forceResetNext_=true;}else forceResetNext_=false;
    {ID3D11ShaderResourceView*s[]={nrOutput8Srv_.Get(),currentSrv_.Get(),maskSrv_.Get(),motionSrv_.Get(),depthSrv_.Get()};ok&=runCompute(post_.Get(),s,5,postUav_.Get(),width_,height_);}
    const bool blitOk=blit(bb,st);ok&=blitOk;if(blitOk)markPipelineStage(st.stageMask,PipelineStage::OutputComposited);else if(st.failureStage==PipelineStage::None)st.failureStage=PipelineStage::OutputCompositeFailed;
    context_->CopyResource(history_.Get(),current_.Get());context_->CopyResource(historyLow_.Get(),currentLow_.Get());hasHistory_=true;
    auto t1=std::chrono::high_resolution_clock::now();st.lastGpuMs=(float)std::chrono::duration<double,std::milli>(t1-t0).count();restore();return ok;
}

} // namespace udlss::gpu
