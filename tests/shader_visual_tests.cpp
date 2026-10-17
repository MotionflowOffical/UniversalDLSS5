#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <vector>
#include <filesystem>
#include <iostream>
#include <cmath>
#include <stdexcept>
#include "../include/udlss/motion_route_policy.hpp"
#include "../include/udlss/temporal_frame.hpp"
using Microsoft::WRL::ComPtr;
static void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
struct Pixel{float r,g,b,a;};
struct Params {
 unsigned w{},h{},ds{1},radius{2};float exposure{1},motionScale{1},confidence{.1f},text{};
 float ui{},edge{},sharp{},reactive{};float control{},clamp{},disocclusion{},temporal{1};
 unsigned invert{},history{1},pad0{},pad1{};float deadzone{},scaleX{1},scaleY{1},split{};
 unsigned view{},depthMode{},srgb{},useMask{},encoding{},hdr{};float paper{203},maxNits{1000};
};
struct Texture {ComPtr<ID3D11Texture2D> t;ComPtr<ID3D11ShaderResourceView> s;ComPtr<ID3D11UnorderedAccessView> u;};
struct Gpu {
 ComPtr<ID3D11Device>d;ComPtr<ID3D11DeviceContext>c;ComPtr<ID3D11Buffer>cb;
 Gpu(){check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&d,nullptr,&c)),"D3D11 WARP");D3D11_BUFFER_DESC b{};b.ByteWidth=sizeof(Params);b.Usage=D3D11_USAGE_DEFAULT;b.BindFlags=D3D11_BIND_CONSTANT_BUFFER;check(SUCCEEDED(d->CreateBuffer(&b,nullptr,&cb)),"constant buffer");}
 Texture texture(unsigned w,unsigned h,DXGI_FORMAT f,const void* data=nullptr,unsigned pitch=0){
  Texture t;D3D11_TEXTURE2D_DESC x{};x.Width=w;x.Height=h;x.MipLevels=x.ArraySize=x.SampleDesc.Count=1;x.Format=f;x.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS|D3D11_BIND_RENDER_TARGET;
  D3D11_SUBRESOURCE_DATA init{data,pitch,0};check(SUCCEEDED(d->CreateTexture2D(&x,data?&init:nullptr,&t.t)),"texture");check(SUCCEEDED(d->CreateShaderResourceView(t.t.Get(),nullptr,&t.s)),"SRV");check(SUCCEEDED(d->CreateUnorderedAccessView(t.t.Get(),nullptr,&t.u)),"UAV");return t;
 }
 void run(const wchar_t* file,const Params&p,std::initializer_list<ID3D11ShaderResourceView*> input,Texture&out){
  ComPtr<ID3DBlob>b,e;auto path=std::filesystem::path(UDLSS_SOURCE_DIR)/L"shaders"/file;
  auto hr=D3DCompileFromFile(path.c_str(),nullptr,D3D_COMPILE_STANDARD_FILE_INCLUDE,"CSMain","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&b,&e);
  if(FAILED(hr)&&e)std::cerr<<(const char*)e->GetBufferPointer();check(SUCCEEDED(hr),"compile shader");ComPtr<ID3D11ComputeShader> cs;check(SUCCEEDED(d->CreateComputeShader(b->GetBufferPointer(),b->GetBufferSize(),nullptr,&cs)),"compute shader");
  c->UpdateSubresource(cb.Get(),0,nullptr,&p,0,0);auto buf=cb.Get();c->CSSetConstantBuffers(0,1,&buf);c->CSSetShader(cs.Get(),nullptr,0);c->CSSetShaderResources(0,(UINT)input.size(),input.begin());auto u=out.u.Get();c->CSSetUnorderedAccessViews(0,1,&u,nullptr);c->Dispatch((p.w+p.ds-1)/p.ds/8+1,(p.h+p.ds-1)/p.ds/8+1,1);
  std::array<ID3D11ShaderResourceView*,8> nil{};c->CSSetShaderResources(0,8,nil.data());u=nullptr;c->CSSetUnorderedAccessViews(0,1,&u,nullptr);
 }
 std::vector<Pixel> read(Texture&t){
  D3D11_TEXTURE2D_DESC x{};t.t->GetDesc(&x);check(x.Format==DXGI_FORMAT_R32G32B32A32_FLOAT,"float readback");x.BindFlags=0;x.Usage=D3D11_USAGE_STAGING;x.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Texture2D> staging;check(SUCCEEDED(d->CreateTexture2D(&x,nullptr,&staging)),"test-only staging");c->CopyResource(staging.Get(),t.t.Get());D3D11_MAPPED_SUBRESOURCE m{};check(SUCCEEDED(c->Map(staging.Get(),0,D3D11_MAP_READ,0,&m)),"readback");std::vector<Pixel> v(x.Width*x.Height);for(unsigned y=0;y<x.Height;y++)memcpy(v.data()+y*x.Width,(const char*)m.pData+y*m.RowPitch,x.Width*sizeof(Pixel));c->Unmap(staging.Get(),0);return v;
 }
 void scene(unsigned frame,bool hdr,Texture&color,Texture&depth,Texture&motion){
  auto path=std::filesystem::path(UDLSS_SOURCE_DIR)/L"src/testapp/scene.hlsl";ComPtr<ID3DBlob>vs,ps,e;
  check(SUCCEEDED(D3DCompileFromFile(path.c_str(),nullptr,D3D_COMPILE_STANDARD_FILE_INCLUDE,"VSMain","vs_5_0",0,0,&vs,&e)),"scene vertex shader");
  auto hr=D3DCompileFromFile(path.c_str(),nullptr,D3D_COMPILE_STANDARD_FILE_INCLUDE,"PSMain","ps_5_0",0,0,&ps,&e);if(FAILED(hr)&&e)std::cerr<<(char*)e->GetBufferPointer();check(SUCCEEDED(hr),"scene pixel shader");
  ComPtr<ID3D11VertexShader>v;ComPtr<ID3D11PixelShader>p;d->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&v);d->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&p);
  D3D11_TEXTURE2D_DESC desc{};color.t->GetDesc(&desc);Params values;values.w=desc.Width;values.h=desc.Height;values.ds=frame;values.radius=hdr;
  c->UpdateSubresource(cb.Get(),0,nullptr,&values,0,0);auto buf=cb.Get();c->PSSetConstantBuffers(0,1,&buf);c->VSSetShader(v.Get(),nullptr,0);c->PSSetShader(p.Get(),nullptr,0);
  ComPtr<ID3D11RenderTargetView>rt[3];d->CreateRenderTargetView(color.t.Get(),nullptr,&rt[0]);d->CreateRenderTargetView(depth.t.Get(),nullptr,&rt[1]);d->CreateRenderTargetView(motion.t.Get(),nullptr,&rt[2]);ID3D11RenderTargetView* raw[]={rt[0].Get(),rt[1].Get(),rt[2].Get()};c->OMSetRenderTargets(3,raw,nullptr);D3D11_VIEWPORT vp{0,0,(float)desc.Width,(float)desc.Height,0,1};c->RSSetViewports(1,&vp);c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);c->Draw(3,0);c->OMSetRenderTargets(0,nullptr,nullptr);
 }
};
static float srgb(float x){return x<=.0031308f?12.92f*x:1.055f*std::pow(x,1/2.4f)-.055f;}
int main(){try{
 Gpu g;constexpr unsigned w=96,h=64;Params p;p.w=w;p.h=h;
 auto sceneColor=g.texture(320,180,DXGI_FORMAT_R32G32B32A32_FLOAT),sceneDepth=g.texture(320,180,DXGI_FORMAT_R32G32B32A32_FLOAT),sceneMotion=g.texture(320,180,DXGI_FORMAT_R32G32B32A32_FLOAT);
 g.scene(0,false,sceneColor,sceneDepth,sceneMotion);auto initial=g.read(sceneMotion);for(auto v:initial)check(std::abs(v.r)<.001&&std::abs(v.g)<.001,"camera cut must have zero history motion");
 g.scene(1,true,sceneColor,sceneDepth,sceneMotion);auto visible=g.read(sceneColor),knownDepth=g.read(sceneDepth),knownMotion=g.read(sceneMotion);unsigned moving=0,geometry=0,bright=0;
 for(unsigned i=0;i<visible.size();i++){check(std::isfinite(visible[i].r)&&knownDepth[i].r>=0&&knownDepth[i].r<=1,"finite scene and normalized depth");moving+=std::abs(knownMotion[i].r)+std::abs(knownMotion[i].g)>.01;geometry+=knownDepth[i].r<.999;bright+=visible[i].r>1;}
 check(moving>1000&&geometry>1000&&bright>1000,"3D scene must contain real geometric motion, depth and HDR highlights");
 std::vector<float> cur(w*h),prev(w*h);
 auto pattern=[](int x,int y){return .5f+.2f*std::sin(x*.37f+y*.19f)+.15f*std::cos(y*.43f-x*.11f);};
 for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++){prev[y*w+x]=pattern(x,y);cur[y*w+x]=pattern((int)x+8,y);}
 auto a=g.texture(w,h,DXGI_FORMAT_R32_FLOAT,cur.data(),w*4),b=g.texture(w,h,DXGI_FORMAT_R32_FLOAT,prev.data(),w*4),o=g.texture(w,h,DXGI_FORMAT_R32G32B32A32_FLOAT);
 g.run(L"flow.hlsl",p,{a.s.Get(),b.s.Get()},o);auto f=g.read(o);unsigned good=0,total=0;for(unsigned y=16;y<h-16;y++)for(unsigned x=20;x<w-20;x++){auto v=f[y*w+x];good+=std::abs(v.r-8)<.6f&&std::abs(v.g)<.6f;total++;}std::cout<<"large-motion exact fraction="<<(double)good/total<<"\n";check(good>total*.8,"hierarchical flow must recover 8px translation beyond radius 2");
 p.radius=4;for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++)cur[y*w+x]=pattern((int)x+16,y);g.c->UpdateSubresource(a.t.Get(),0,nullptr,cur.data(),w*4,0);g.run(L"flow.hlsl",p,{a.s.Get(),b.s.Get()},o);f=g.read(o);good=total=0;for(unsigned y=16;y<h-16;y++)for(unsigned x=20;x<w-20;x++){auto v=f[y*w+x];good+=std::abs(v.r-16)<.6f&&std::abs(v.g)<.6f;total++;}check(good>total*.8,"larger configured search radius must expand motion capture range");
 std::fill(cur.begin(),cur.end(),.5f);g.c->UpdateSubresource(a.t.Get(),0,nullptr,cur.data(),w*4,0);g.c->UpdateSubresource(b.t.Get(),0,nullptr,cur.data(),w*4,0);g.run(L"flow.hlsl",p,{a.s.Get(),b.s.Get()},o);f=g.read(o);for(auto v:f)check(v.r==0&&v.g==0&&v.b<.01f,"ambiguous patches must reject motion");
 // A stable current neural result must not pulse when trusted native guides
 // reset source-history reactivity. Reproduce the reactive mask transition on
 // the GPU; NR output itself remains fixed, as in the live flicker isolation.
 {
  Params light;light.w=light.h=8;light.pad1=udlss::postCompositeMotionFlags(udlss::MotionRoute::AdapterNative);light.clamp=.75f;light.reactive=.25f;light.edge=.12f;
  std::vector<Pixel> source(64,{.2f,.2f,.2f,1}),neuralLight(64,{.8f,.8f,.8f,1});
  std::vector<std::array<float,2>> zeroMotion(64,{0,0});
  auto sourceTex=g.texture(8,8,DXGI_FORMAT_R32G32B32A32_FLOAT,source.data(),8*16);
  auto neuralTex=g.texture(8,8,DXGI_FORMAT_R32G32B32A32_FLOAT,neuralLight.data(),8*16);
  auto motionTex=g.texture(8,8,DXGI_FORMAT_R32G32_FLOAT,zeroMotion.data(),8*8);
  auto reactiveMask=g.texture(8,8,DXGI_FORMAT_R32G32B32A32_FLOAT),result=g.texture(8,8,DXGI_FORMAT_R32G32B32A32_FLOAT);
  auto composite=[&]{const auto postFlags=light.pad1;light.pad1=2;g.run(L"mask.hlsl",light,{sourceTex.s.Get(),sourceTex.s.Get(),nullptr,motionTex.s.Get()},reactiveMask);light.pad1=postFlags;g.run(L"post.hlsl",light,{neuralTex.s.Get(),sourceTex.s.Get(),reactiveMask.s.Get()},result);return g.read(result);};
  light.history=1;auto stable=composite();light.history=0;auto reset=composite();
  for(unsigned i=0;i<64;++i){check(std::abs(stable[i].r-.8f)<.0001f,"trusted stable neural strength");check(std::abs(reset[i].r-stable[i].r)<.0001f,"trusted native reactive reset makes a stable neural light flicker");}
  // Preserve requested effect strength, fallback attenuation and UI bypass.
  light.temporal=.6f;auto partial=composite();for(auto pixel:partial)check(std::abs(pixel.r-.56f)<.0001f,"trusted output must preserve requested effect strength");
  light.temporal=1;light.pad1=0;g.run(L"post.hlsl",light,{neuralTex.s.Get(),sourceTex.s.Get(),reactiveMask.s.Get()},result);
  for(auto pixel:g.read(result))check(std::abs(pixel.r-.35f)<.0001f,"untrusted fallback must retain reactive attenuation");
  light.pad1=2;g.run(L"post.hlsl",light,{neuralTex.s.Get(),sourceTex.s.Get(),reactiveMask.s.Get()},result);
  for(auto pixel:g.read(result))check(std::abs(pixel.r-.35f)<.0001f,"camera-only mask confidence must not bypass post reactive attenuation");
  for(auto route:{udlss::MotionRoute::CameraDepth,udlss::MotionRoute::Hlsl,udlss::MotionRoute::Nvof,udlss::MotionRoute::Zero}){
   light.pad1=udlss::postCompositeMotionFlags(route);g.run(L"post.hlsl",light,{neuralTex.s.Get(),sourceTex.s.Get(),reactiveMask.s.Get()},result);
   for(auto pixel:g.read(result))check(std::abs(pixel.r-.35f)<.0001f,"non-native motion route must retain fallback attenuation");
  }
  light.pad1=udlss::postCompositeMotionFlags(udlss::MotionRoute::TrackedNative);std::vector<Pixel> uiMask(64,{0,1,0,1});g.c->UpdateSubresource(reactiveMask.t.Get(),0,nullptr,uiMask.data(),8*16,0);
  g.run(L"post.hlsl",light,{neuralTex.s.Get(),sourceTex.s.Get(),reactiveMask.s.Get()},result);
  for(auto pixel:g.read(result))check(std::abs(pixel.r-.2f)<.0001f,"explicit protected UI coverage must retain source pixels");
  light.hdr=1;light.ui=light.text=0;light.history=1;
  for(auto& pixel:neuralLight)pixel={srgb(.15f),srgb(.15f),srgb(.15f),1};
  g.c->UpdateSubresource(neuralTex.t.Get(),0,nullptr,neuralLight.data(),8*16,0);
  auto hdrStable=composite();light.history=0;auto hdrReset=composite();
  for(unsigned i=0;i<64;++i){check(std::abs(hdrStable[i].r-.410625f)<.0001f,"native HDR residual strength");check(std::abs(hdrReset[i].r-hdrStable[i].r)<.0001f,"native HDR output must not pulse with source reactivity");}
  // A source-history reset also changes heuristic UI coverage on static
  // bright edges. A duplicate provider token resets NR, not this color cache.
  light.hdr=0;light.ui=.9f;light.text=.8f;
  for(unsigned i=0;i<64;++i){source[i]=i%8<4?Pixel{.2f,.2f,.2f,1}:Pixel{.8f,.8f,.8f,1};neuralLight[i]={.8f,.8f,.8f,1};}
  g.c->UpdateSubresource(sourceTex.t.Get(),0,nullptr,source.data(),8*16,0);g.c->UpdateSubresource(neuralTex.t.Get(),0,nullptr,neuralLight.data(),8*16,0);
  light.history=1;auto protectedEdge=composite();
  udlss::TemporalFrame previous{};previous.flags=udlss::Temporal_SourceFrameKnown;previous.sourceFrame=10;previous.viewport=1;previous.outputWidth=previous.outputHeight=8;
  auto repeated=previous;check(udlss::temporalDiscontinuity(previous,repeated),"duplicate token must still reset neural history");
  light.history=udlss::canReusePresentedColorHistory(previous,repeated)?1u:0u;auto repeatedEdge=composite();
  check(std::abs(protectedEdge[3].r-.26f)<.0001f,"UI protection fixture must actually preserve source coverage");
  for(unsigned i=0;i<64;++i)check(std::abs(repeatedEdge[i].r-protectedEdge[i].r)<.0001f,"duplicate source token makes static protected light edges flicker");
 }
 // Neutral reconstruction preserves original HDR energy; a nonzero neural
 // residual must be able to introduce detail into black source pixels.
 p.w=8;p.h=8;p.hdr=1;std::vector<Pixel> original(64,{0,0,0,1}),neural(64,{srgb(.05f),srgb(.04f),srgb(.03f),1}),mask(64,{0,0,0,0});
 auto n=g.texture(8,8,DXGI_FORMAT_R32G32B32A32_FLOAT,neural.data(),8*16),c=g.texture(8,8,DXGI_FORMAT_R32G32B32A32_FLOAT,original.data(),8*16),m=g.texture(8,8,DXGI_FORMAT_R32G32B32A32_FLOAT,mask.data(),8*16),out=g.texture(8,8,DXGI_FORMAT_R32G32B32A32_FLOAT);
 g.run(L"post.hlsl",p,{n.s.Get(),c.s.Get(),m.s.Get()},out);auto v=g.read(out);check(v[20].r>.05f,"HDR residual must retain newly inferred shadow detail");
 for(unsigned i=0;i<64;i++){float value=(i%8)*2.f;original[i]={value,value*.5f,value*.25f,1};float denom=value+p.paper/80.f;neural[i]={srgb(value/denom),srgb(value*.5f/denom),srgb(value*.25f/denom),1};}
 g.c->UpdateSubresource(n.t.Get(),0,nullptr,neural.data(),8*16,0);g.c->UpdateSubresource(c.t.Get(),0,nullptr,original.data(),8*16,0);g.run(L"post.hlsl",p,{n.s.Get(),c.s.Get(),m.s.Get()},out);v=g.read(out);for(unsigned i=0;i<64;i++)check(std::abs(v[i].r-original[i].r)<.005f&&std::abs(v[i].g-original[i].g)<.005f,"neutral HDR proxy must preserve source highlights and chroma");
 std::cout<<"PASS: GPU flow and HDR pixel checks\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 1;}}
