#include "../src/neural/backend.hpp"
#include "../src/neural/external_host_protocol.hpp"
#include "udlss/external_host_policy.hpp"
#include <wrl/client.h>
#include <vector>
#include <iostream>
#include <memory>
#include <stdexcept>
using Microsoft::WRL::ComPtr;
void check(bool b,const char*why){if(!b)throw std::runtime_error(why);}
int wmain(int argc,wchar_t**argv){try{
 if(argc<2)return 77;
 ComPtr<ID3D11Device>d;ComPtr<ID3D11DeviceContext>c;check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&d,nullptr,&c)),"D3D11 device");
 constexpr UINT w=640,h=360;std::vector<unsigned char> color(w*h*4),mask(w*h*4,255);std::vector<unsigned short> motion(w*h*2,0);std::vector<float> depth(w*h,.5f);
 for(UINT y=0;y<h;y++)for(UINT x=0;x<w;x++){auto i=(y*w+x)*4;color[i]=(unsigned char)(40+150*x/w);color[i+1]=(unsigned char)(30+120*y/h);color[i+2]=(unsigned char)(50+((x/16+y/16)%2)*80);color[i+3]=255;}
 auto texture=[&](DXGI_FORMAT f,const void*data,UINT pitch){D3D11_TEXTURE2D_DESC desc{};desc.Width=w;desc.Height=h;desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;desc.Format=f;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;D3D11_SUBRESOURCE_DATA init{data,pitch,0};ComPtr<ID3D11Texture2D>t;check(SUCCEEDED(d->CreateTexture2D(&desc,data?&init:nullptr,&t)),"D3D11 texture");return t;};
 auto input=texture(DXGI_FORMAT_R8G8B8A8_UNORM,color.data(),w*4),output=texture(DXGI_FORMAT_R8G8B8A8_UNORM,nullptr,0),mv=texture(DXGI_FORMAT_R16G16_FLOAT,motion.data(),w*4),z=texture(DXGI_FORMAT_R32_FLOAT,depth.data(),w*4),control=texture(DXGI_FORMAT_R8G8B8A8_UNORM,mask.data(),w*4);
 auto s=udlss::defaultSettings();s.backend=udlss::BackendMode::ExternalHostNR;s.attemptUnsupportedHardware=true;s.useControlMask=false;s.nrPasses=1;udlss::RuntimeStatus status{};
 std::unique_ptr<udlss::neural::Backend>backend(udlss::neural::createExternalHostNR());check(backend->initialize({d.Get(),c.Get()},argv[1],s,status),"D3D11 host initialize");
 udlss::neural::FrameResources frame{};frame.input=input.Get();frame.output=output.Get();frame.motion=mv.Get();frame.depth=z.Get();frame.width=w;frame.height=h;frame.inputFormat=DXGI_FORMAT_R8G8B8A8_UNORM;frame.depthInverted=false;
 double lastMean=0,lastDiff=0;
 for(unsigned i=0;i<9;i++){
  if(i==3){s.useControlMask=true;frame.controlMask=control.Get();}if(i==6){s.useControlMask=false;frame.controlMask=nullptr;}frame.resetHistory=i==0;
  const auto startupDeadline=GetTickCount64()+10000;
  bool ok=backend->evaluate(c.Get(),frame,s,status);
  // Only initial feature warmup may retry a soft busy response. Resource rebinds
  // and all hard errors retain the strict single-evaluation checks below.
  while(i==0&&!ok&&status.failureStage==udlss::PipelineStage::None&&GetTickCount64()<startupDeadline){
   std::wcout<<L"D3D11 startup busy: "<<status.message<<std::endl;
   Sleep(20);ok=backend->evaluate(c.Get(),frame,s,status);
  }
  if(!ok)std::wcerr<<status.message<<L"\n";check(ok&&status.neuralActive&&status.neuralPassesExecuted==1,"D3D11 one-pass evaluation");
  if(i%3==0){
   D3D11_TEXTURE2D_DESC desc{};output->GetDesc(&desc);desc.BindFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Texture2D>read;check(SUCCEEDED(d->CreateTexture2D(&desc,nullptr,&read)),"test-only staging");c->CopyResource(read.Get(),output.Get());D3D11_MAPPED_SUBRESOURCE m{};check(SUCCEEDED(c->Map(read.Get(),0,D3D11_MAP_READ,0,&m)),"test readback");double mean=0,diff=0;
   for(UINT y=0;y<h;y++)for(UINT x=0;x<w*4;x++)if(x%4!=3){auto value=((unsigned char*)m.pData)[y*m.RowPitch+x];mean+=value;diff+=std::abs(int(value)-int(color[y*w*4+x]));}c->Unmap(read.Get(),0);lastMean=mean/(w*h*3);lastDiff=diff/(w*h*3);check(lastMean>10&&lastMean<245&&lastDiff>.05,"visible changed D3D11 output across mask rebinds");
  }
 }
 auto names=udlss::makeNrHostSessionNames(GetCurrentProcessId(),1);HANDLE mapping=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,names.map.c_str());check(mapping!=nullptr,"diagnostic mapping");auto*shared=(udlss::neural::hostipc::Shared*)MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(udlss::neural::hostipc::Shared));check(shared!=nullptr,"host diagnostics");
 std::cout<<"feature creations="<<shared->featureCreations<<" reuses="<<shared->featureReuses<<" mean="<<lastMean<<" difference="<<lastDiff<<"\n";check(shared->featureCreations==1&&shared->featureReuses==2,"D3D11 resource-only rebind must preserve the feature");
 const auto resourceGeneration=shared->resourceGeneration;const LONG configGeneration=shared->configGeneration,frameSeq=shared->frameSeq;
 InterlockedExchange(&shared->state,(LONG)udlss::neural::hostipc::State::Configuring);s.useControlMask=true;frame.controlMask=control.Get();
 check(!backend->evaluate(c.Get(),frame,s,status),"in-flight host configuration must keep batch ownership");
 check(shared->resourceGeneration==resourceGeneration&&shared->configGeneration==configGeneration&&shared->frameSeq==frameSeq,"in-flight configuration must not be overwritten");
 shared->failureStage=(UINT)udlss::PipelineStage::HostRuntimeFailed;shared->lastResult=WAIT_TIMEOUT;wcscpy_s(shared->message,L"injected terminal host error");InterlockedExchange(&shared->state,(LONG)udlss::neural::hostipc::State::Error);
 check(!backend->evaluate(c.Get(),frame,s,status),"terminal host error must wait for explicit retry");
 check(shared->resourceGeneration==resourceGeneration&&shared->configGeneration==configGeneration&&shared->frameSeq==frameSeq,"terminal host error must not create a configuration storm");
 UnmapViewOfFile(shared);CloseHandle(mapping);return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 1;}}
