#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <shellapi.h>
#include <wrl/client.h>
#include <nvsdk_ngx.h>
#include <nvsdk_ngx_params.h>
#include <MinHook.h>
#include <filesystem>
#include <string>
#include <array>
#include <cstdint>
#include <cstdio>
#include "../neural/external_host_protocol.hpp"
#include "udlss/external_host_policy.hpp"
#include "udlss/runtime_diagnostics.hpp"

using Microsoft::WRL::ComPtr;
using namespace udlss;
using namespace udlss::neural::hostipc;

namespace {
constexpr const char* kHostProjectId="a4df2ee7-cd2a-47ba-9c83-1d7f7c0a5b51";
constexpr const char* kHostEngineVersion="0.3.1";
constexpr NVSDK_NGX_Feature kFeature=NVSDK_NGX_Feature_Reserved18;
constexpr unsigned long long kFallbackSnippetApplicationId=0x0876232Cull;
constexpr const char* kWidth="DLSSNR.Width"; constexpr const char* kHeight="DLSSNR.Height";
constexpr const char* kInputWidth="DLSSNR.InputWidth"; constexpr const char* kInputHeight="DLSSNR.InputHeight";
constexpr const char* kOutputWidth="DLSSNR.OutputWidth"; constexpr const char* kOutputHeight="DLSSNR.OutputHeight";
constexpr const char* kOutputWidth2="DLSSNR.Output.Width"; constexpr const char* kOutputHeight2="DLSSNR.Output.Height";
constexpr const char* kUpscaling="DLSSNR.Upscaling"; constexpr const char* kScale="DLSSNR.Scale"; constexpr const char* kScalingRatio="DLSSNR.ScalingRatio";
constexpr const char* kScalingRatioCallback="DLSSNRComputeScalingRatioCallback"; constexpr const char* kPreset="DLSSNR.Hint.Render.Preset";
constexpr const char* kColor="DLSSNR.Color"; constexpr const char* kOutput="DLSSNR.Output"; constexpr const char* kMVec="DLSSNR.MVec"; constexpr const char* kDepth="DLSSNR.Depth"; constexpr const char* kControlMask="DLSSNR.ControlMask";
constexpr const char* kMVecScaleX="DLSSNR.MVecScaleX"; constexpr const char* kMVecScaleY="DLSSNR.MVecScaleY";
constexpr const char* kDepthInverted="DLSSNR.DepthInverted"; constexpr const char* kEnabled="DLSSNR.Enabled"; constexpr const char* kReset="DLSSNR.Reset";
constexpr const char* kStyle="DLSSNR.Style"; constexpr const char* kIntensity="DLSSNR.Intensity"; constexpr const char* kLocalTone="DLSSNR.LocalToneStrength";
constexpr const char* kLocalStructure="DLSSNR.LocalStructureStrength"; constexpr const char* kSkinStructure="DLSSNR.SkinStructureStrength"; constexpr const char* kUseAutoMask="DLSSNR.UseAutoMask"; constexpr const char* kUiCorrection="DLSSNR.UICorrection";
constexpr const char* kColorSubrectW="DLSSNR.ColorSubrectWidth"; constexpr const char* kColorSubrectH="DLSSNR.ColorSubrectHeight";
constexpr const char* kOutputSubrectW="DLSSNR.OutputSubrectWidth"; constexpr const char* kOutputSubrectH="DLSSNR.OutputSubrectHeight";
constexpr const char* kMVecSubrectW="DLSSNR.MVecSubrectWidth"; constexpr const char* kMVecSubrectH="DLSSNR.MVecSubrectHeight";
constexpr const char* kDepthSubrectW="DLSSNR.DepthSubrectWidth"; constexpr const char* kDepthSubrectH="DLSSNR.DepthSubrectHeight"; constexpr const char* kControlSubrectW="DLSSNR.ControlMaskSubrectWidth"; constexpr const char* kControlSubrectH="DLSSNR.ControlMaskSubrectHeight"; constexpr const char* kPaperWhite="PaperWhiteScale"; constexpr const char* kTransferStrength="TransferStrength"; constexpr const char* kColorStrength="ColorStrength";

using SnippetInitExtFn=NVSDK_NGX_Result (NVSDK_CONV *)(unsigned long long,const wchar_t*,ID3D12Device*,NVSDK_NGX_Version,const NVSDK_NGX_Parameter*);
using SnippetPopulateFn=NVSDK_NGX_Result (NVSDK_CONV *)(NVSDK_NGX_Parameter*);
using SnippetCreateFn=NVSDK_NGX_Result (NVSDK_CONV *)(ID3D12GraphicsCommandList*,NVSDK_NGX_Feature,NVSDK_NGX_Parameter*,NVSDK_NGX_Handle**);
using SnippetEvaluateFn=NVSDK_NGX_Result (NVSDK_CONV *)(ID3D12GraphicsCommandList*,const NVSDK_NGX_Handle*,const NVSDK_NGX_Parameter*,PFN_NVSDK_NGX_ProgressCallback);
using SnippetReleaseFn=NVSDK_NGX_Result (NVSDK_CONV *)(NVSDK_NGX_Handle*);
using SnippetShutdownFn=NVSDK_NGX_Result (NVSDK_CONV *)(ID3D12Device*);
using SnippetGetApplicationIdFn=std::uint32_t (NVSDK_CONV *)();
using SnippetGetVersionFn=std::uint32_t (NVSDK_CONV *)();
using ForwarderLoadFn=int (WINAPI *)(const wchar_t*);
using ForwarderPathFn=void (WINAPI *)(wchar_t*,unsigned int);
using ForwarderGetU32Fn=std::uint32_t (WINAPI *)();

constexpr unsigned NVAPI_ID_INITIALIZE=0x0150E828u;
constexpr unsigned NVAPI_ID_ENUM_GPUS=0xE5AC921Fu;
constexpr unsigned NVAPI_ID_GET_ARCH=0xD8265D24u;
constexpr unsigned NV_ARCH_TURING=0x160u;
constexpr unsigned NV_ARCH_AMPERE=0x170u;
constexpr unsigned NV_ARCH_ADA=0x190u;
constexpr unsigned NV_ARCH_BLACKWELL_COMPAT=0x1B0u;
struct NvArchInfo { unsigned version{},architecture{},implementation{},revision{}; };
using NvApiQueryInterfaceFn=void* (__cdecl *)(unsigned);
using NvApiInitializeFn=int (__cdecl *)();
using NvApiEnumPhysicalGpusFn=int (__cdecl *)(void**,int*);
using NvApiGetArchInfoFn=int (__cdecl *)(void*,NvArchInfo*);

NvApiGetArchInfoFn g_originalGetArch{};
LPVOID g_archHookTarget{};
void* g_archTargetGpu{};
bool g_archHookInstalled{};

int __cdecl nrHostGetArchInfoHook(void* gpu,NvArchInfo* info) {
    if(!g_originalGetArch) return -1;
    const int result=g_originalGetArch(gpu,info);
    if(result!=0 || !info || gpu!=g_archTargetGpu) return result;
    const unsigned group=info->architecture&0xFFFFFFF0u;
    if(group==NV_ARCH_TURING || group==NV_ARCH_AMPERE || group==NV_ARCH_ADA) {
        info->architecture=NV_ARCH_BLACKWELL_COMPAT;
        info->implementation=0x3u;
        info->revision=0xA1u;
    }
    return result;
}

void setMessage(Shared* s,PipelineStage failure,std::int32_t result,const std::wstring& text) {
    s->failureStage=static_cast<std::uint32_t>(failure);s->lastResult=result;wcsncpy_s(s->message,text.c_str(),_TRUNCATE);
}
void mark(Shared* s,PipelineStage stage){s->stageMask|=pipelineStageBit(stage);}

std::filesystem::path logDir(){wchar_t p[MAX_PATH]{};DWORD n=GetEnvironmentVariableW(L"LOCALAPPDATA",p,MAX_PATH);std::filesystem::path d=(n&&n<MAX_PATH)?p:std::filesystem::temp_directory_path();d/=L"UniversalDLSS5";d/=L"ngx-host";std::error_code ec;std::filesystem::create_directories(d,ec);return d;}

std::filesystem::path executableDir(){wchar_t p[32768]{};const DWORD n=GetModuleFileNameW(nullptr,p,_countof(p));return (n&&n<_countof(p))?std::filesystem::path(p).parent_path():std::filesystem::current_path();}

NVSDK_NGX_Result NVSDK_CONV ratioCallback(NVSDK_NGX_Parameter* p) noexcept { if(!p)return NVSDK_NGX_Result_FAIL_InvalidParameter; p->Set(kScalingRatio,1.0f);return NVSDK_NGX_Result_Success; }

struct Host {
    Shared* shared{};HANDLE mapping{},frameEvent{},stopEvent{},doneEvent{},bridgeProcess{};std::wstring session;
    ComPtr<IDXGIAdapter1> adapter;ComPtr<ID3D12Device> device;ComPtr<ID3D12CommandQueue> queue;ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;ComPtr<ID3D12Fence> sharedFence;ComPtr<ID3D12Fence> localFence;HANDLE localEvent{};UINT64 localFenceValue{};
    ComPtr<ID3D12Resource> color,output,motion,depth,controlMask;LONG configSeen{};LONG frameSeen{};
    NVSDK_NGX_Parameter* params{};NVSDK_NGX_Handle* feature{};bool coreInitialized{};bool reset{true};Route route{Route::None};
    HMODULE forwarder{},nvapiModule{};SnippetInitExtFn snInit{};SnippetPopulateFn snPopulate{};SnippetCreateFn snCreate{};SnippetEvaluateFn snEvaluate{};SnippetReleaseFn snRelease{};SnippetShutdownFn snShutdown{};bool snippetInitialized{};
    std::wstring architectureNote;

