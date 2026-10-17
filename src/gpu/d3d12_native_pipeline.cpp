#include "d3d12_native_pipeline.hpp"
#include "udlss/guide_candidate_policy.hpp"
#include "udlss/motion_route_policy.hpp"
#include <d3dcompiler.h>
#include <cstring>
#include <algorithm>
#include <utility>
#include <chrono>
#include "udlss/gpu_fence_wait.hpp"
using Microsoft::WRL::ComPtr;
namespace udlss::gpu {
struct D3D12NativePipeline::Retirement {
 std::array<ComPtr<IUnknown>,32> objects{};unsigned count{};
 ComPtr<ID3D12Fence> fence;std::shared_ptr<neural::Backend> backend;
 CapturedGameGuides guides;HANDLE event{},completionEvent{CreateEventW(nullptr,FALSE,FALSE,nullptr)};UINT64 target{};HMODULE module{};PTP_WAIT wait{};
 ~Retirement(){if(completionEvent)CloseHandle(completionEvent);}
 static void CALLBACK completed(PTP_CALLBACK_INSTANCE instance,void*context,PTP_WAIT,TP_WAIT_RESULT){
  auto* self=static_cast<Retirement*>(context);auto module=self->module;
  const auto completedValue=self->fence->GetCompletedValue();
  if(completedValue!=UINT64_MAX&&completedValue<self->target){
   if(SUCCEEDED(self->fence->SetEventOnCompletion(self->target,self->completionEvent)))SetThreadpoolWait(self->wait,self->completionEvent,nullptr);
   return;
  }
  CloseThreadpoolWait(self->wait);CloseHandle(self->event);delete self;
  if(module)FreeLibraryWhenCallbackReturns(instance,module);
 }
};
D3D12NativePipeline::D3D12NativePipeline():retirement_(std::make_unique<Retirement>()){}
namespace {
struct Params {
 UINT width,height,downsample,radius;float exposure,motionScale,confidence,text;
 float ui,edge,sharpness,reactive,control,historyClamp,disocclusion,temporal;
 UINT invertY,hasHistory,pad0,pad1;float deadzone,scaleX,scaleY,split;
 UINT view,depthMode,srgb,useMask,encoding,hdr;float paper,maxNits;
};
static_assert(sizeof(Params)==128);
DXGI_FORMAT depthView(DXGI_FORMAT f){switch(f){
 case DXGI_FORMAT_D32_FLOAT:case DXGI_FORMAT_R32_TYPELESS:return DXGI_FORMAT_R32_FLOAT;
 case DXGI_FORMAT_D16_UNORM:case DXGI_FORMAT_R16_TYPELESS:return DXGI_FORMAT_R16_UNORM;
 case DXGI_FORMAT_D24_UNORM_S8_UINT:case DXGI_FORMAT_R24G8_TYPELESS:return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
 case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:case DXGI_FORMAT_R32G8X24_TYPELESS:return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
 default:return f;}}
bool shader(const std::filesystem::path&path,const char*entry,const char*target,ComPtr<ID3DBlob>&b,RuntimeStatus&st){
 ComPtr<ID3DBlob>e;auto hr=D3DCompileFromFile(path.c_str(),nullptr,D3D_COMPILE_STANDARD_FILE_INCLUDE,entry,target,D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&b,&e);
 if(FAILED(hr)){std::string s=e?std::string((char*)e->GetBufferPointer(),e->GetBufferSize()):"D3D12 shader compilation failed";std::wstring w(s.begin(),s.end());wcsncpy_s(st.message,w.c_str(),_TRUNCATE);}return SUCCEEDED(hr);
}
}
D3D12_RESOURCE_BARRIER D3D12NativePipeline::barrier(ID3D12Resource*r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b){D3D12_RESOURCE_BARRIER x{};x.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;x.Transition.pResource=r;x.Transition.StateBefore=a;x.Transition.StateAfter=b;x.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;return x;}
bool D3D12NativePipeline::eligible(const D3D12_RESOURCE_DESC&d,const CapturedGameGuides&g,const Settings&s)const{
 const bool color=d.Format==DXGI_FORMAT_R8G8B8A8_UNORM||d.Format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB||d.Format==DXGI_FORMAT_B8G8R8A8_UNORM||d.Format==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB||d.Format==DXGI_FORMAT_R10G10B10A2_UNORM||d.Format==DXGI_FORMAT_R16G16B16A16_FLOAT;
 return !failed_&&color&&d.SampleDesc.Count==1&&s.backend==BackendMode::ExternalHostNR&&s.framePacing==FramePacingMode::Synchronized&&s.motionSource==MotionSource::Auto&&s.useGameDepth&&s.depthMode!=DepthGuideMode::SyntheticFar&&
  g.snapshotOwned&&g.depthSnapshotOwned&&g.motionSnapshotOwned&&g.depth12&&g.motion12&&g.motionConventionKnown&&!g.temporal.cropX&&!g.temporal.cropY&&
  providerGuideExtentCompatible(g.depthWidth,g.depthHeight,(UINT)d.Width,d.Height)&&providerGuideExtentCompatible(g.motionWidth,g.motionHeight,(UINT)d.Width,d.Height);
}
bool D3D12NativePipeline::initialize(ID3D12Device*d,ID3D12CommandQueue*q,DXGI_FORMAT format,const std::wstring&dir,RuntimeStatus&st){
 if(device_&&device_.Get()==d&&queue_.Get()==q&&format_==format)return true;
 if(device_){if(!finish())return false;reset();backend_.reset();}
 device_=d;queue_=q;format_=format;root_.Reset();descriptors_.Reset();rtv_.Reset();blit_.Reset();for(auto&p:compute_)p.Reset();allocator_.Reset();list_.Reset();fence_.Reset();value_=0;
 if(event_)CloseHandle(event_);event_=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!event_)return false;
 D3D12_DESCRIPTOR_RANGE range{};range.RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV;range.NumDescriptors=5;range.OffsetInDescriptorsFromTableStart=0;
 D3D12_DESCRIPTOR_RANGE ur{};ur.RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_UAV;ur.NumDescriptors=1;ur.OffsetInDescriptorsFromTableStart=5;
 D3D12_DESCRIPTOR_RANGE ranges[]={range,ur};D3D12_ROOT_PARAMETER rp[2]{};rp[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;rp[0].DescriptorTable.NumDescriptorRanges=2;rp[0].DescriptorTable.pDescriptorRanges=ranges;rp[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;rp[1].Constants.Num32BitValues=32;
 D3D12_STATIC_SAMPLER_DESC sampler{};sampler.Filter=D3D12_FILTER_MIN_MAG_MIP_LINEAR;sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;sampler.MaxLOD=D3D12_FLOAT32_MAX;sampler.ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
 D3D12_ROOT_SIGNATURE_DESC rd{};rd.NumParameters=2;rd.pParameters=rp;rd.NumStaticSamplers=1;rd.pStaticSamplers=&sampler;rd.Flags=D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;ComPtr<ID3DBlob>signature,error;
 if(FAILED(D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&signature,&error))||FAILED(d->CreateRootSignature(0,signature->GetBufferPointer(),signature->GetBufferSize(),IID_PPV_ARGS(&root_))))return false;
 D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.NumDescriptors=128;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;if(FAILED(d->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&descriptors_))))return false;stride_=d->GetDescriptorHandleIncrementSize(hd.Type);hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;hd.NumDescriptors=1;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_NONE;if(FAILED(d->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&rtv_))))return false;
 const wchar_t* files[]={L"convert.hlsl",L"depth_convert.hlsl",L"native_motion_convert.hlsl",L"mask.hlsl",L"post.hlsl",L"convert.hlsl"};
 for(unsigned i=0;i<6;i++){ComPtr<ID3DBlob>b;if(!shader(std::filesystem::path(dir)/L"shaders"/files[i],"CSMain","cs_5_0",b,st))return false;D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root_.Get();pd.CS={b->GetBufferPointer(),b->GetBufferSize()};if(FAILED(d->CreateComputePipelineState(&pd,IID_PPV_ARGS(&compute_[i]))))return false;}
 ComPtr<ID3DBlob>vs,ps;if(!shader(std::filesystem::path(dir)/L"shaders/blit.hlsl","VSMain","vs_5_0",vs,st)||!shader(std::filesystem::path(dir)/L"shaders/blit.hlsl","PSMain","ps_5_0",ps,st))return false;
 D3D12_GRAPHICS_PIPELINE_STATE_DESC gp{};gp.pRootSignature=root_.Get();gp.VS={vs->GetBufferPointer(),vs->GetBufferSize()};gp.PS={ps->GetBufferPointer(),ps->GetBufferSize()};gp.SampleMask=UINT_MAX;gp.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;gp.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;gp.RasterizerState.DepthClipEnable=TRUE;gp.BlendState.RenderTarget[0].RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;gp.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;gp.NumRenderTargets=1;gp.RTVFormats[0]=format;gp.SampleDesc.Count=1;
 if(FAILED(d->CreateGraphicsPipelineState(&gp,IID_PPV_ARGS(&blit_)))||FAILED(d->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator_)))||FAILED(d->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator_.Get(),nullptr,IID_PPV_ARGS(&list_)))||FAILED(list_->Close())||FAILED(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence_))))return false;
 return true;
}
bool D3D12NativePipeline::make(Texture&t,UINT w,UINT h,DXGI_FORMAT f,bool shared){
 t={};D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=w;d.Height=h;d.DepthOrArraySize=d.MipLevels=d.SampleDesc.Count=1;d.Format=f;d.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
 if(FAILED(device_->CreateCommittedResource(&hp,shared?D3D12_HEAP_FLAG_SHARED:D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&t.r))))return false;t.format=f;return true;
}
bool D3D12NativePipeline::resources(const D3D12_RESOURCE_DESC&d){
 if(width_==d.Width&&height_==d.Height&&source_.r)return true;
 if(!finish())return false;source_={};current_={};history_={};input_={};output_={};motion_={};depth_={};mask_={};post_={};historyValid_=false;
 D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;auto sd=d;sd.Flags=D3D12_RESOURCE_FLAG_NONE;
 if(FAILED(device_->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&sd,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&source_.r))))return false;source_.format=d.Format;
 const UINT w=(UINT)d.Width,h=d.Height;
 if(!make(current_,w,h,DXGI_FORMAT_R16G16B16A16_FLOAT)||!make(history_,w,h,DXGI_FORMAT_R16G16B16A16_FLOAT)||!make(input_,w,h,DXGI_FORMAT_R8G8B8A8_UNORM,true)||!make(output_,w,h,DXGI_FORMAT_R8G8B8A8_UNORM,true)||!make(motion_,w,h,DXGI_FORMAT_R16G16_FLOAT,true)||!make(depth_,w,h,DXGI_FORMAT_R32_FLOAT,true)||!make(mask_,w,h,DXGI_FORMAT_R8G8B8A8_UNORM)||!make(post_,w,h,DXGI_FORMAT_R16G16B16A16_FLOAT))return false;
 width_=w;height_=h;return true;
}
void D3D12NativePipeline::state(Texture&t,D3D12_RESOURCE_STATES target){if(t.state==target)return;auto b=barrier(t.r.Get(),t.state,target);list_->ResourceBarrier(1,&b);t.state=target;}
bool D3D12NativePipeline::finish(){return !fence_||!value_||waitForGpuFenceValue(fence_.Get(),event_,value_,2000);}
bool D3D12NativePipeline::begin(){if(!finish())return false;cursor_=0;return SUCCEEDED(allocator_->Reset())&&SUCCEEDED(list_->Reset(allocator_.Get(),nullptr));}
bool D3D12NativePipeline::submit(){if(FAILED(list_->Close())){failed_=true;return false;}ID3D12CommandList* lists[]={list_.Get()};queue_->ExecuteCommandLists(1,lists);return SUCCEEDED(queue_->Signal(fence_.Get(),++value_));}
void D3D12NativePipeline::bind(std::initializer_list<Texture*> in,Texture*out,bool graphics){
 auto cpu=descriptors_->GetCPUDescriptorHandleForHeapStart();cpu.ptr+=cursor_*stride_;auto gpu=descriptors_->GetGPUDescriptorHandleForHeapStart();gpu.ptr+=cursor_*stride_;auto it=in.begin();
 for(unsigned i=0;i<5;i++){Texture*t=it!=in.end()?*it++:nullptr;D3D12_SHADER_RESOURCE_VIEW_DESC sd{};sd.Format=t?t->format:DXGI_FORMAT_R8G8B8A8_UNORM;sd.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;sd.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;sd.Texture2D.MipLevels=1;device_->CreateShaderResourceView(t?t->r.Get():nullptr,&sd,cpu);cpu.ptr+=stride_;}
 D3D12_UNORDERED_ACCESS_VIEW_DESC ud{};ud.Format=out?out->format:DXGI_FORMAT_R8G8B8A8_UNORM;ud.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;device_->CreateUnorderedAccessView(out?out->r.Get():nullptr,nullptr,&ud,cpu);cursor_+=6;
 ID3D12DescriptorHeap* heaps[]={descriptors_.Get()};list_->SetDescriptorHeaps(1,heaps);if(graphics){list_->SetGraphicsRootSignature(root_.Get());list_->SetGraphicsRootDescriptorTable(0,gpu);}else{list_->SetComputeRootSignature(root_.Get());list_->SetComputeRootDescriptorTable(0,gpu);}
}
void D3D12NativePipeline::dispatch(unsigned shader,std::initializer_list<Texture*> in,Texture&out,const void*params,UINT bytes){
 for(auto*t:in)if(t)state(*t,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);state(out,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);bind(in,&out);list_->SetPipelineState(compute_[shader].Get());list_->SetComputeRoot32BitConstants(1,bytes/4,params,0);list_->Dispatch((width_+7)/8,(height_+7)/8,1);
}
bool D3D12NativePipeline::process(ID3D12Device*d,ID3D12CommandQueue*q,ID3D12Resource*bb,const CapturedGameGuides&g,const Settings&s,const std::wstring&runtime,const std::wstring&dir,RuntimeStatus&st,const SwapchainColorContext*color,const std::shared_ptr<neural::Backend>& sharedHost){
 const auto profileStart=std::chrono::steady_clock::now();
 struct Attempt {bool& history;bool success{};~Attempt(){if(!success)history=false;}} attempt{historyValid_};
 st.neuralActive=0;st.neuralPassesExecuted=0;st.recoveryPostCopyCommitted=0;
 if(!bb||!eligible(bb->GetDesc(),g,s))return false;
 if(!initialize(d,q,bb->GetDesc().Format,dir,st)){failed_=true;return false;}
 if(sharedHost&&backend_!=sharedHost){
  if(!finish())return false;backend_=sharedHost;runtime_=runtime;unsupportedCached_=s.attemptUnsupportedHardware;historyValid_=false;
 }
 // A missed neural deadline leaves these exact shared textures in use. Do not
 // overwrite them on the next Present, even if the event has been signalled.
 if(backend_&&!backend_->canWriteInputs()){st.neuralActive=0;return false;}
 if(!resources(bb->GetDesc()))return false;
 if(!backend_||runtime_!=runtime||unsupportedCached_!=s.attemptUnsupportedHardware){
  backend_.reset(neural::createExternalHostNR());runtime_=runtime;unsupportedCached_=s.attemptUnsupportedHardware;
  if(!backend_->initialize({nullptr,nullptr,d,q},runtime,s,st)){backend_.reset();failed_=true;return false;}
 }
 if(!begin())return false;guideHold_={};guideHold_=g;
 Texture back;back.r=bb;back.format=bb->GetDesc().Format;state(back,D3D12_RESOURCE_STATE_COPY_SOURCE);state(source_,D3D12_RESOURCE_STATE_COPY_DEST);list_->CopyResource(source_.r.Get(),bb);state(back,D3D12_RESOURCE_STATE_COMMON);
 auto temporal=g.temporal;temporal.outputWidth=width_;temporal.outputHeight=height_;
 const bool reset=!historyValid_||g.cameraCut||temporalDiscontinuity(previousTemporal_,temporal);
 const bool sourceHistory=historyValid_&&!g.cameraCut&&canReusePresentedColorHistory(previousTemporal_,temporal);
 Params p{width_,height_,std::max(width_,height_),1,s.exposure,s.motionScale,s.flowConfidenceThreshold,s.protectUI?s.textProtection:0,s.protectUI?s.uiProtection:0,s.edgeThreshold,s.sharpness,s.reactiveStrength,s.controlMaskStrength,s.historyClamp,s.disocclusionThreshold,s.temporalStrength,s.invertMotionY?1u:0u,sourceHistory?1u:0u,0,0,s.staticMotionDeadzone,s.motionScaleX,s.motionScaleY,s.debugSplit,(UINT)s.debugView,(UINT)s.depthMode,0,0,0,0,203,1000};
 if(color){p.encoding=(UINT)color->decision.encoding;p.hdr=color->decision.hdrActive&&color->decision.supported;p.srgb=color->decision.encoding==ColorEncoding::SdrSrgb;p.paper=color->paperWhiteNits;p.maxNits=color->maxNits;}
 else p.srgb=back.format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB||back.format==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
 dispatch(0,{&source_},current_,&p,sizeof(p));p.exposure=1;p.pad0=1;dispatch(0,{&current_},input_,&p,sizeof(p));p.pad0=0;
 Texture gd;gd.r=g.depth12;gd.format=depthView(g.depthFormat);gd.state=g.depthState;dispatch(1,{&gd},depth_,&p,sizeof(p));state(gd,g.depthState);
 const auto jitter=canonicalJitterDelta(previousTemporal_,temporal,width_,height_);
 Texture gm;gm.r=g.motion12;gm.format=g.motionFormat;gm.state=g.motionState;struct {UINT w,h,encoding,invert;float scale,x,y,pad,jitterX,jitterY,reserved[2];} mp{width_,height_,1,s.invertMotionY?1u:0u,s.motionScale,g.motionToPixelScaleX,g.motionToPixelScaleY,0,jitter[0],jitter[1],{0,0}};dispatch(2,{&gm},motion_,&mp,sizeof(mp));state(gm,g.motionState);
 p.pad1=2;dispatch(3,{&current_,&history_,nullptr,&motion_},mask_,&p,sizeof(p));
 state(input_,D3D12_RESOURCE_STATE_COMMON);state(output_,D3D12_RESOURCE_STATE_COMMON);state(motion_,D3D12_RESOURCE_STATE_COMMON);state(depth_,D3D12_RESOURCE_STATE_COMMON);
 if(!submit())return false;
 neural::FrameResources fr{};fr.input12=input_.r.Get();fr.output12=output_.r.Get();fr.motion12=motion_.r.Get();fr.depth12=depth_.r.Get();fr.width=width_;fr.height=height_;fr.inputFormat=DXGI_FORMAT_R8G8B8A8_UNORM;fr.depthInverted=s.depthMode==DepthGuideMode::ForceNormal?false:(s.depthMode==DepthGuideMode::ForceInverted?true:g.depthInverted);fr.resetHistory=reset;fr.temporal=temporal;fr.temporal.flags&=~Temporal_MotionJittered;fr.motionScaleX=s.motionScaleX;fr.motionScaleY=s.motionScaleY;
 st.neuralLocation=NeuralExecutionLocation::ExternalHost;st.neuralBackendKind=NeuralBackendKind::ExternalHost;
 const auto hostStart=std::chrono::steady_clock::now();
 bool evaluated=backend_->evaluate(nullptr,fr,s,st);const auto hostEnd=std::chrono::steady_clock::now();st.neuralActive=evaluated;st.gameDepthActive=1;st.temporalResetThisFrame=reset;st.temporalHistoryValid=!reset;st.depthInverted=fr.depthInverted;st.controlMaskActive=0;
 if(!evaluated){historyValid_=false;finish();return false;}
 if(!begin())return false;guideHold_={};
 p.pad1=postCompositeMotionFlags(MotionRoute::AdapterNative);
 dispatch(4,{&output_,&current_,&mask_,&motion_,&depth_},post_,&p,sizeof(p));
 state(post_,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);state(back,D3D12_RESOURCE_STATE_RENDER_TARGET);device_->CreateRenderTargetView(bb,nullptr,rtv_->GetCPUDescriptorHandleForHeapStart());auto rt=rtv_->GetCPUDescriptorHandleForHeapStart();list_->OMSetRenderTargets(1,&rt,FALSE,nullptr);D3D12_VIEWPORT vp{0,0,(float)width_,(float)height_,0,1};D3D12_RECT rect{0,0,(LONG)width_,(LONG)height_};list_->RSSetViewports(1,&vp);list_->RSSetScissorRects(1,&rect);bind({&post_},nullptr,true);list_->SetPipelineState(blit_.Get());list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);list_->DrawInstanced(3,1,0,0);state(back,D3D12_RESOURCE_STATE_COMMON);
 state(current_,D3D12_RESOURCE_STATE_COPY_SOURCE);state(history_,D3D12_RESOURCE_STATE_COPY_DEST);list_->CopyResource(history_.r.Get(),current_.r.Get());state(output_,D3D12_RESOURCE_STATE_COMMON);state(motion_,D3D12_RESOURCE_STATE_COMMON);state(depth_,D3D12_RESOURCE_STATE_COMMON);
 if(!submit()||!finish())return false;previousTemporal_=temporal;historyValid_=true;attempt.success=true;
 const auto profileEnd=std::chrono::steady_clock::now();
 const auto ms=[](auto from,auto to){return std::chrono::duration<double,std::milli>(to-from).count();};
 wchar_t timing[160]{};swprintf_s(timing,L" | NativeCPU pre=%.3f host=%.3f post=%.3f total=%.3f ms",ms(profileStart,hostStart),ms(hostStart,hostEnd),ms(hostEnd,profileEnd),ms(profileStart,profileEnd));
 wcsncat_s(st.message,timing,_TRUNCATE);
 st.guideDepthWidth=g.depthWidth;st.guideDepthHeight=g.depthHeight;st.guideMotionWidth=g.motionWidth;st.guideMotionHeight=g.motionHeight;st.guideDepthConfidence=st.guideMotionConfidence=g.confidence;
 wcscpy_s(st.interopName,L"Native D3D12 shaders + direct shared D3D12 NRHost");wcscpy_s(st.flowName,L"Game-native motion (D3D12)");wcsncpy_s(st.guideAdapterName,g.provider.c_str(),_TRUNCATE);wcscpy_s(st.guideFields,L"Owned depth + motion; direct D3D12 transport");st.recoveryPostCopyCommitted=1;markPipelineStage(st.stageMask,PipelineStage::OutputComposited);return true;
}
void D3D12NativePipeline::reset(){if(!finish())return;guideHold_={};historyValid_=false;width_=height_=0;source_={};current_={};history_={};input_={};output_={};motion_={};depth_={};mask_={};post_={};if(backend_)backend_->reset();}
void D3D12NativePipeline::releaseObsoleteHost(const Settings&s,const std::wstring& runtime){
 if(!backend_||(s.backend==BackendMode::ExternalHostNR&&runtime_==runtime&&unsupportedCached_==s.attemptUnsupportedHardware))return;
 if(!finish())return;
 reset();backend_.reset();runtime_.clear();
}
void D3D12NativePipeline::retryNeural(){if(!finish())return;reset();backend_.reset();runtime_.clear();failed_=false;}
void D3D12NativePipeline::retireInFlight(){
 // Preallocate this bundle during construction: timeout handling cannot depend
 // on another large allocation. The callback owns both GPU objects and the
 // snapshot lease until the last submitted command really completes.
 auto* retired=retirement_.release();
 auto hold=[&](auto& object){if(object){object.As(&retired->objects[retired->count++]);object.Reset();}};
 hold(device_);hold(queue_);hold(allocator_);hold(list_);hold(root_);hold(descriptors_);hold(rtv_);hold(blit_);for(auto&p:compute_)hold(p);
 for(auto*t:{&source_,&current_,&history_,&input_,&output_,&motion_,&depth_,&mask_,&post_})hold(t->r);
 retired->fence=std::move(fence_);retired->target=value_;retired->backend=std::move(backend_);retired->guides=std::move(guideHold_);retired->event=std::exchange(event_,nullptr);
 GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(&Retirement::completed),&retired->module);
 retired->wait=CreateThreadpoolWait(&Retirement::completed,retired,nullptr);
 if(retired->wait&&retired->module&&retired->completionEvent&&SUCCEEDED(retired->fence->SetEventOnCompletion(retired->target,retired->completionEvent)))SetThreadpoolWait(retired->wait,retired->completionEvent,nullptr);
 // If Windows cannot register retirement, keep the bundle for process lifetime.
 // Releasing live GPU references would be unsafe; this is a failure-only leak.
}
D3D12NativePipeline::~D3D12NativePipeline(){
 if(!finish()&&device_&&SUCCEEDED(device_->GetDeviceRemovedReason()))retireInFlight();
 backend_.reset();if(event_)CloseHandle(event_);
}
}
