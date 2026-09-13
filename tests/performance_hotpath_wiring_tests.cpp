#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#ifndef UDLSS_SOURCE_DIR
#error UDLSS_SOURCE_DIR must be defined
#endif
static std::string read(const std::filesystem::path&p){std::ifstream f(p,std::ios::binary);std::ostringstream s;s<<f.rdbuf();return s.str();}
static bool need(const std::string&s,const char*t,const char*m){if(s.find(t)!=std::string::npos)return true;std::cerr<<m<<" missing: "<<t<<"\n";return false;}
static bool forbid(const std::string&s,const char*t,const char*m){if(s.find(t)==std::string::npos)return true;std::cerr<<m<<" found: "<<t<<"\n";return false;}
int main(){
 const std::filesystem::path r=UDLSS_SOURCE_DIR;
 const auto pipe=read(r/"src/gpu/d3d11_pipeline.cpp"), pipeH=read(r/"src/gpu/d3d11_pipeline.hpp"), guide=read(r/"src/gpu/d3d11_guide_extractor.cpp"), guideH=read(r/"src/gpu/d3d11_guide_extractor.hpp"), on12=read(r/"src/gpu/d3d12_on12.cpp"), tracker12=read(r/"src/gpu/d3d12_resource_tracker.cpp"), tracker12H=read(r/"src/gpu/d3d12_resource_tracker.hpp"), hooks=read(r/"src/bridge/dxgi_hooks.cpp"), ngx=read(r/"src/neural/ngx_nr.cpp"), readbackAudit=read(r/"tools/audit_no_readback.py");
 bool ok=true;
 ok&=need(guideH,"adapterProbeAttempted_","game-guide adapter negative lookup is not cached");
 ok&=need(guide,"adapterProbeAttempted_=true","adapter probe is not marked before filesystem work");
 ok&=need(pipeH,"directSourceSrvs_","D3D11 direct-source SRV cache is missing");
 ok&=need(pipe,"tryDirectSourceSrv","shader-readable inputs do not bypass the redundant source copy");
 ok&=need(pipe,"allowDirectD3D11SurfaceReuse(nativeD12_!=nullptr)","native D3D11 swapchain frames are not protected from direct backbuffer view reuse");
 ok&=need(pipeH,"blitRtvs_","backbuffer RTV cache is missing");
 ok&=need(pipe,"shouldMaintainHlslFlowHistory","HLSL low-resolution guide work is not policy-gated");
 ok&=need(on12,"D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE","D3D12On12 staging color is not shader-readable through D3D11");
 ok&=forbid(on12,"std::vector<ID3D11Resource*> acquired","D3D12On12 still heap-allocates the acquired-resource list every frame");
 ok&=need(ngx,"completionEvent_","NGX pacing fence event is not persistent");
 ok&=need(ngx,"D3D12_QUERY_HEAP_TYPE_TIMESTAMP","direct Feature-18 path lacks GPU timestamp queries");
 ok&=need(ngx,"GetTimestampFrequency","neural GPU profiler does not calibrate the D3D12 timestamp clock");
 ok&=need(ngx,"ResolveQueryData","neural GPU profiler does not resolve timestamps asynchronously");
 ok&=need(ngx,"needsNeuralRefinementScratch","multipass scratch allocation is not policy-gated");
 ok&=need(ngx,"needsNeuralControlMask","control-mask slot allocation is not policy-gated");
 ok&=need(ngx,"needsAsyncNeuralCache","completed-output cache allocation is not pacing-aware");
 ok&=need(ngx,"if(useControlMask)","unused control-mask barriers are still emitted");
 ok&=need(tracker12H,"setD3D12QueueTouchCaptureEnabled","D3D12 late-attach queue touch capture cannot be disabled after queue proof");
 ok&=need(tracker12,"queueTouchCaptureEnabled","D3D12 ResourceBarrier hot path lacks a cheap queue-touch gate");
 ok&=need(hooks,"refreshQueueTouchCapture","bridge does not disable queue-touch discovery after presentation queues are trusted");
 ok&=need(ngx,"UDLSS_TIMESTAMP_QUERY_READBACK","timestamp query readback is not explicitly marked as metadata-only");
 ok&=need(readbackAudit,"UDLSS_TIMESTAMP_QUERY_READBACK","release readback audit has no narrow timestamp-query exception");
 ok&=forbid(pipe,"st.lastGpuMs=(float)std::chrono","CPU wall-clock time is still mislabeled as GPU time");
 return ok?0:1;
}