    ~Host(){shutdown();}

    bool openSession(const std::wstring& name){session=name;mapping=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,name.c_str());if(!mapping)return false;shared=(Shared*)MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(Shared));if(!shared||shared->magic!=kMagic||shared->abi!=kAbi)return false;mark(shared,PipelineStage::HostStarted);frameEvent=OpenEventW(SYNCHRONIZE|EVENT_MODIFY_STATE,FALSE,(name+L".Frame").c_str());stopEvent=OpenEventW(SYNCHRONIZE,FALSE,(name+L".Stop").c_str());doneEvent=OpenEventW(SYNCHRONIZE|EVENT_MODIFY_STATE,FALSE,(name+L".Done").c_str());if(!frameEvent||!stopEvent||!doneEvent)return false;shared->hostPid=GetCurrentProcessId();bridgeProcess=OpenProcess(SYNCHRONIZE,FALSE,shared->bridgePid);mark(shared,PipelineStage::HostConnected);InterlockedExchange(&shared->state,(LONG)State::WaitingConfig);wcscpy_s(shared->message,L"External NR host waiting for GPU resource configuration");return true;}

    bool findAdapter(){ComPtr<IDXGIFactory6> f;if(FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&f))))return false;for(UINT i=0;;++i){ComPtr<IDXGIAdapter1>a;if(f->EnumAdapters1(i,&a)==DXGI_ERROR_NOT_FOUND)break;DXGI_ADAPTER_DESC1 d{};a->GetDesc1(&d);if(d.AdapterLuid.HighPart==shared->adapterHigh&&d.AdapterLuid.LowPart==shared->adapterLow){adapter=a;break;}}if(!adapter)return false;if(FAILED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device))))return false;D3D12_COMMAND_QUEUE_DESC q{};q.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;if(FAILED(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue))))return false;if(FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator))))return false;if(FAILED(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list))))return false;list->Close();if(FAILED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&localFence))))return false;localEvent=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!localEvent)return false;mark(shared,PipelineStage::NeuralD3D12Ready);return true;}

    bool openTransferred(std::uint64_t wireHandle,REFIID iid,void** out,HRESULT& result){
        if(!wireHandle){result=E_INVALIDARG;return false;}
        HANDLE h=reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(wireHandle));
        result=device->OpenSharedHandle(h,iid,out);
        CloseHandle(h);
        return SUCCEEDED(result);
    }

    void removeArchitectureCompatibility(){
        if(g_archHookInstalled && g_archHookTarget){MH_DisableHook(g_archHookTarget);MH_RemoveHook(g_archHookTarget);}
        g_archHookInstalled=false;g_archHookTarget=nullptr;g_originalGetArch=nullptr;g_archTargetGpu=nullptr;
        MH_Uninitialize();
        if(nvapiModule){FreeLibrary(nvapiModule);nvapiModule=nullptr;}
    }

    bool prepareArchitectureCompatibility(){
        shared->realGpuArchitecture=0;shared->reportedGpuArchitecture=0;shared->architectureCompatibilityActive=0;architectureNote.clear();
        nvapiModule=LoadLibraryW(L"nvapi64.dll");
        if(!nvapiModule){architectureNote=L"NVAPI unavailable; architecture compatibility not installed";return true;}
        auto query=reinterpret_cast<NvApiQueryInterfaceFn>(GetProcAddress(nvapiModule,"nvapi_QueryInterface"));
        if(!query){architectureNote=L"nvapi_QueryInterface missing; architecture compatibility not installed";return true;}
        auto init=reinterpret_cast<NvApiInitializeFn>(query(NVAPI_ID_INITIALIZE));
        auto enumerate=reinterpret_cast<NvApiEnumPhysicalGpusFn>(query(NVAPI_ID_ENUM_GPUS));
        auto getArch=reinterpret_cast<NvApiGetArchInfoFn>(query(NVAPI_ID_GET_ARCH));
        if(!init||!enumerate||!getArch||init()!=0){architectureNote=L"NVAPI architecture interfaces unavailable";return true;}
        void* handles[64]{};int count=0;
        if(enumerate(handles,&count)!=0 || count<=0){architectureNote=L"NVAPI returned no physical NVIDIA GPU";return true;}
        NvArchInfo info{};info.version=static_cast<unsigned>(sizeof(NvArchInfo))|(2u<<16);
        if(getArch(handles[0],&info)!=0){info={};info.version=static_cast<unsigned>(sizeof(NvArchInfo))|(1u<<16);if(getArch(handles[0],&info)!=0){architectureNote=L"NvAPI_GPU_GetArchInfo failed";return true;}}
        shared->realGpuArchitecture=info.architecture;shared->reportedGpuArchitecture=info.architecture;
        if(!shared->allowUnsupportedHardware){architectureNote=L"RTX compatibility disabled by settings";return true;}
        if(count!=1){architectureNote=L"RTX compatibility skipped: multiple NVIDIA physical GPUs detected";return true;}
        const unsigned group=info.architecture&0xFFFFFFF0u;
        if(group!=NV_ARCH_TURING && group!=NV_ARCH_AMPERE && group!=NV_ARCH_ADA){architectureNote=L"GPU architecture does not require RTX compatibility remapping";return true;}
        const MH_STATUS initStatus=MH_Initialize();
        if(initStatus!=MH_OK && initStatus!=MH_ERROR_ALREADY_INITIALIZED){architectureNote=L"MinHook initialization failed in NRHost";return true;}
        g_archTargetGpu=handles[0];
        g_archHookTarget=reinterpret_cast<LPVOID>(getArch);
        const MH_STATUS createStatus=MH_CreateHook(g_archHookTarget,reinterpret_cast<LPVOID>(&nrHostGetArchInfoHook),reinterpret_cast<LPVOID*>(&g_originalGetArch));
        if(createStatus!=MH_OK){g_archHookTarget=nullptr;architectureNote=L"NvAPI architecture compatibility hook creation failed";return true;}
        if(MH_EnableHook(g_archHookTarget)!=MH_OK){MH_RemoveHook(g_archHookTarget);g_archHookTarget=nullptr;g_originalGetArch=nullptr;g_archTargetGpu=nullptr;architectureNote=L"NvAPI architecture compatibility hook enable failed";return true;}
        g_archHookInstalled=true;shared->reportedGpuArchitecture=NV_ARCH_BLACKWELL_COMPAT;shared->architectureCompatibilityActive=1;
        architectureNote=L"NRHost-only RTX architecture compatibility active";
        return true;
    }

    bool coreInit(){if(coreInitialized)return true;const wchar_t* paths[]={shared->runtimePath};NVSDK_NGX_FeatureCommonInfo fi{};fi.PathListInfo.Path=paths;fi.PathListInfo.Length=1;const auto r=NVSDK_NGX_D3D12_Init_with_ProjectID(kHostProjectId,NVSDK_NGX_ENGINE_TYPE_CUSTOM,kHostEngineVersion,logDir().c_str(),device.Get(),&fi,NVSDK_NGX_Version_API);if(NVSDK_NGX_FAILED(r)){setMessage(shared,PipelineStage::NgxCoreInitFailed,(int)r,L"NRHost: NGX core Project-ID initialization failed");return false;}coreInitialized=true;mark(shared,PipelineStage::NgxCoreInitialized);auto pr=NVSDK_NGX_D3D12_GetCapabilityParameters(&params);if(NVSDK_NGX_FAILED(pr)||!params){setMessage(shared,PipelineStage::NgxCoreInitFailed,(int)pr,L"NRHost: GetCapabilityParameters failed");return false;}return true;}

    void fillCreate(){params->Set(kWidth,shared->width);params->Set(kHeight,shared->height);params->Set(kInputWidth,shared->width);params->Set(kInputHeight,shared->height);params->Set(kOutputWidth,shared->width);params->Set(kOutputHeight,shared->height);params->Set(kOutputWidth2,shared->width);params->Set(kOutputHeight2,shared->height);params->Set(kUpscaling,0u);params->Set(kScale,1.0f);params->Set(kScalingRatio,1.0f);params->Set(kScalingRatioCallback,(unsigned long long)(uintptr_t)&ratioCallback);params->Set(kPreset,shared->nrPreset);params->Set(NVSDK_NGX_Parameter_PerfQualityValue,(int)NVSDK_NGX_PerfQuality_Value_Balanced);params->Set(NVSDK_NGX_Parameter_Width,shared->width);params->Set(NVSDK_NGX_Parameter_Height,shared->height);params->Set(NVSDK_NGX_Parameter_CreationNodeMask,1u);params->Set(NVSDK_NGX_Parameter_VisibilityNodeMask,1u);}

    bool beginList(){if(FAILED(allocator->Reset()))return false;return SUCCEEDED(list->Reset(allocator.Get(),nullptr));}
    bool executeAndDrain(){if(FAILED(list->Close()))return false;ID3D12CommandList* l[]={list.Get()};queue->ExecuteCommandLists(1,l);UINT64 v=++localFenceValue;if(FAILED(queue->Signal(localFence.Get(),v)))return false;if(localFence->GetCompletedValue()<v){if(FAILED(localFence->SetEventOnCompletion(v,localEvent)))return false;WaitForSingleObject(localEvent,3000);}return true;}

    bool tryCoreCreate(std::int32_t& result){if(!beginList())return false;fillCreate();NVSDK_NGX_Handle* h{};auto r=NVSDK_NGX_D3D12_CreateFeature(list.Get(),kFeature,params,&h);result=(int)r;if(NVSDK_NGX_FAILED(r)||!h){list->Close();return false;}feature=h;route=Route::CoreDispatch;InterlockedExchange(&shared->route,(LONG)route);mark(shared,PipelineStage::FeatureCreated);return executeAndDrain();}

    bool loadSnippet(std::int32_t coreResult){
        const auto snippetPath=std::filesystem::path(shared->runtimePath)/L"nvngx_dlssnr.dll";
        const auto forwarderPath=executableDir()/L"nvngx.dll_UniversalDLSS5_NRForwarder.dll";
        forwarder=LoadLibraryExW(forwarderPath.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if(!forwarder){setMessage(shared,PipelineStage::SnippetLoadFailed,(int)GetLastError(),L"NRHost: caller-compatible NR forwarder is missing beside NRHost.exe");return false;}
        auto load=reinterpret_cast<ForwarderLoadFn>(GetProcAddress(forwarder,"UdlssNrFwdLoad"));
        auto where=reinterpret_cast<ForwarderPathFn>(GetProcAddress(forwarder,"UdlssNrFwdPath"));
        snInit=reinterpret_cast<SnippetInitExtFn>(GetProcAddress(forwarder,"UdlssNrFwdInitExt"));
        snPopulate=reinterpret_cast<SnippetPopulateFn>(GetProcAddress(forwarder,"UdlssNrFwdPopulate"));
        snCreate=reinterpret_cast<SnippetCreateFn>(GetProcAddress(forwarder,"UdlssNrFwdCreate"));
        snEvaluate=reinterpret_cast<SnippetEvaluateFn>(GetProcAddress(forwarder,"UdlssNrFwdEvaluate"));
        snRelease=reinterpret_cast<SnippetReleaseFn>(GetProcAddress(forwarder,"UdlssNrFwdRelease"));
        snShutdown=reinterpret_cast<SnippetShutdownFn>(GetProcAddress(forwarder,"UdlssNrFwdShutdown"));
        auto app=reinterpret_cast<ForwarderGetU32Fn>(GetProcAddress(forwarder,"UdlssNrFwdGetApplicationId"));
        auto ver=reinterpret_cast<ForwarderGetU32Fn>(GetProcAddress(forwarder,"UdlssNrFwdGetSnippetVersion"));
        if(!load||!snInit||!snPopulate||!snCreate||!snEvaluate||!snRelease||!snShutdown){setMessage(shared,PipelineStage::SnippetLoadFailed,0,L"NRHost: NR forwarder is missing required exports");return false;}
        const int loadResult=load(snippetPath.c_str());
        if(loadResult!=0){setMessage(shared,PipelineStage::SnippetLoadFailed,loadResult,L"NRHost: NR forwarder could not load nvngx_dlssnr.dll");return false;}
        wchar_t actualForwarder[32768]{};if(where)where(actualForwarder,_countof(actualForwarder));
        if(actualForwarder[0] && wcsstr(actualForwarder,L"nvngx.dll")==nullptr){setMessage(shared,PipelineStage::SnippetLoadFailed,0,L"NRHost: forwarder path lacks required nvngx.dll compatibility marker");return false;}
        mark(shared,PipelineStage::SnippetLoaded);
        const std::uint32_t discoveredAppId=app?app():0u;
        const unsigned long long appId=discoveredAppId?discoveredAppId:kFallbackSnippetApplicationId;
        shared->snippetApplicationId=(UINT)appId;shared->snippetVersion=ver?ver():0;
        const auto ir=snInit(appId,logDir().c_str(),device.Get(),NVSDK_NGX_Version_API,params);
        if(NVSDK_NGX_FAILED(ir)){wchar_t msg[384]{};swprintf_s(msg,L"NRHost: core CreateFeature(18)=0x%08X; forwarder Init_Ext=0x%08X; real arch=0x%X reported=0x%X",(unsigned)coreResult,(unsigned)ir,shared->realGpuArchitecture,shared->reportedGpuArchitecture);setMessage(shared,PipelineStage::SnippetInitFailed,(int)ir,msg);return false;}
        snippetInitialized=true;
        const auto pr=snPopulate(params);
        if(NVSDK_NGX_FAILED(pr)){setMessage(shared,PipelineStage::SnippetInitFailed,(int)pr,L"NRHost: forwarder PopulateParameters_Impl failed");return false;}
        mark(shared,PipelineStage::SnippetInitialized);
        if(!beginList())return false;fillCreate();NVSDK_NGX_Handle* h{};
        const auto cr=snCreate(list.Get(),kFeature,params,&h);
        if(NVSDK_NGX_FAILED(cr)||!h){list->Close();wchar_t msg[384]{};swprintf_s(msg,L"NRHost: forwarder CreateFeature(18)=0x%08X; real arch=0x%X reported=0x%X; compatibility=%s",(unsigned)cr,shared->realGpuArchitecture,shared->reportedGpuArchitecture,shared->architectureCompatibilityActive?L"active":L"off");setMessage(shared,PipelineStage::FeatureCreateFailed,(int)cr,msg);return false;}
        feature=h;route=Route::SignedSnippet;InterlockedExchange(&shared->route,(LONG)route);mark(shared,PipelineStage::FeatureCreated);return executeAndDrain();
    }

    bool configure(){InterlockedExchange(&shared->state,(LONG)State::Configuring);releaseFeature();color.Reset();output.Reset();motion.Reset();depth.Reset();controlMask.Reset();if(!device&&!findAdapter()){setMessage(shared,PipelineStage::HostRuntimeFailed,0,L"NRHost: could not create D3D12 device on the source adapter");return false;}HRESULT openHr=S_OK;if(!sharedFence){if(!openTransferred(shared->fenceHandle,IID_PPV_ARGS(&sharedFence),openHr)){setMessage(shared,PipelineStage::HostResourceShareFailed,(int)openHr,L"NRHost: could not open duplicated shared GPU fence handle");return false;}shared->fenceHandle=0;}
        if(!openTransferred(shared->colorHandle,IID_PPV_ARGS(&color),openHr)){setMessage(shared,PipelineStage::HostResourceShareFailed,(int)openHr,L"NRHost: could not open duplicated color texture handle");return false;}shared->colorHandle=0;
        if(!openTransferred(shared->outputHandle,IID_PPV_ARGS(&output),openHr)){setMessage(shared,PipelineStage::HostResourceShareFailed,(int)openHr,L"NRHost: could not open duplicated output texture handle");return false;}shared->outputHandle=0;
        if(!openTransferred(shared->motionHandle,IID_PPV_ARGS(&motion),openHr)){setMessage(shared,PipelineStage::HostResourceShareFailed,(int)openHr,L"NRHost: could not open duplicated motion texture handle");return false;}shared->motionHandle=0;
        if(!openTransferred(shared->depthHandle,IID_PPV_ARGS(&depth),openHr)){setMessage(shared,PipelineStage::HostResourceShareFailed,(int)openHr,L"NRHost: could not open duplicated depth texture handle");return false;}shared->depthHandle=0;if(!openTransferred(shared->controlMaskHandle,IID_PPV_ARGS(&controlMask),openHr)){setMessage(shared,PipelineStage::HostResourceShareFailed,(int)openHr,L"NRHost: could not open duplicated control-mask texture handle");return false;}shared->controlMaskHandle=0;mark(shared,PipelineStage::HostResourcesShared);removeArchitectureCompatibility();prepareArchitectureCompatibility();if(!coreInit())return false;std::int32_t coreResult=0;if(!tryCoreCreate(coreResult)){if(!loadSnippet(coreResult))return false;}shared->failureStage=(UINT)PipelineStage::None;shared->lastResult=1;wcscpy_s(shared->message,route==Route::CoreDispatch?L"NRHost ready: feature 18 created through standard NGX core dispatch":L"NRHost ready: feature 18 created through signed-snippet fallback");InterlockedExchange(&shared->state,(LONG)State::Ready);reset=true;return true;}

    static D3D12_RESOURCE_BARRIER tr(ID3D12Resource* r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b){D3D12_RESOURCE_BARRIER x{};x.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;x.Transition.pResource=r;x.Transition.StateBefore=a;x.Transition.StateAfter=b;x.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;return x;}
    void setFrameParams(){
        params->Set(kColor,color.Get());params->Set(kOutput,output.Get());params->Set(kMVec,motion.Get());params->Set(kDepth,depth.Get());
        params->Set(kControlMask,shared->useControlMask?controlMask.Get():static_cast<ID3D12Resource*>(nullptr));
        params->Set(kMVecScaleX,shared->motionScaleX);params->Set(kMVecScaleY,shared->motionScaleY);
        params->Set(kDepthInverted,shared->depthInverted);params->Set(kEnabled,1u);params->Set(kReset,(reset||shared->frameReset)?1u:0u);
        params->Set(kStyle,shared->nrStyle);params->Set(kIntensity,shared->nrIntensity);params->Set(kLocalTone,shared->nrTone);
        params->Set(kLocalStructure,shared->nrStructure);params->Set(kSkinStructure,shared->nrSkinStructure);
        params->Set(kUseAutoMask,shared->nrAutoMask);params->Set(kUiCorrection,shared->nrUiCorrection);
        params->Set(kPaperWhite,shared->nrPaperWhite);params->Set(kTransferStrength,shared->nrTransferStrength);params->Set(kColorStrength,shared->nrColorStrength);
        params->Set(kColorSubrectW,shared->width);params->Set(kColorSubrectH,shared->height);params->Set(kOutputSubrectW,shared->width);params->Set(kOutputSubrectH,shared->height);
        params->Set(kMVecSubrectW,shared->width);params->Set(kMVecSubrectH,shared->height);params->Set(kDepthSubrectW,shared->width);params->Set(kDepthSubrectH,shared->height);
        params->Set(kControlSubrectW,shared->width);params->Set(kControlSubrectH,shared->height);
    }

    bool recordFallbackCopy(){if(!beginList())return false;std::array<D3D12_RESOURCE_BARRIER,2>b={tr(color.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_SOURCE),tr(output.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST)};list->ResourceBarrier(2,b.data());list->CopyResource(output.Get(),color.Get());b={tr(color.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COMMON),tr(output.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COMMON)};list->ResourceBarrier(2,b.data());return SUCCEEDED(list->Close());}

    void processFrame(){const LONG seq=InterlockedCompareExchange(&shared->frameSeq,0,0);if(seq<=frameSeen||!feature)return;frameSeen=seq;const UINT64 in=(UINT64)InterlockedCompareExchange64(&shared->inputFenceValue,0,0),out=(UINT64)InterlockedCompareExchange64(&shared->outputFenceValue,0,0);queue->Wait(sharedFence.Get(),in);bool success=false;NVSDK_NGX_Result r=NVSDK_NGX_Result_Fail;if(beginList()){std::array<D3D12_RESOURCE_BARRIER,5>b={tr(color.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),tr(motion.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),tr(depth.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),tr(controlMask.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),tr(output.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_UNORDERED_ACCESS)};list->ResourceBarrier(5,b.data());setFrameParams();r=route==Route::CoreDispatch?NVSDK_NGX_D3D12_EvaluateFeature(list.Get(),feature,params,nullptr):snEvaluate(list.Get(),feature,params,nullptr);if(!NVSDK_NGX_FAILED(r)){b={tr(color.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COMMON),tr(motion.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COMMON),tr(depth.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COMMON),tr(controlMask.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COMMON),tr(output.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COMMON)};list->ResourceBarrier(5,b.data());if(SUCCEEDED(list->Close())){ID3D12CommandList*l[]={list.Get()};queue->ExecuteCommandLists(1,l);success=true;}}else list->Close();}
        if(!success){recordFallbackCopy();ID3D12CommandList*l[]={list.Get()};queue->ExecuteCommandLists(1,l);}const HRESULT signalHr=queue->Signal(sharedFence.Get(),out);if(FAILED(signalHr)){setMessage(shared,PipelineStage::HostRuntimeFailed,(int)signalHr,L"NRHost: failed to enqueue output fence signal");InterlockedExchange(&shared->state,(LONG)State::Error);InterlockedExchange(&shared->completedSeq,seq);SetEvent(doneEvent);return;}shared->lastResult=(int)r;if(success){mark(shared,PipelineStage::FeatureEvaluated);shared->failureStage=(UINT)PipelineStage::None;wcscpy_s(shared->message,route==Route::CoreDispatch?L"NRHost ACTIVE: feature 18 evaluated through NGX core dispatch":L"NRHost ACTIVE: feature 18 evaluated through signed-snippet fallback");InterlockedExchange(&shared->state,(LONG)State::Active);reset=false;}else{setMessage(shared,PipelineStage::FeatureEvaluateFailed,(int)r,L"NRHost: feature 18 evaluate failed; host returned GPU passthrough output");InterlockedExchange(&shared->state,(LONG)State::Error);}InterlockedExchange(&shared->completedSeq,seq);InterlockedExchange(&shared->enqueuedSeq,seq);SetEvent(doneEvent);}

    void releaseFeature(){if(feature){if(route==Route::CoreDispatch)NVSDK_NGX_D3D12_ReleaseFeature(feature);else if(snRelease)snRelease(feature);feature=nullptr;}route=Route::None;InterlockedExchange(&shared->route,(LONG)Route::None);}
    void shutdown(){if(shared)InterlockedExchange(&shared->state,(LONG)State::Stopping);releaseFeature();if(snippetInitialized&&snShutdown&&device)snShutdown(device.Get());snippetInitialized=false;if(forwarder){FreeLibrary(forwarder);forwarder=nullptr;}removeArchitectureCompatibility();if(params){NVSDK_NGX_D3D12_DestroyParameters(params);params=nullptr;}if(coreInitialized&&device)NVSDK_NGX_D3D12_Shutdown1(device.Get());coreInitialized=false;if(localEvent){CloseHandle(localEvent);localEvent=nullptr;}if(shared){UnmapViewOfFile(shared);shared=nullptr;}if(mapping){CloseHandle(mapping);mapping=nullptr;}if(frameEvent){CloseHandle(frameEvent);frameEvent=nullptr;}if(stopEvent){CloseHandle(stopEvent);stopEvent=nullptr;}if(doneEvent){CloseHandle(doneEvent);doneEvent=nullptr;}if(bridgeProcess){CloseHandle(bridgeProcess);bridgeProcess=nullptr;}}
};

std::wstring sessionArg(){int argc=0;auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);std::wstring out;if(argv){for(int i=1;i+1<argc;++i)if(std::wstring(argv[i])==L"--session")out=argv[i+1];LocalFree(argv);}return out;}
}

int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int){const auto session=sessionArg();if(session.empty())return 2;Host h;if(!h.openSession(session))return 3;while(true){HANDLE handles[3]={h.stopEvent,h.frameEvent,h.bridgeProcess};DWORD count=h.bridgeProcess?3:2;DWORD w=WaitForMultipleObjects(count,handles,FALSE,INFINITE);if(w==WAIT_OBJECT_0||w==WAIT_OBJECT_0+2||InterlockedCompareExchange(&h.shared->stopRequested,0,0))break;if(w!=WAIT_OBJECT_0+1)continue;LONG cfg=InterlockedCompareExchange(&h.shared->configGeneration,0,0);if(cfg!=h.configSeen){h.configSeen=cfg;if(!h.configure()){InterlockedExchange(&h.shared->state,(LONG)State::Error);continue;}}h.processFrame();}return 0;}
