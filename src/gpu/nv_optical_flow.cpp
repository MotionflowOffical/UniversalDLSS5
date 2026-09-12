#include "nv_optical_flow.hpp"
#include "udlss/nvof_policy.hpp"
#include <algorithm>
#include <array>
#include <vector>
#include <cwchar>

namespace udlss::gpu {
using Microsoft::WRL::ComPtr;

NvOpticalFlowD3D11::~NvOpticalFlowD3D11(){ shutdown(); }

void NvOpticalFlowD3D11::resetHistory(){ havePrevious_=false; writeIndex_=0; previousIndex_=0; }

void NvOpticalFlowD3D11::shutdown(){
#ifdef UDLSS_WITH_NVOF
    unregisterAll();
    if(session_ && api_.nvOFDestroy) api_.nvOFDestroy(session_);
    session_=nullptr;
    if(module_) FreeLibrary(module_);
    module_=nullptr;
    api_={};
#endif
    inputs_[0].Reset(); inputs_[1].Reset(); inputRtvs_[0].Reset(); inputRtvs_[1].Reset();
    vectorTex_.Reset(); costTex_.Reset(); vectorSrv_.Reset(); costSrv_.Reset();
    device_.Reset(); context_.Reset(); width_=height_=grid_=desiredGrid_=0; hasCost_=false; ready_=false; resetHistory();
}

ID3D11RenderTargetView* NvOpticalFlowD3D11::currentInputRtv() const { return ready_ ? inputRtvs_[writeIndex_].Get() : nullptr; }

#ifndef UDLSS_WITH_NVOF
bool NvOpticalFlowD3D11::configure(ID3D11Device*,ID3D11DeviceContext*,std::uint32_t,std::uint32_t,std::uint32_t,LatencyMode,RuntimeStatus& status){
    wcscpy_s(status.flowName,L"HLSL optical flow (NVOFA headers disabled)"); return false;
}
bool NvOpticalFlowD3D11::execute(RuntimeStatus&){ return false; }
#else
namespace {
using PFNCreateInstance = NV_OF_STATUS (NVOFAPI*)(std::uint32_t, NV_OF_D3D11_API_FUNCTION_LIST*);
NV_OF_PERF_LEVEL perfFor(LatencyMode m){
    switch(nvofPerfClass(m)){
    case NvofPerfClass::Fast: return NV_OF_PERF_LEVEL_FAST;
    case NvofPerfClass::Slow: return NV_OF_PERF_LEVEL_SLOW;
    default: return NV_OF_PERF_LEVEL_MEDIUM;
    }
}
}

bool NvOpticalFlowD3D11::loadApi(RuntimeStatus& status){
    if(module_) return true;
#ifdef _WIN64
    const wchar_t* dll=L"nvofapi64.dll";
#else
    const wchar_t* dll=L"nvofapi.dll";
#endif
    module_=LoadLibraryExW(dll,nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!module_) module_=LoadLibraryW(dll);
    if(!module_){ wcscpy_s(status.flowName,L"HLSL optical flow (NVOFA driver unavailable)"); return false; }
    auto create=reinterpret_cast<PFNCreateInstance>(GetProcAddress(module_,"NvOFAPICreateInstanceD3D11"));
    if(!create || create(NV_OF_API_VERSION,&api_)!=NV_OF_SUCCESS){ wcscpy_s(status.flowName,L"HLSL optical flow (NVOFA API 5 unavailable)"); return false; }
    if(!api_.nvCreateOpticalFlowD3D11||!api_.nvOFInit||!api_.nvOFGetCaps||!api_.nvOFGetSurfaceFormatCountD3D11||
       !api_.nvOFGetSurfaceFormatD3D11||!api_.nvOFRegisterResourceD3D11||!api_.nvOFUnregisterResourceD3D11||
       !api_.nvOFExecute||!api_.nvOFDestroy){ wcscpy_s(status.flowName,L"HLSL optical flow (incomplete NVOFA API)"); return false; }
    return true;
}

bool NvOpticalFlowD3D11::supportsFormat(NV_OF_BUFFER_USAGE usage,DXGI_FORMAT format) const {
    std::uint32_t count=0;
    if(api_.nvOFGetSurfaceFormatCountD3D11(session_,usage,NV_OF_MODE_OPTICALFLOW,&count)!=NV_OF_SUCCESS||count==0||count>64) return false;
    std::vector<DXGI_FORMAT> formats(count);
    if(api_.nvOFGetSurfaceFormatD3D11(session_,usage,NV_OF_MODE_OPTICALFLOW,formats.data())!=NV_OF_SUCCESS) return false;
    return std::find(formats.begin(),formats.end(),format)!=formats.end();
}

bool NvOpticalFlowD3D11::queryGrid(std::uint32_t desired,RuntimeStatus& status){
    std::uint32_t count=0;
    if(api_.nvOFGetCaps(session_,NV_OF_CAPS_SUPPORTED_OUTPUT_GRID_SIZES,nullptr,&count)!=NV_OF_SUCCESS||count==0||count>16){wcscpy_s(status.flowName,L"HLSL optical flow (NVOFA grid query failed)");return false;}
    std::vector<std::uint32_t> grids(count);
    if(api_.nvOFGetCaps(session_,NV_OF_CAPS_SUPPORTED_OUTPUT_GRID_SIZES,grids.data(),&count)!=NV_OF_SUCCESS){wcscpy_s(status.flowName,L"HLSL optical flow (NVOFA grid query failed)");return false;}
    grid_=chooseNvofGrid(desired,grids);
    return grid_!=0;
}

bool NvOpticalFlowD3D11::registerResource(ID3D11Texture2D* tex,NvOFGPUBufferHandle& handle,RuntimeStatus& status){
    auto r=api_.nvOFRegisterResourceD3D11(session_,tex,&handle);
    if(r!=NV_OF_SUCCESS){status.lastResult=(std::int32_t)r;wcscpy_s(status.flowName,L"HLSL optical flow (NVOFA resource registration failed)");return false;}
    return true;
}

void NvOpticalFlowD3D11::unregisterAll(){
    if(!api_.nvOFUnregisterResourceD3D11) return;
    for(auto& h:inputHandles_){if(h){api_.nvOFUnregisterResourceD3D11(h);h=nullptr;}}
    if(vectorHandle_){api_.nvOFUnregisterResourceD3D11(vectorHandle_);vectorHandle_=nullptr;}
    if(costHandle_){api_.nvOFUnregisterResourceD3D11(costHandle_);costHandle_=nullptr;}
}

bool NvOpticalFlowD3D11::createResources(RuntimeStatus& status){
    D3D11_TEXTURE2D_DESC in{}; in.Width=width_;in.Height=height_;in.MipLevels=1;in.ArraySize=1;in.Format=DXGI_FORMAT_B8G8R8A8_UNORM;in.SampleDesc.Count=1;in.Usage=D3D11_USAGE_DEFAULT;in.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
    for(int i=0;i<2;i++) if(FAILED(device_->CreateTexture2D(&in,nullptr,&inputs_[i]))||FAILED(device_->CreateRenderTargetView(inputs_[i].Get(),nullptr,&inputRtvs_[i]))){wcscpy_s(status.flowName,L"HLSL optical flow (NVOFA input allocation failed)");return false;}
    const UINT vw=(width_+grid_-1)/grid_, vh=(height_+grid_-1)/grid_;
    D3D11_TEXTURE2D_DESC out{};out.Width=vw;out.Height=vh;out.MipLevels=1;out.ArraySize=1;out.Format=DXGI_FORMAT_R16G16_SINT;out.SampleDesc.Count=1;out.Usage=D3D11_USAGE_DEFAULT;out.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    if(FAILED(device_->CreateTexture2D(&out,nullptr,&vectorTex_))||FAILED(device_->CreateShaderResourceView(vectorTex_.Get(),nullptr,&vectorSrv_))){wcscpy_s(status.flowName,L"HLSL optical flow (NVOFA vector allocation failed)");return false;}
    hasCost_=supportsFormat(NV_OF_BUFFER_USAGE_COST,DXGI_FORMAT_R8_UINT);
    if(hasCost_){out.Format=DXGI_FORMAT_R8_UINT;if(FAILED(device_->CreateTexture2D(&out,nullptr,&costTex_))||FAILED(device_->CreateShaderResourceView(costTex_.Get(),nullptr,&costSrv_)))hasCost_=false;}
    if(!registerResource(inputs_[0].Get(),inputHandles_[0],status)||!registerResource(inputs_[1].Get(),inputHandles_[1],status)||!registerResource(vectorTex_.Get(),vectorHandle_,status))return false;
    if(hasCost_&&!registerResource(costTex_.Get(),costHandle_,status)) return false;
    return true;
}

bool NvOpticalFlowD3D11::configure(ID3D11Device* device,ID3D11DeviceContext* context,std::uint32_t width,std::uint32_t height,std::uint32_t desiredGrid,LatencyMode latency,RuntimeStatus& status){
    if(ready_&&device_.Get()==device&&width_==width&&height_==height&&latency_==latency&&desiredGrid_==desiredGrid) return true;
    if(!ready_&&attempted_&&attemptedDevice_==device&&attemptedWidth_==width&&attemptedHeight_==height&&attemptedGrid_==desiredGrid&&attemptedLatency_==latency) return false;
    shutdown(); attempted_=true;attemptedDevice_=device;attemptedWidth_=width;attemptedHeight_=height;attemptedGrid_=desiredGrid;attemptedLatency_=latency; device_=device;context_=context;width_=width;height_=height;desiredGrid_=desiredGrid;latency_=latency;
    if(!loadApi(status)){shutdown();return false;}
    auto r=api_.nvCreateOpticalFlowD3D11(device,context,&session_);if(r!=NV_OF_SUCCESS||!session_){status.lastResult=(std::int32_t)r;wcscpy_s(status.flowName,L"HLSL optical flow (NVOFA device unsupported)");shutdown();return false;}
    if(!queryGrid(desiredGrid,status)){shutdown();return false;}
    if(!supportsFormat(NV_OF_BUFFER_USAGE_INPUT,DXGI_FORMAT_B8G8R8A8_UNORM)||!supportsFormat(NV_OF_BUFFER_USAGE_OUTPUT,DXGI_FORMAT_R16G16_SINT)){wcscpy_s(status.flowName,L"HLSL optical flow (NVOFA formats unsupported)");shutdown();return false;}
    NV_OF_INIT_PARAMS init{};init.width=width_;init.height=height_;init.outGridSize=(NV_OF_OUTPUT_VECTOR_GRID_SIZE)grid_;init.hintGridSize=NV_OF_HINT_VECTOR_GRID_SIZE_UNDEFINED;init.mode=NV_OF_MODE_OPTICALFLOW;init.perfLevel=perfFor(latency_);init.enableExternalHints=NV_OF_FALSE;init.enableOutputCost=supportsFormat(NV_OF_BUFFER_USAGE_COST,DXGI_FORMAT_R8_UINT)?NV_OF_TRUE:NV_OF_FALSE;init.hPrivData=nullptr;init.disparityRange=NV_OF_STEREO_DISPARITY_RANGE_UNDEFINED;init.enableRoi=NV_OF_FALSE;init.predDirection=NV_OF_PRED_DIRECTION_FORWARD;init.enableGlobalFlow=NV_OF_FALSE;init.inputBufferFormat=NV_OF_BUFFER_FORMAT_ABGR8;
    r=api_.nvOFInit(session_,&init);if(r!=NV_OF_SUCCESS){status.lastResult=(std::int32_t)r;wcscpy_s(status.flowName,L"HLSL optical flow (NVOFA init failed)");shutdown();return false;}
    if(!createResources(status)){shutdown();return false;}
    ready_=true;resetHistory();swprintf_s(status.flowName,L"NVIDIA Optical Flow Accelerator (%ux%u grid)",grid_,grid_);return true;
}

bool NvOpticalFlowD3D11::execute(RuntimeStatus& status){
    if(!ready_) return false;
    const auto cur=writeIndex_;
    if(!havePrevious_){previousIndex_=cur;writeIndex_=1-cur;havePrevious_=true;return false;}
    NV_OF_EXECUTE_INPUT_PARAMS in{};in.inputFrame=inputHandles_[cur];in.referenceFrame=inputHandles_[previousIndex_];in.externalHints=nullptr;in.disableTemporalHints=NV_OF_FALSE;
    NV_OF_EXECUTE_OUTPUT_PARAMS out{};out.outputBuffer=vectorHandle_;out.outputCostBuffer=hasCost_?costHandle_:nullptr;
    const auto r=api_.nvOFExecute(session_,&in,&out);previousIndex_=cur;writeIndex_=1-cur;
    if(r!=NV_OF_SUCCESS){status.lastResult=(std::int32_t)r;wcscpy_s(status.flowName,L"HLSL optical flow (NVOFA execute failed)");return false;}
    swprintf_s(status.flowName,L"NVIDIA Optical Flow Accelerator (%ux%u grid)",grid_,grid_);return true;
}
#endif
} // namespace udlss::gpu
