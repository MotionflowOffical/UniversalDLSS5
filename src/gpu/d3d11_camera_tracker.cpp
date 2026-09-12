#include "d3d11_camera_tracker.hpp"
#include "d3d11_resource_tracker.hpp"
#include <MinHook.h>
#include <d3dcompiler.h>
#include <d3d11shader.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

using Microsoft::WRL::ComPtr;
namespace udlss::gpu {
namespace {
struct Binding { CameraMatrixKind kind{}; UINT slot{}; UINT offset{}; UINT size{}; bool columnMajor{}; };
struct ShaderInfo { std::vector<Binding> bindings; };
struct BufferData { ComPtr<ID3D11Buffer> buffer; ComPtr<ID3D11Device> device; std::vector<std::uint8_t> bytes; void* mapped{}; };
struct ContextState { ComPtr<ID3D11VertexShader> vs; std::array<ComPtr<ID3D11Buffer>,14> cbs{}; };
struct FrameCandidate { std::array<float,16> matrix{}; std::uint32_t hits{}; ComPtr<ID3D11Device> device; };
struct DeviceState { CameraMatrixSnapshot snapshot{}; std::array<float,16> lastVp{}; bool lastValid{}; std::uint32_t frames{}; };
struct State {
    mutable std::mutex mutex;
    std::unordered_map<ID3D11VertexShader*,ShaderInfo> shaders;
    std::unordered_map<ID3D11Buffer*,BufferData> buffers;
    std::unordered_map<ID3D11DeviceContext*,ContextState> contexts;
    std::unordered_map<std::uintptr_t,FrameCandidate> frameCandidates;
    std::unordered_map<ID3D11Device*,DeviceState> devices;
} g;

bool finiteMatrix(const float* m){for(int i=0;i<16;i++)if(!std::isfinite(m[i]))return false;return true;}
bool usefulMatrix(const float* m){if(!finiteMatrix(m))return false;float sum=0;for(int i=0;i<16;i++)sum+=std::fabs(m[i]);return sum>0.5f&&sum<1e8f;}
std::uintptr_t key(ID3D11VertexShader* s,ID3D11Buffer* b,UINT offset){return (reinterpret_cast<std::uintptr_t>(s)>>4)^(reinterpret_cast<std::uintptr_t>(b)<<1)^offset;}
bool sameDevice(ID3D11Device* a,ID3D11Device* b){if(!a||!b)return false;ComPtr<IUnknown>ua,ub;if(FAILED(a->QueryInterface(IID_PPV_ARGS(&ua)))||FAILED(b->QueryInterface(IID_PPV_ARGS(&ub))))return a==b;return ua.Get()==ub.Get();}

using CreateVSFn=HRESULT (STDMETHODCALLTYPE*)(ID3D11Device*,const void*,SIZE_T,ID3D11ClassLinkage*,ID3D11VertexShader**);
using VSSetShaderFn=void (STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11VertexShader*,ID3D11ClassInstance* const*,UINT);
using VSSetCBFn=void (STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT,ID3D11Buffer* const*);
using UpdateFn=void (STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11Resource*,UINT,const D3D11_BOX*,const void*,UINT,UINT);
using MapFn=HRESULT (STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11Resource*,UINT,D3D11_MAP,UINT,D3D11_MAPPED_SUBRESOURCE*);
using UnmapFn=void (STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11Resource*,UINT);
CreateVSFn origCreateVS{};VSSetShaderFn origVS{};VSSetCBFn origCB{};UpdateFn origUpdate{};MapFn origMap{};UnmapFn origUnmap{};

HRESULT STDMETHODCALLTYPE hkCreateVS(ID3D11Device*d,const void*bc,SIZE_T n,ID3D11ClassLinkage*l,ID3D11VertexShader**out){auto hr=origCreateVS(d,bc,n,l,out);if(SUCCEEDED(hr)&&out&&*out&&!d3d11TrackingSuppressed())globalD3D11CameraTracker().onVertexShaderCreated(bc,n,*out);return hr;}
void STDMETHODCALLTYPE hkVS(ID3D11DeviceContext*c,ID3D11VertexShader*s,ID3D11ClassInstance* const*i,UINT n){if(!d3d11TrackingSuppressed())globalD3D11CameraTracker().onVertexShaderBound(c,s);origVS(c,s,i,n);}
void STDMETHODCALLTYPE hkCB(ID3D11DeviceContext*c,UINT start,UINT n,ID3D11Buffer* const*b){if(!d3d11TrackingSuppressed())globalD3D11CameraTracker().onConstantBuffersBound(c,start,n,b);origCB(c,start,n,b);}
void STDMETHODCALLTYPE hkUpdate(ID3D11DeviceContext*c,ID3D11Resource*r,UINT sub,const D3D11_BOX*box,const void*data,UINT row,UINT depth){if(!d3d11TrackingSuppressed()&&sub==0&&!box&&data)globalD3D11CameraTracker().onUpdateResource(r,data);origUpdate(c,r,sub,box,data,row,depth);}
HRESULT STDMETHODCALLTYPE hkMap(ID3D11DeviceContext*c,ID3D11Resource*r,UINT sub,D3D11_MAP type,UINT flags,D3D11_MAPPED_SUBRESOURCE*m){auto hr=origMap(c,r,sub,type,flags,m);if(SUCCEEDED(hr)&&!d3d11TrackingSuppressed()&&sub==0&&m&&m->pData)globalD3D11CameraTracker().onMap(r,m->pData);return hr;}
void STDMETHODCALLTYPE hkUnmap(ID3D11DeviceContext*c,ID3D11Resource*r,UINT sub){if(!d3d11TrackingSuppressed()&&sub==0)globalD3D11CameraTracker().onUnmap(r);origUnmap(c,r,sub);}
bool hook(void*target,void*detour,void**original){auto r=MH_CreateHook(target,detour,original);return r==MH_OK||r==MH_ERROR_ALREADY_CREATED;}
}

D3D11CameraTracker& globalD3D11CameraTracker(){static D3D11CameraTracker t;return t;}

void D3D11CameraTracker::onVertexShaderCreated(const void* bytecode,SIZE_T length,ID3D11VertexShader* shader){
    if(!bytecode||!length||!shader)return;ComPtr<ID3D11ShaderReflection> refl;if(FAILED(D3DReflect(bytecode,length,IID_PPV_ARGS(&refl))))return;D3D11_SHADER_DESC sd{};if(FAILED(refl->GetDesc(&sd)))return;ShaderInfo info{};
    for(UINT bi=0;bi<sd.ConstantBuffers;bi++){auto*cb=refl->GetConstantBufferByIndex(bi);if(!cb)continue;D3D11_SHADER_BUFFER_DESC cbd{};if(FAILED(cb->GetDesc(&cbd))||!cbd.Name)continue;D3D11_SHADER_INPUT_BIND_DESC bind{};if(FAILED(refl->GetResourceBindingDescByName(cbd.Name,&bind))||bind.Type!=D3D_SIT_CBUFFER)continue;
        for(UINT vi=0;vi<cbd.Variables;vi++){auto*v=cb->GetVariableByIndex(vi);if(!v)continue;D3D11_SHADER_VARIABLE_DESC vd{};if(FAILED(v->GetDesc(&vd))||!vd.Name||vd.Size<64)continue;const auto kind=classifyMatrixBindingName(vd.Name);if(kind==CameraMatrixKind::Unknown)continue;auto*t=v->GetType();D3D11_SHADER_TYPE_DESC td{};if(!t||FAILED(t->GetDesc(&td))||td.Class!=D3D_SVC_MATRIX_ROWS&&td.Class!=D3D_SVC_MATRIX_COLUMNS)continue;if(td.Rows!=4||td.Columns!=4)continue;info.bindings.push_back({kind,bind.BindPoint,vd.StartOffset,vd.Size,td.Class==D3D_SVC_MATRIX_COLUMNS});}
    }
    if(!info.bindings.empty()){std::scoped_lock l(g.mutex);g.shaders[shader]=std::move(info);}
}
void D3D11CameraTracker::onVertexShaderBound(ID3D11DeviceContext*c,ID3D11VertexShader*s){if(!c)return;std::scoped_lock l(g.mutex);g.contexts[c].vs=s;}
void D3D11CameraTracker::onConstantBuffersBound(ID3D11DeviceContext*c,UINT start,UINT count,ID3D11Buffer* const*buffers){if(!c)return;std::scoped_lock l(g.mutex);auto&st=g.contexts[c];for(UINT i=0;i<count&&start+i<st.cbs.size();i++)st.cbs[start+i]=buffers?buffers[i]:nullptr;}
void D3D11CameraTracker::onUpdateResource(ID3D11Resource*r,const void*data){if(!r||!data)return;ComPtr<ID3D11Buffer>b;if(FAILED(r->QueryInterface(IID_PPV_ARGS(&b))))return;D3D11_BUFFER_DESC d{};b->GetDesc(&d);if(!d.ByteWidth||d.ByteWidth>65536)return;std::scoped_lock l(g.mutex);auto&e=g.buffers[b.Get()];e.buffer=b;b->GetDevice(&e.device);e.bytes.resize(d.ByteWidth);std::memcpy(e.bytes.data(),data,d.ByteWidth);}
void D3D11CameraTracker::onMap(ID3D11Resource*r,void*data){if(!r||!data)return;ComPtr<ID3D11Buffer>b;if(FAILED(r->QueryInterface(IID_PPV_ARGS(&b))))return;D3D11_BUFFER_DESC d{};b->GetDesc(&d);if(!d.ByteWidth||d.ByteWidth>65536)return;std::scoped_lock l(g.mutex);auto&e=g.buffers[b.Get()];e.buffer=b;b->GetDevice(&e.device);e.bytes.resize(d.ByteWidth);e.mapped=data;}
void D3D11CameraTracker::onUnmap(ID3D11Resource*r){if(!r)return;ComPtr<ID3D11Buffer>b;if(FAILED(r->QueryInterface(IID_PPV_ARGS(&b))))return;std::scoped_lock l(g.mutex);auto it=g.buffers.find(b.Get());if(it!=g.buffers.end()&&it->second.mapped&&!it->second.bytes.empty()){std::memcpy(it->second.bytes.data(),it->second.mapped,it->second.bytes.size());it->second.mapped=nullptr;}}
void D3D11CameraTracker::onDraw(ID3D11DeviceContext*c){if(!c)return;std::scoped_lock l(g.mutex);auto ci=g.contexts.find(c);if(ci==g.contexts.end()||!ci->second.vs)return;auto si=g.shaders.find(ci->second.vs.Get());if(si==g.shaders.end())return;for(const auto&binding:si->second.bindings){if(binding.kind!=CameraMatrixKind::ViewProjection)continue;if(binding.slot>=ci->second.cbs.size())continue;auto*b=ci->second.cbs[binding.slot].Get();if(!b)continue;auto bd=g.buffers.find(b);if(bd==g.buffers.end()||bd->second.bytes.size()<binding.offset+64)continue;const float*m=reinterpret_cast<const float*>(bd->second.bytes.data()+binding.offset);if(!usefulMatrix(m))continue;auto&fc=g.frameCandidates[key(ci->second.vs.Get(),b,binding.offset)];if(binding.columnMajor){for(int r=0;r<4;r++)for(int c2=0;c2<4;c2++)fc.matrix[r*4+c2]=m[c2*4+r];}else std::copy(m,m+16,fc.matrix.begin());fc.device=bd->second.device;++fc.hits;}}
void D3D11CameraTracker::finalizeFrame(ID3D11Device*device){if(!device)return;std::scoped_lock l(g.mutex);FrameCandidate*best=nullptr;for(auto&[_,c]:g.frameCandidates)if(sameDevice(c.device.Get(),device)&&(!best||c.hits>best->hits))best=&c;auto&ds=g.devices[device];++ds.frames;if(best&&best->hits>=2){ds.snapshot.previousViewProjection=ds.lastVp;ds.snapshot.previousValid=ds.lastValid;ds.snapshot.currentViewProjection=best->matrix;ds.snapshot.currentValid=true;ds.snapshot.confidence=std::min<std::uint32_t>(100,40+best->hits*2+std::min<std::uint32_t>(20,ds.frames*2));ds.lastVp=best->matrix;ds.lastValid=true;}else{ds.snapshot={};}g.frameCandidates.clear();}
CameraMatrixSnapshot D3D11CameraTracker::snapshot(ID3D11Device*device)const{std::scoped_lock l(g.mutex);for(const auto&[d,s]:g.devices)if(sameDevice(d,device))return s.snapshot;return {};}
void D3D11CameraTracker::reset(){std::scoped_lock l(g.mutex);g.shaders.clear();g.buffers.clear();g.contexts.clear();g.frameCandidates.clear();g.devices.clear();}

bool installD3D11CameraTrackingHooks(ID3D11Device*device,ID3D11DeviceContext*context){if(!device||!context)return false;void**dv=*(void***)device;void**cv=*(void***)context;bool ok=true;ok&=hook(dv[12],(void*)hkCreateVS,(void**)&origCreateVS);ok&=hook(cv[11],(void*)hkVS,(void**)&origVS);ok&=hook(cv[7],(void*)hkCB,(void**)&origCB);ok&=hook(cv[48],(void*)hkUpdate,(void**)&origUpdate);ok&=hook(cv[14],(void*)hkMap,(void**)&origMap);ok&=hook(cv[15],(void*)hkUnmap,(void**)&origUnmap);return ok;}

} // namespace udlss::gpu
