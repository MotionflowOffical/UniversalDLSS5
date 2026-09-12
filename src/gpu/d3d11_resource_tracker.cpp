#include "d3d11_resource_tracker.hpp"
#include "d3d11_camera_tracker.hpp"
#include <MinHook.h>
#include <d3dcompiler.h>
#include <d3d11shader.h>
#include <algorithm>
#include <cctype>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

using Microsoft::WRL::ComPtr;
namespace udlss::gpu {
namespace {
struct Entry {
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11Device> device;
    NativeMotionCandidateMeta meta{};
    std::uint64_t id{};
    std::uint64_t lastWriteFrame{};
    std::uint64_t lastSeenFrame{};
    std::uint32_t frameWrites{};
    std::uint32_t frameSamples{};
    std::uint32_t frameSampledAfterWrite{};
    bool depthBound{};
    DXGI_FORMAT depthViewFormat{DXGI_FORMAT_UNKNOWN};
    std::uint32_t frameDepthDraws{};
    std::uint32_t depthDraws{};
    std::uint32_t frameDepthClears{};
    float frameClearDepth{1.0f};
    float clearDepth{1.0f};
    bool clearKnown{};
};
struct MotionBinding { UINT slot{}; NativeMotionEncoding encoding{NativeMotionEncoding::Unknown}; };
struct ShaderInfo { std::vector<MotionBinding> motionSrvSlots; };
struct ContextShaderState { ComPtr<IUnknown> ps; ComPtr<IUnknown> cs; };
struct State {
    mutable std::mutex mutex;
    std::unordered_map<ID3D11Texture2D*,Entry> entries;
    std::unordered_map<IUnknown*,ShaderInfo> shaders;
    std::unordered_map<ID3D11DeviceContext*,ContextShaderState> contexts;
    std::vector<ComPtr<ID3D11Texture2D>> currentRtvs;
    ComPtr<ID3D11Texture2D> currentDsv;
    std::uint64_t frame{1};
    std::uint64_t nextId{1};
} g;
thread_local bool g_suppressed=false;

MotionFormatClass classify(DXGI_FORMAT f) {
    switch(f) {
    case DXGI_FORMAT_R16G16_FLOAT: return MotionFormatClass::Rg16Float;
    case DXGI_FORMAT_R16G16_SNORM: return MotionFormatClass::Rg16Snorm;
    case DXGI_FORMAT_R32G32_FLOAT: return MotionFormatClass::Rg32Float;
    case DXGI_FORMAT_D16_UNORM: case DXGI_FORMAT_D24_UNORM_S8_UINT: case DXGI_FORMAT_D32_FLOAT:
    case DXGI_FORMAT_R16_TYPELESS: case DXGI_FORMAT_R24G8_TYPELESS: case DXGI_FORMAT_R32_TYPELESS:
        return MotionFormatClass::DepthLike;
    default: return MotionFormatClass::ColorLike;
    }
}
Entry* getOrCreate(ID3D11Resource* resource) {
    if(!resource) return nullptr;
    ComPtr<ID3D11Texture2D> tex;
    if(FAILED(resource->QueryInterface(IID_PPV_ARGS(&tex)))) return nullptr;
    auto it=g.entries.find(tex.Get());
    if(it!=g.entries.end()) return &it->second;
    D3D11_TEXTURE2D_DESC d{}; tex->GetDesc(&d);
    Entry e{}; e.texture=tex; tex->GetDevice(&e.device); e.id=g.nextId++;
    e.meta.width=d.Width; e.meta.height=d.Height; e.meta.format=classify(d.Format); e.meta.msaa=d.SampleDesc.Count>1;
    auto [pos,_]=g.entries.emplace(tex.Get(),std::move(e));
    return &pos->second;
}
bool sameDevice(ID3D11Device* a,ID3D11Device* b) {
    if(!a||!b) return false;
    ComPtr<IUnknown> ua,ub; if(FAILED(a->QueryInterface(IID_PPV_ARGS(&ua)))||FAILED(b->QueryInterface(IID_PPV_ARGS(&ub)))) return a==b;
    return ua.Get()==ub.Get();
}
std::string normalized(std::string s){
    std::string out; out.reserve(s.size());
    for(unsigned char c:s) if(std::isalnum(c)) out.push_back(static_cast<char>(std::tolower(c)));
    return out;
}
NativeMotionEncoding semanticEncoding(const char* name){
    if(!name) return NativeMotionEncoding::Unknown;
    const auto n=normalized(name);
    // Unity's built-in _CameraMotionVectorsTexture stores currentUV-previousUV.
    // DLSS consumes current->previous vectors, so the pipeline negates and
    // converts this UV field to pixels before use.
    if(n.find("cameramotionvectors")!=std::string::npos)
        return NativeMotionEncoding::UnityUvPreviousToCurrent;
    return NativeMotionEncoding::Unknown;
}
bool isMotionSemantic(const char* name){
    if(!name) return false;
    const auto n=normalized(name);
    return n.find("cameramotionvectors")!=std::string::npos ||
           n.find("motionvectors")!=std::string::npos ||
           n.find("motionvector")!=std::string::npos ||
           n.find("velocity")!=std::string::npos;
}
const MotionBinding* semanticBinding(const ShaderInfo* info,UINT slot){
    if(!info) return nullptr;
    for(const auto& b:info->motionSrvSlots) if(b.slot==slot) return &b;
    return nullptr;
}

using OMSetRenderTargetsFn=void (STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,ID3D11RenderTargetView* const*,ID3D11DepthStencilView*);
using PSSetShaderResourcesFn=void (STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT,ID3D11ShaderResourceView* const*);
using CSSetShaderResourcesFn=void (STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT,ID3D11ShaderResourceView* const*);
using PSSetShaderFn=void (STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11PixelShader*,ID3D11ClassInstance* const*,UINT);
using CSSetShaderFn=void (STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11ComputeShader*,ID3D11ClassInstance* const*,UINT);
using CreatePixelShaderFn=HRESULT (STDMETHODCALLTYPE*)(ID3D11Device*,const void*,SIZE_T,ID3D11ClassLinkage*,ID3D11PixelShader**);
using CreateComputeShaderFn=HRESULT (STDMETHODCALLTYPE*)(ID3D11Device*,const void*,SIZE_T,ID3D11ClassLinkage*,ID3D11ComputeShader**);
using DrawFn=void (STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT);
using DrawIndexedFn=void (STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT,INT);
using ClearRTVFn=void (STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11RenderTargetView*,const FLOAT[4]);
using ClearDepthStencilViewFn=void (STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11DepthStencilView*,UINT,FLOAT,UINT8);
OMSetRenderTargetsFn origOM{}; PSSetShaderResourcesFn origPS{}; CSSetShaderResourcesFn origCS{};
PSSetShaderFn origPSShader{}; CSSetShaderFn origCSShader{}; CreatePixelShaderFn origCreatePS{}; CreateComputeShaderFn origCreateCS{};
DrawFn origDraw{}; DrawIndexedFn origDrawIndexed{}; ClearRTVFn origClear{}; ClearDepthStencilViewFn origClearDepth{};

void STDMETHODCALLTYPE hkOM(ID3D11DeviceContext* c,UINT n,ID3D11RenderTargetView* const* r,ID3D11DepthStencilView* d){ if(!g_suppressed) globalD3D11ResourceTracker().onRenderTargets(c,n,r,d); origOM(c,n,r,d); }
void STDMETHODCALLTYPE hkPS(ID3D11DeviceContext* c,UINT s,UINT n,ID3D11ShaderResourceView* const* r){ if(!g_suppressed) globalD3D11ResourceTracker().onShaderResources(c,D3D11ResourceTracker::ShaderStage::Pixel,s,n,r); origPS(c,s,n,r); }
void STDMETHODCALLTYPE hkCS(ID3D11DeviceContext* c,UINT s,UINT n,ID3D11ShaderResourceView* const* r){ if(!g_suppressed) globalD3D11ResourceTracker().onShaderResources(c,D3D11ResourceTracker::ShaderStage::Compute,s,n,r); origCS(c,s,n,r); }
void STDMETHODCALLTYPE hkPSShader(ID3D11DeviceContext*c,ID3D11PixelShader*s,ID3D11ClassInstance* const*i,UINT n){if(!g_suppressed)globalD3D11ResourceTracker().onShaderBound(c,D3D11ResourceTracker::ShaderStage::Pixel,s);origPSShader(c,s,i,n);}
void STDMETHODCALLTYPE hkCSShader(ID3D11DeviceContext*c,ID3D11ComputeShader*s,ID3D11ClassInstance* const*i,UINT n){if(!g_suppressed)globalD3D11ResourceTracker().onShaderBound(c,D3D11ResourceTracker::ShaderStage::Compute,s);origCSShader(c,s,i,n);}
HRESULT STDMETHODCALLTYPE hkCreatePS(ID3D11Device*d,const void*bc,SIZE_T n,ID3D11ClassLinkage*l,ID3D11PixelShader**out){const auto hr=origCreatePS(d,bc,n,l,out);if(SUCCEEDED(hr)&&out&&*out&&!g_suppressed)globalD3D11ResourceTracker().onShaderCreated(D3D11ResourceTracker::ShaderStage::Pixel,bc,n,*out);return hr;}
HRESULT STDMETHODCALLTYPE hkCreateCS(ID3D11Device*d,const void*bc,SIZE_T n,ID3D11ClassLinkage*l,ID3D11ComputeShader**out){const auto hr=origCreateCS(d,bc,n,l,out);if(SUCCEEDED(hr)&&out&&*out&&!g_suppressed)globalD3D11ResourceTracker().onShaderCreated(D3D11ResourceTracker::ShaderStage::Compute,bc,n,*out);return hr;}
void STDMETHODCALLTYPE hkDraw(ID3D11DeviceContext* c,UINT a,UINT b){ if(!g_suppressed){ globalD3D11ResourceTracker().onDraw(c); globalD3D11CameraTracker().onDraw(c);} origDraw(c,a,b); }
void STDMETHODCALLTYPE hkDrawIndexed(ID3D11DeviceContext* c,UINT a,UINT b,INT d){ if(!g_suppressed){ globalD3D11ResourceTracker().onDraw(c); globalD3D11CameraTracker().onDraw(c);} origDrawIndexed(c,a,b,d); }
void STDMETHODCALLTYPE hkClear(ID3D11DeviceContext* c,ID3D11RenderTargetView* r,const FLOAT col[4]){ if(!g_suppressed) globalD3D11ResourceTracker().onClearRenderTarget(r); origClear(c,r,col); }
void STDMETHODCALLTYPE hkClearDepth(ID3D11DeviceContext* c,ID3D11DepthStencilView* d,UINT flags,FLOAT depth,UINT8 stencil){ if(!g_suppressed) globalD3D11ResourceTracker().onClearDepthStencil(d,flags,depth); origClearDepth(c,d,flags,depth,stencil); }

bool hook(void* target,void* detour,void** original){
    const auto r=MH_CreateHook(target,detour,original);
    return r==MH_OK || r==MH_ERROR_ALREADY_CREATED;
}
}

D3D11ResourceTracker& globalD3D11ResourceTracker(){ static D3D11ResourceTracker t; return t; }
void setD3D11TrackingSuppressed(bool value){g_suppressed=value;} bool d3d11TrackingSuppressed(){return g_suppressed;}

void D3D11ResourceTracker::onRenderTargets(ID3D11DeviceContext*,UINT count,ID3D11RenderTargetView* const* rtvs,ID3D11DepthStencilView* dsv){
    std::scoped_lock l(g.mutex); g.currentRtvs.clear(); g.currentDsv.Reset();
    for(UINT i=0;i<count;i++){ if(!rtvs||!rtvs[i])continue; ComPtr<ID3D11Resource> r;rtvs[i]->GetResource(&r);auto* e=getOrCreate(r.Get());if(e)g.currentRtvs.push_back(e->texture); }
    if(dsv){
        D3D11_DEPTH_STENCIL_VIEW_DESC vd{}; dsv->GetDesc(&vd);
        ComPtr<ID3D11Resource> r; dsv->GetResource(&r); auto* e=getOrCreate(r.Get());
        if(e){ e->depthBound=true; e->depthViewFormat=vd.Format; e->lastSeenFrame=g.frame; g.currentDsv=e->texture; }
    }
}
void D3D11ResourceTracker::onShaderCreated(ShaderStage,const void* bytecode,SIZE_T length,IUnknown* shader){
    if(!bytecode||!length||!shader)return;
    ComPtr<ID3D11ShaderReflection> reflection;
    if(FAILED(D3DReflect(bytecode,length,IID_PPV_ARGS(&reflection))))return;
    D3D11_SHADER_DESC desc{}; if(FAILED(reflection->GetDesc(&desc)))return;
    ShaderInfo info{};
    for(UINT i=0;i<desc.BoundResources;i++){
        D3D11_SHADER_INPUT_BIND_DESC b{};
        if(FAILED(reflection->GetResourceBindingDesc(i,&b))||!b.Name)continue;
        if((b.Type==D3D_SIT_TEXTURE||b.Type==D3D_SIT_TBUFFER)&&isMotionSemantic(b.Name)){
            for(UINT n=0;n<std::max<UINT>(1,b.BindCount);n++)info.motionSrvSlots.push_back({b.BindPoint+n,semanticEncoding(b.Name)});
        }
    }
    if(!info.motionSrvSlots.empty()){std::scoped_lock l(g.mutex);g.shaders[shader]=std::move(info);}
}
void D3D11ResourceTracker::onShaderBound(ID3D11DeviceContext* context,ShaderStage stage,IUnknown* shader){
    if(!context)return;std::scoped_lock l(g.mutex);auto&cs=g.contexts[context];if(stage==ShaderStage::Pixel)cs.ps=shader;else cs.cs=shader;
}
void D3D11ResourceTracker::onShaderResources(ID3D11DeviceContext* context,ShaderStage stage,UINT start,UINT count,ID3D11ShaderResourceView* const* srvs){
    std::scoped_lock l(g.mutex);
    const ShaderInfo* info=nullptr;
    auto ci=g.contexts.find(context);
    if(ci!=g.contexts.end()){
        IUnknown* shader=stage==ShaderStage::Pixel?ci->second.ps.Get():ci->second.cs.Get();
        if(shader){auto si=g.shaders.find(shader);if(si!=g.shaders.end())info=&si->second;}
    }
    for(UINT i=0;i<count;i++){
        if(!srvs||!srvs[i])continue;ComPtr<ID3D11Resource> r;srvs[i]->GetResource(&r);auto*e=getOrCreate(r.Get());if(!e)continue;
        ++e->frameSamples;if(e->lastWriteFrame==g.frame)++e->frameSampledAfterWrite;e->lastSeenFrame=g.frame;
        if(const auto* binding=semanticBinding(info,start+i)) {
            e->meta.semanticMotion = true;
            if(binding->encoding!=NativeMotionEncoding::Unknown) e->meta.encoding=binding->encoding;
        }
    }
}
void D3D11ResourceTracker::onDraw(ID3D11DeviceContext*){
    std::scoped_lock l(g.mutex);for(auto&t:g.currentRtvs){auto it=g.entries.find(t.Get());if(it==g.entries.end())continue;++it->second.frameWrites;it->second.lastWriteFrame=g.frame;it->second.lastSeenFrame=g.frame;}
    if(g.currentDsv){auto it=g.entries.find(g.currentDsv.Get());if(it!=g.entries.end()){++it->second.frameDepthDraws;it->second.lastSeenFrame=g.frame;}}
}
void D3D11ResourceTracker::onClearRenderTarget(ID3D11RenderTargetView* rtv){
    if(!rtv)return;std::scoped_lock l(g.mutex);ComPtr<ID3D11Resource> r;rtv->GetResource(&r);auto*e=getOrCreate(r.Get());if(e)++e->meta.clears;
}
void D3D11ResourceTracker::onClearDepthStencil(ID3D11DepthStencilView* dsv,UINT clearFlags,float clearDepth){
    if(!dsv || !(clearFlags&D3D11_CLEAR_DEPTH)) return;
    std::scoped_lock l(g.mutex); D3D11_DEPTH_STENCIL_VIEW_DESC vd{}; dsv->GetDesc(&vd);
    ComPtr<ID3D11Resource> r; dsv->GetResource(&r); auto* e=getOrCreate(r.Get()); if(!e)return;
    e->depthBound=true; e->depthViewFormat=vd.Format; ++e->frameDepthClears; e->frameClearDepth=clearDepth; e->lastSeenFrame=g.frame;
}
void D3D11ResourceTracker::finalizeFrame(ID3D11Device* device,UINT,UINT){
    std::scoped_lock l(g.mutex);for(auto&[_,e]:g.entries){if(!sameDevice(e.device.Get(),device))continue;if(e.lastSeenFrame==g.frame){e.meta.renderTargetWrites=e.frameWrites;e.meta.shaderResourceBinds=e.frameSamples;e.meta.sampledAfterWrite=e.frameSampledAfterWrite;++e.meta.framesObserved;e.depthDraws=e.frameDepthDraws;if(e.frameDepthClears){e.clearDepth=e.frameClearDepth;e.clearKnown=true;}}e.frameWrites=e.frameSamples=e.frameSampledAfterWrite=0;e.frameDepthDraws=0;e.frameDepthClears=0;}++g.frame;g.currentRtvs.clear();g.currentDsv.Reset();
}
NativeMotionCandidate D3D11ResourceTracker::bestMotionCandidate(ID3D11Device* device,UINT width,UINT height) const{
    std::scoped_lock l(g.mutex);NativeMotionCandidate best{};for(const auto&[_,e]:g.entries){if(!sameDevice(e.device.Get(),device))continue;const auto score=scoreNativeMotionCandidate(e.meta,width,height);if(score>best.score){best.texture=e.texture;best.meta=e.meta;best.stableId=e.id;best.score=score;}}if(!nativeMotionCandidateAutoUsable(best.meta,width,height))best.texture.Reset();return best;
}
TrackedDepthCandidate D3D11ResourceTracker::bestDepthCandidate(ID3D11Device* device,UINT width,UINT height) const{
    std::scoped_lock l(g.mutex); TrackedDepthCandidate best{};
    for(const auto&[_,e]:g.entries){
        if(!sameDevice(e.device.Get(),device)||!e.depthBound||e.depthDraws==0)continue;
        D3D11_TEXTURE2D_DESC d{};e.texture->GetDesc(&d);
        const bool formatOk=e.depthViewFormat==DXGI_FORMAT_D32_FLOAT||e.depthViewFormat==DXGI_FORMAT_D24_UNORM_S8_UINT||e.depthViewFormat==DXGI_FORMAT_D16_UNORM;
        if(!formatOk||d.Width!=width||d.Height!=height||d.SampleDesc.Count!=1)continue;
        const std::uint32_t score=10000u+std::min<std::uint32_t>(5000,e.depthDraws*4u)+(e.clearKnown?250u:0u);
        if(score>best.score){best.texture=e.texture;best.viewFormat=e.depthViewFormat;best.clearDepth=e.clearDepth;best.clearKnown=e.clearKnown;best.drawUses=e.depthDraws;best.stableId=e.id;best.score=score;}
    }
    return best;
}
void D3D11ResourceTracker::reset(){std::scoped_lock l(g.mutex);g.entries.clear();g.shaders.clear();g.contexts.clear();g.currentRtvs.clear();g.currentDsv.Reset();g.frame=1;g.nextId=1;}

bool installD3D11ResourceTrackingHooks(ID3D11DeviceContext* sample){
    if(!sample)return false;void**v=*(void***)sample;bool ok=true;
    ComPtr<ID3D11Device> device; sample->GetDevice(&device); void**dv=device?*(void***)device.Get():nullptr;
    ok&=hook(v[33],(void*)hkOM,(void**)&origOM);
    ok&=hook(v[8],(void*)hkPS,(void**)&origPS);
    ok&=hook(v[67],(void*)hkCS,(void**)&origCS);
    ok&=hook(v[9],(void*)hkPSShader,(void**)&origPSShader);
    ok&=hook(v[69],(void*)hkCSShader,(void**)&origCSShader);
    ok&=hook(v[13],(void*)hkDraw,(void**)&origDraw);
    ok&=hook(v[12],(void*)hkDrawIndexed,(void**)&origDrawIndexed);
    ok&=hook(v[50],(void*)hkClear,(void**)&origClear);
    ok&=hook(v[53],(void*)hkClearDepth,(void**)&origClearDepth); // ClearDepthStencilView
    if(dv){ok&=hook(dv[15],(void*)hkCreatePS,(void**)&origCreatePS);ok&=hook(dv[18],(void*)hkCreateCS,(void**)&origCreateCS);}
    return ok;
}

} // namespace udlss::gpu
