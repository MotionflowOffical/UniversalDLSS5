#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <nvsdk_ngx.h>
#include <nvsdk_ngx_params.h>
#include <cstdint>

namespace {
using InitExtFn = NVSDK_NGX_Result (NVSDK_CONV *)(unsigned long long,const wchar_t*,ID3D12Device*,NVSDK_NGX_Version,const NVSDK_NGX_Parameter*);
using PopulateFn = NVSDK_NGX_Result (NVSDK_CONV *)(NVSDK_NGX_Parameter*);
using CreateFn = NVSDK_NGX_Result (NVSDK_CONV *)(ID3D12GraphicsCommandList*,NVSDK_NGX_Feature,NVSDK_NGX_Parameter*,NVSDK_NGX_Handle**);
using EvaluateFn = NVSDK_NGX_Result (NVSDK_CONV *)(ID3D12GraphicsCommandList*,const NVSDK_NGX_Handle*,const NVSDK_NGX_Parameter*,PFN_NVSDK_NGX_ProgressCallback);
using ReleaseFn = NVSDK_NGX_Result (NVSDK_CONV *)(NVSDK_NGX_Handle*);
using ShutdownFn = NVSDK_NGX_Result (NVSDK_CONV *)(ID3D12Device*);
using GetU32Fn = std::uint32_t (NVSDK_CONV *)();

HMODULE g_runtime{};
InitExtFn g_init{};
PopulateFn g_populate{};
CreateFn g_create{};
EvaluateFn g_evaluate{};
ReleaseFn g_release{};
ShutdownFn g_shutdown{};
GetU32Fn g_applicationId{};
GetU32Fn g_snippetVersion{};
volatile LONG g_callResult{};

void remember(NVSDK_NGX_Result value) noexcept {
    InterlockedExchange(&g_callResult, static_cast<LONG>(value));
}

template<class T>
T resolve(const char* name) noexcept {
    return g_runtime ? reinterpret_cast<T>(GetProcAddress(g_runtime,name)) : nullptr;
}
}

extern "C" __declspec(dllexport) int WINAPI UdlssNrFwdLoad(const wchar_t* path) {
    if(g_runtime) return 0;
    if(!path || !*path) return ERROR_INVALID_PARAMETER;
    g_runtime=LoadLibraryExW(path,nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if(!g_runtime) return static_cast<int>(GetLastError()?GetLastError():ERROR_MOD_NOT_FOUND);
    g_init=resolve<InitExtFn>("NVSDK_NGX_D3D12_Init_Ext");
    g_populate=resolve<PopulateFn>("NVSDK_NGX_D3D12_PopulateParameters_Impl");
    g_create=resolve<CreateFn>("NVSDK_NGX_D3D12_CreateFeature");
    g_evaluate=resolve<EvaluateFn>("NVSDK_NGX_D3D12_EvaluateFeature");
    g_release=resolve<ReleaseFn>("NVSDK_NGX_D3D12_ReleaseFeature");
    g_shutdown=resolve<ShutdownFn>("NVSDK_NGX_D3D12_Shutdown1");
    g_applicationId=resolve<GetU32Fn>("NVSDK_NGX_GetApplicationId");
    g_snippetVersion=resolve<GetU32Fn>("NVSDK_NGX_GetSnippetVersion");
    if(!g_init||!g_populate||!g_create||!g_evaluate||!g_release||!g_shutdown) return ERROR_PROC_NOT_FOUND;
    return 0;
}

extern "C" __declspec(dllexport) void WINAPI UdlssNrFwdPath(wchar_t* out,unsigned int cch) {
    if(!out||!cch) return;
    out[0]=L'\0';
    HMODULE self{};
    if(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&UdlssNrFwdPath),&self)) GetModuleFileNameW(self,out,cch);
}

extern "C" __declspec(dllexport) std::uint32_t WINAPI UdlssNrFwdGetApplicationId() {
    const auto value=g_applicationId?g_applicationId():0u;
    InterlockedExchange(&g_callResult,static_cast<LONG>(value));
    return value;
}

extern "C" __declspec(dllexport) std::uint32_t WINAPI UdlssNrFwdGetSnippetVersion() {
    const auto value=g_snippetVersion?g_snippetVersion():0u;
    InterlockedExchange(&g_callResult,static_cast<LONG>(value));
    return value;
}

extern "C" __declspec(dllexport) __declspec(noinline)
NVSDK_NGX_Result NVSDK_CONV UdlssNrFwdInitExt(unsigned long long appId,const wchar_t* dataPath,ID3D12Device* device,
    NVSDK_NGX_Version version,const NVSDK_NGX_Parameter* params) {
    if(!g_init) return NVSDK_NGX_Result_FAIL_NotInitialized;
    const auto result=g_init(appId,dataPath,device,version,params);
    remember(result);
    return result;
}

extern "C" __declspec(dllexport) __declspec(noinline)
NVSDK_NGX_Result NVSDK_CONV UdlssNrFwdPopulate(NVSDK_NGX_Parameter* params) {
    if(!g_populate) return NVSDK_NGX_Result_FAIL_NotInitialized;
    const auto result=g_populate(params);
    remember(result);
    return result;
}

extern "C" __declspec(dllexport) __declspec(noinline)
NVSDK_NGX_Result NVSDK_CONV UdlssNrFwdCreate(ID3D12GraphicsCommandList* list,NVSDK_NGX_Feature feature,
    NVSDK_NGX_Parameter* params,NVSDK_NGX_Handle** out) {
    if(!g_create) return NVSDK_NGX_Result_FAIL_NotInitialized;
    const auto result=g_create(list,feature,params,out);
    remember(result);
    return result;
}

extern "C" __declspec(dllexport) __declspec(noinline)
NVSDK_NGX_Result NVSDK_CONV UdlssNrFwdEvaluate(ID3D12GraphicsCommandList* list,const NVSDK_NGX_Handle* feature,
    const NVSDK_NGX_Parameter* params,PFN_NVSDK_NGX_ProgressCallback callback) {
    if(!g_evaluate) return NVSDK_NGX_Result_FAIL_NotInitialized;
    const auto result=g_evaluate(list,feature,params,callback);
    remember(result);
    return result;
}

extern "C" __declspec(dllexport) __declspec(noinline)
NVSDK_NGX_Result NVSDK_CONV UdlssNrFwdRelease(NVSDK_NGX_Handle* feature) {
    if(!g_release) return NVSDK_NGX_Result_FAIL_NotInitialized;
    const auto result=g_release(feature);
    remember(result);
    return result;
}

extern "C" __declspec(dllexport) __declspec(noinline)
NVSDK_NGX_Result NVSDK_CONV UdlssNrFwdShutdown(ID3D12Device* device) {
    if(!g_shutdown) return NVSDK_NGX_Result_FAIL_NotInitialized;
    const auto result=g_shutdown(device);
    remember(result);
    return result;
}

extern "C" __declspec(dllexport) void WINAPI UdlssNrFwdUnload() {
    g_init=nullptr; g_populate=nullptr; g_create=nullptr; g_evaluate=nullptr; g_release=nullptr; g_shutdown=nullptr;
    g_applicationId=nullptr; g_snippetVersion=nullptr;
    if(g_runtime){ FreeLibrary(g_runtime); g_runtime=nullptr; }
}

BOOL WINAPI DllMain(HINSTANCE,DWORD,LPVOID){return TRUE;}
