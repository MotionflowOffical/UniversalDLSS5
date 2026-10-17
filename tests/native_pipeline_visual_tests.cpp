#include "../src/neural/external_host_protocol.hpp"
#include "udlss/external_host_policy.hpp"
#include "../src/gpu/d3d12_native_pipeline.hpp"
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <vector>
#include <iostream>
#include <stdexcept>
#include <cmath>
#include <algorithm>
#include <fstream>
using Microsoft::WRL::ComPtr;
void check(bool b,const char* m){if(!b)throw std::runtime_error(m);}
int wmain(int argc,wchar_t**argv){try{
 if(argc<2){std::cout<<"SKIP: pass runtime directory for the real NVIDIA pixel test\n";return 77;}
 ComPtr<ID3D12Device>d;check(SUCCEEDED(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&d))),"device");ComPtr<ID3D12CommandQueue>q;D3D12_COMMAND_QUEUE_DESC qd{};check(SUCCEEDED(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q))),"queue");
 const UINT w=argc>2?std::clamp((UINT)_wtoi(argv[2]),320u,3840u):640,h=argc>3?std::clamp((UINT)_wtoi(argv[3]),180u,2160u):360;D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=w;desc.Height=h;desc.DepthOrArraySize=desc.MipLevels=desc.SampleDesc.Count=1;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
 ComPtr<ID3D12Resource>color,depth,motion;check(SUCCEEDED(d->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&color))),"color");desc.Format=DXGI_FORMAT_R32_FLOAT;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;check(SUCCEEDED(d->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_SHARED,&desc,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&depth))),"depth");desc.Format=DXGI_FORMAT_R32G32_FLOAT;check(SUCCEEDED(d->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_SHARED,&desc,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&motion))),"motion");
 ComPtr<ID3D12CommandAllocator>a;ComPtr<ID3D12GraphicsCommandList>l;d->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&a));d->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,a.Get(),nullptr,IID_PPV_ARGS(&l));l->Close();
 ComPtr<ID3D12Fence>f;d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&f));HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);UINT64 seq=0;
 auto submit=[&]{check(SUCCEEDED(l->Close()),"close");ID3D12CommandList* lists[]={l.Get()};q->ExecuteCommandLists(1,lists);q->Signal(f.Get(),++seq);f->SetEventOnCompletion(seq,event);check(WaitForSingleObject(event,5000)==WAIT_OBJECT_0,"test fence");};
 auto upload=[&](ID3D12Resource*r,const void*src,UINT pitch){auto rd=r->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 bytes{};d->GetCopyableFootprints(&rd,0,1,0,&fp,nullptr,nullptr,&bytes);D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_UPLOAD;D3D12_RESOURCE_DESC bd{};bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;bd.Width=bytes;bd.Height=bd.DepthOrArraySize=bd.MipLevels=bd.SampleDesc.Count=1;bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;ComPtr<ID3D12Resource>u;check(SUCCEEDED(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&u))),"upload");void*p{};u->Map(0,nullptr,&p);for(UINT y=0;y<h;y++)memcpy((char*)p+y*fp.Footprint.RowPitch,(const char*)src+y*pitch,pitch);u->Unmap(0,nullptr);a->Reset();l->Reset(a.Get(),nullptr);auto tr=udlss::gpu::D3D12NativePipeline::barrier(r,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST);l->ResourceBarrier(1,&tr);D3D12_TEXTURE_COPY_LOCATION dst{};dst.pResource=r;dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;D3D12_TEXTURE_COPY_LOCATION from{};from.pResource=u.Get();from.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;from.PlacedFootprint=fp;l->CopyTextureRegion(&dst,0,0,0,&from,nullptr);tr=udlss::gpu::D3D12NativePipeline::barrier(r,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COMMON);l->ResourceBarrier(1,&tr);submit();/* submit waits for GPU completion, so the upload can retire here. */};
 std::vector<unsigned char> pixels(w*h*4);std::vector<float> z(w*h,.5f),mv(w*h*2,0);
 for(UINT y=0;y<h;y++)for(UINT x=0;x<w;x++){auto i=(y*w+x)*4;pixels[i]=(unsigned char)(40+150*x/w);pixels[i+1]=(unsigned char)(30+120*y/h);pixels[i+2]=(unsigned char)(50+40*std::sin(x*.2)*std::cos(y*.2));pixels[i+3]=255;}
 upload(color.Get(),pixels.data(),w*4);upload(depth.Get(),z.data(),w*4);upload(motion.Get(),mv.data(),w*8);
 udlss::gpu::CapturedGameGuides guides{};guides.depth12=depth;guides.motion12=motion;guides.depthFormat=DXGI_FORMAT_R32_FLOAT;guides.motionFormat=DXGI_FORMAT_R32G32_FLOAT;guides.depthWidth=guides.motionWidth=w;guides.depthHeight=guides.motionHeight=h;guides.motionConventionKnown=guides.depthConventionKnown=guides.snapshotOwned=guides.depthSnapshotOwned=guides.motionSnapshotOwned=true;guides.confidence=100;auto lease=std::make_shared<int>();guides.snapshotLease=lease;guides.temporal.flags=udlss::Temporal_SourceFrameKnown;guides.temporal.renderWidth=w;guides.temporal.renderHeight=h;
 auto pipe=std::make_unique<udlss::gpu::D3D12NativePipeline>();pipe->invalidateHistory();udlss::RuntimeStatus st{};auto s=udlss::defaultSettings();s.backend=udlss::BackendMode::ExternalHostNR;s.attemptUnsupportedHardware=true;s.nrPasses=1;s.sharpness=0;
 wchar_t exe[MAX_PATH]{};GetModuleFileNameW(nullptr,exe,MAX_PATH);auto dir=std::filesystem::path(exe).parent_path().wstring();
 // Menu-style D3D11 warmup must hand off to native D3D12 without creating
 // another feature after a game has filled VRAM.
 ComPtr<ID3D11Device>producer;ComPtr<ID3D11DeviceContext>producerContext;
 check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&producer,nullptr,&producerContext)),"D3D11 warmup producer");
 auto texture11=[&](DXGI_FORMAT format,const void*data,UINT pitch){D3D11_TEXTURE2D_DESC td{};td.Width=w;td.Height=h;td.MipLevels=td.ArraySize=td.SampleDesc.Count=1;td.Format=format;td.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;D3D11_SUBRESOURCE_DATA init{data,pitch,0};ComPtr<ID3D11Texture2D>r;check(SUCCEEDED(producer->CreateTexture2D(&td,data?&init:nullptr,&r)),"warmup texture");return r;};
 auto warmInput=texture11(DXGI_FORMAT_R8G8B8A8_UNORM,pixels.data(),w*4),warmOutput=texture11(DXGI_FORMAT_R8G8B8A8_UNORM,nullptr,0),warmMotion=texture11(DXGI_FORMAT_R16G16_FLOAT,mv.data(),w*4),warmDepth=texture11(DXGI_FORMAT_R32_FLOAT,z.data(),w*4);
 std::shared_ptr<udlss::neural::Backend>sharedHost(udlss::neural::createExternalHostNR());
 check(sharedHost->initialize({producer.Get(),producerContext.Get(),d.Get(),q.Get()},argv[1],s,st),"shared host startup");
 udlss::neural::FrameResources warm{};warm.input=warmInput.Get();warm.output=warmOutput.Get();warm.motion=warmMotion.Get();warm.depth=warmDepth.Get();warm.width=w;warm.height=h;warm.inputFormat=DXGI_FORMAT_R8G8B8A8_UNORM;warm.resetHistory=true;
 // Feature creation can consume the first frame's pacing deadline. Retry only
 // soft startup responses, retaining identical inputs and the exact host/fences.
 // Hard host failures must still fail this test immediately.
 auto warmHost=[&]{
  const auto startupDeadline=GetTickCount64()+10000;
  bool warmed=sharedHost->evaluate(producerContext.Get(),warm,s,st);
  while(!warmed&&st.failureStage==udlss::PipelineStage::None&&GetTickCount64()<startupDeadline){
   std::wcout<<L"D3D11 startup busy: "<<st.message<<std::endl;
   Sleep(20);warmed=sharedHost->evaluate(producerContext.Get(),warm,s,st);
  }
  return warmed&&st.neuralActive&&st.neuralPassesExecuted==1;
 };
 if(!warmHost()){
  std::wcerr<<L"D3D11 warmup failed: stage="<<(unsigned)st.failureStage<<L" hr="<<std::hex<<st.lastResult<<std::dec<<L" "<<st.message<<std::endl;
  throw std::runtime_error("D3D11 warmup frame");
 }
 const unsigned stressCycles=argc>5&&std::wstring(argv[4])==L"--stress"?std::clamp((unsigned)_wtoi(argv[5]),1u,10000u):0;
 if(argc>4&&!stressCycles){
  HANDLE gate=OpenEventW(SYNCHRONIZE,FALSE,argv[4]);check(gate!=nullptr,"warm pressure gate");std::cout<<"WARM_READY"<<std::endl;
  check(WaitForSingleObject(gate,30000)==WAIT_OBJECT_0,"warm pressure gate timeout");CloseHandle(gate);
 }

 for(unsigned i=0;i<12;i++){
 if(i==8){
  warm.resetHistory=false;
  for(unsigned fallback=0;fallback<3;fallback++){
   pipe->invalidateHistory();check(sharedHost->evaluate(producerContext.Get(),warm,s,st),"return to D3D11 transport");
   if(fallback==2){auto session=udlss::makeNrHostSessionNames(GetCurrentProcessId(),1);HANDLE map=OpenFileMappingW(FILE_MAP_READ,FALSE,session.map.c_str());check(map!=nullptr,"fallback diagnostic mapping");auto* wire=(const udlss::neural::hostipc::Shared*)MapViewOfFile(map,FILE_MAP_READ,0,0,sizeof(udlss::neural::hostipc::Shared));check(wire&&wire->frameReset==0,"continuous fallback must retain shared neural history");UnmapViewOfFile(wire);CloseHandle(map);}
  }
 }
 guides.temporal.flags=(i>=3&&i<=4)?0:udlss::Temporal_SourceFrameKnown;upload(color.Get(),pixels.data(),w*4);guides.temporal.sourceFrame=i;unsigned warmup=0;bool processed=pipe->process(d.Get(),q.Get(),color.Get(),guides,s,argv[1],dir,st,nullptr,sharedHost);while(!processed&&i==0&&warmup++<100){Sleep(100);processed=pipe->process(d.Get(),q.Get(),color.Get(),guides,s,argv[1],dir,st,nullptr,sharedHost);}
if(!processed){std::wcerr<<L"native failed: stage="<<(unsigned)st.failureStage<<L" hr="<<std::hex<<st.lastResult<<std::dec<<L" "<<st.message<<L"\n";throw std::runtime_error("native pipeline evaluation");}std::wcout<<L"frame="<<i<<L" neural="<<st.neuralActive<<L" passes="<<st.neuralPassesExecuted<<L" GPUms="<<st.lastGpuMs<<L" "<<st.message<<L"\n";check(st.neuralActive&&st.neuralPassesExecuted==1,"one active neural pass");if(i==3)pipe->invalidateHistory();if(i==4||i==8)check(st.temporalResetThisFrame==1,"native reentry must reset unknown-token history too");if(i==5)pipe->reset();}
 for(unsigned cycle=0;cycle<stressCycles;++cycle){
  pipe->invalidateHistory();warm.resetHistory=false;
  if(!sharedHost->evaluate(producerContext.Get(),warm,s,st)){std::wcerr<<L"stress D3D11 cycle="<<cycle<<L" "<<st.message<<L"\n";throw std::runtime_error("stress D3D11 handoff");}
  guides.temporal.flags=udlss::Temporal_SourceFrameKnown;guides.temporal.sourceFrame=100+cycle;
  upload(color.Get(),pixels.data(),w*4);
  if(!pipe->process(d.Get(),q.Get(),color.Get(),guides,s,argv[1],dir,st,nullptr,sharedHost)){std::wcerr<<L"stress D3D12 cycle="<<cycle<<L" "<<st.message<<L"\n";throw std::runtime_error("stress D3D12 handoff");}
  check(st.neuralActive&&st.neuralPassesExecuted==1,"stress one-pass output");
  if((cycle+1)%100==0)std::cout<<"stress cycles="<<cycle+1<<std::endl;
 }
 check(wcsstr(st.message,L"NativeCPU pre=")!=nullptr,"native stage timing diagnostics missing");
 double preMs{},hostMs{},postMs{},totalMs{};
 check(swscanf_s(wcsstr(st.message,L"NativeCPU pre="),L"NativeCPU pre=%lf host=%lf post=%lf total=%lf",&preMs,&hostMs,&postMs,&totalMs)==4,"stage timing parse");
 check(preMs>=0&&hostMs>0&&postMs>=0&&totalMs>0&&std::abs(totalMs-preMs-hostMs-postMs)<.01,"stage timings must account for the completed native call");
 auto names=udlss::makeNrHostSessionNames(GetCurrentProcessId(),1);HANDLE mapping=OpenFileMappingW(FILE_MAP_READ,FALSE,names.map.c_str());check(mapping!=nullptr,"host diagnostic mapping");
 auto* shared=static_cast<const udlss::neural::hostipc::Shared*>(MapViewOfFile(mapping,FILE_MAP_READ,0,0,sizeof(udlss::neural::hostipc::Shared)));check(shared!=nullptr,"host diagnostics");
 std::cout<<"feature creations="<<shared->featureCreations<<" reuses="<<shared->featureReuses<<" budgetMiB="<<shared->localBudgetBytes/1048576<<" sharedMiB="<<shared->sharedUsageBytes/1048576<<"\n";
 check(shared->featureCreations==1&&shared->featureReuses==4+stressCycles*2,"resource rebind must retain the compatible neural feature");UnmapViewOfFile(shared);CloseHandle(mapping);
 D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 bytes{};auto rd=color->GetDesc();d->GetCopyableFootprints(&rd,0,1,0,&fp,nullptr,nullptr,&bytes);D3D12_RESOURCE_DESC bd{};bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;bd.Width=bytes;bd.Height=bd.DepthOrArraySize=bd.MipLevels=bd.SampleDesc.Count=1;bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;heap.Type=D3D12_HEAP_TYPE_READBACK;ComPtr<ID3D12Resource>read;check(SUCCEEDED(d->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&read))),"test-only readback");a->Reset();l->Reset(a.Get(),nullptr);auto tr=udlss::gpu::D3D12NativePipeline::barrier(color.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_SOURCE);l->ResourceBarrier(1,&tr);D3D12_TEXTURE_COPY_LOCATION dst{};dst.pResource=read.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=fp;D3D12_TEXTURE_COPY_LOCATION src{};src.pResource=color.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;l->CopyTextureRegion(&dst,0,0,0,&src,nullptr);submit();void* ptr{};read->Map(0,nullptr,&ptr);double mean=0,diff=0;for(UINT y=0;y<h;y++)for(UINT x=0;x<w*4;x++)if(x%4!=3){auto value=((unsigned char*)ptr)[y*fp.Footprint.RowPitch+x];mean+=value;diff+=std::abs(int(value)-int(pixels[y*w*4+x]));}read->Unmap(0,nullptr);mean/=w*h*3;diff/=w*h*3;std::cout<<"mean="<<mean<<" neural difference="<<diff<<"\n";check(mean>10&&mean<245,"visible nonblack output");check(diff>.05,"neural pixels differ from input");
 auto other=s;other.backend=udlss::BackendMode::Passthrough;std::weak_ptr<udlss::neural::Backend> oldHost=sharedHost;
 pipe->releaseObsoleteHost(other,argv[1]);sharedHost.reset();check(oldHost.expired(),"explicit backend change must release native host ownership");
 sharedHost.reset(udlss::neural::createExternalHostNR());check(sharedHost->initialize({producer.Get(),producerContext.Get(),d.Get(),q.Get()},argv[1],s,st),"replacement shared host startup");check(warmHost(),"replacement host warmup");
 // Deliberately stall only this test queue. Destruction must retain every
 // submitted resource and the provider lease until the queue is unblocked.
 a->Reset();l->Reset(a.Get(),nullptr);tr=udlss::gpu::D3D12NativePipeline::barrier(color.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COMMON);l->ResourceBarrier(1,&tr);submit();
 ComPtr<ID3D12Fence> block;d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&block));check(SUCCEEDED(q->Wait(block.Get(),1)),"test queue stall");guides.temporal.sourceFrame=99;
 check(!pipe->process(d.Get(),q.Get(),color.Get(),guides,s,argv[1],dir,st,nullptr,sharedHost),"stalled frame must bypass");
 pipe.reset();check(lease.use_count()>2,"in-flight snapshot lease must survive pipeline destruction");
 // An isolated client can request reconfiguration before its blocked producer
 // retires. The host must preserve live objects and explain the exact wait.
 auto blockedNames=udlss::makeNrHostSessionNames(GetCurrentProcessId(),2);
 HANDLE blockedMap=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,blockedNames.map.c_str());check(blockedMap!=nullptr,"blocked host mapping");
 auto* blockedWire=(udlss::neural::hostipc::Shared*)MapViewOfFile(blockedMap,FILE_MAP_ALL_ACCESS,0,0,sizeof(udlss::neural::hostipc::Shared));check(blockedWire!=nullptr,"blocked host wire");
 HANDLE configureEvent=OpenEventW(EVENT_MODIFY_STATE,FALSE,(blockedNames.map+L".Frame").c_str());check(configureEvent!=nullptr,"blocked host event");
 HANDLE hostProcess=OpenProcess(PROCESS_DUP_HANDLE,FALSE,blockedWire->hostPid);check(hostProcess!=nullptr,"blocked host process");
 ComPtr<ID3D12Fence> unusedFence;check(SUCCEEDED(d->CreateFence(0,D3D12_FENCE_FLAG_SHARED,IID_PPV_ARGS(&unusedFence))),"unused transfer fence");
 std::array<std::uint64_t*,6> incomingHandles={&blockedWire->fenceHandle,&blockedWire->colorHandle,&blockedWire->outputHandle,&blockedWire->motionHandle,&blockedWire->depthHandle,&blockedWire->controlMaskHandle};
 std::array<HANDLE,6> transferred{};
 for(unsigned i=0;i<incomingHandles.size();++i){
  HANDLE local{};ID3D12DeviceChild* resource=i?static_cast<ID3D12DeviceChild*>(depth.Get()):static_cast<ID3D12DeviceChild*>(unusedFence.Get());
  check(SUCCEEDED(d->CreateSharedHandle(resource,nullptr,GENERIC_ALL,nullptr,&local)),"unused shared GPU handle");
  const bool duplicated=DuplicateHandle(GetCurrentProcess(),local,hostProcess,&transferred[i],0,FALSE,DUPLICATE_SAME_ACCESS);CloseHandle(local);check(duplicated,"unused transfer duplication");*incomingHandles[i]=(std::uint64_t)(uintptr_t)transferred[i];
 }
 InterlockedIncrement(&blockedWire->configGeneration);SetEvent(configureEvent);
 for(unsigned i=0;i<800&&blockedWire->state!=(LONG)udlss::neural::hostipc::State::Error;++i)Sleep(10);
 const bool rejected=blockedWire->state==(LONG)udlss::neural::hostipc::State::Error;
 const bool detailed=rejected&&wcsstr(blockedWire->message,L"local=")&&wcsstr(blockedWire->message,L"device=0x");
 bool transfersReleased=rejected;
 for(unsigned i=0;rejected&&i<incomingHandles.size();++i){
  if(*incomingHandles[i]){
   transfersReleased=false;HANDLE leaked{};
   if(DuplicateHandle(hostProcess,(HANDLE)(uintptr_t)*incomingHandles[i],GetCurrentProcess(),&leaked,0,FALSE,DUPLICATE_SAME_ACCESS|DUPLICATE_CLOSE_SOURCE))CloseHandle(leaked);
   *incomingHandles[i]=0;
  }
 }
 const DWORD failedHostPid=blockedWire->hostPid;
 CloseHandle(hostProcess);std::wcout<<L"blocked reconfigure: "<<blockedWire->message<<std::endl;
 CloseHandle(configureEvent);UnmapViewOfFile(blockedWire);CloseHandle(blockedMap);
 block->Signal(1);for(unsigned i=0;i<600&&lease.use_count()>2;i++)Sleep(10);
 check(detailed,"host drain failure must expose fence and device diagnostics");
 check(transfersReleased,"rejected reconfiguration must close all unopened GPU transfers");
 check(lease.use_count()==2,"retired snapshot lease must release after GPU completion");
 wchar_t diagnosticsEnabled[8]{};
 if(GetEnvironmentVariableW(L"UDLSS_NRHOST_DRED",diagnosticsEnabled,_countof(diagnosticsEnabled))==1&&diagnosticsEnabled[0]==L'1'){
  wchar_t appData[32768]{};check(GetEnvironmentVariableW(L"LOCALAPPDATA",appData,_countof(appData))>0,"diagnostic directory");
  const auto report=std::filesystem::path(appData)/L"UniversalDLSS5"/L"ngx-host"/(L"gpu-fault-"+std::to_wstring(failedHostPid)+L".txt");
  std::ifstream input(report);std::string reportText((std::istreambuf_iterator<char>(input)),{});
  check(reportText.find("NRHost GPU diagnostics")!=std::string::npos&&reportText.find("device=0x00000000")!=std::string::npos&&reportText.find("DRED")!=std::string::npos,"opt-in failed-drain GPU diagnostic report missing");
 }
 CloseHandle(event);std::cout<<"PASS: stalled queue retirement retained then released the snapshot lease\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 1;}}
