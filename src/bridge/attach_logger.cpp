#include "attach_logger.hpp"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <atomic>

namespace fs=std::filesystem;
namespace udlss::bridge {
namespace {
std::mutex g_mutex;
fs::path g_path;
wchar_t g_crashPath[32768]{};
std::atomic<CrashStage> g_crashStage{CrashStage::Idle};
PVOID g_veh{};

const wchar_t* label(AttachLogStage stage){
    switch(stage){
    case AttachLogStage::BridgeThreadStarted:return L"Bridge thread started";
    case AttachLogStage::SharedControlOpened:return L"Shared control opened";
    case AttachLogStage::SafeAttachDelayComplete:return L"Initial safe-attach delay complete";
    case AttachLogStage::DxgiBootstrapCreated:return L"DXGI bootstrap device/swapchain created";
    case AttachLogStage::CoreHooksInstalled:return L"Core DXGI/D3D12 queue hooks installed";
    case AttachLogStage::WaitingForPresent:return L"Waiting for stable Present stream";
    case AttachLogStage::PresentObserved:return L"Present observed";
    case AttachLogStage::SourceD3D11Detected:return L"D3D11 source detected";
    case AttachLogStage::SourceD3D12Detected:return L"D3D12 source detected";
    case AttachLogStage::D3D12QueueCaptured:return L"D3D12 presenting DIRECT queue proven";
    case AttachLogStage::D3D11TrackersInstalled:return L"D3D11 guide trackers installed";
    case AttachLogStage::D3D12TrackersInstalled:return L"D3D12 guide/queue tracker installed";
    case AttachLogStage::GameGuideHooksInstalled:return L"Game temporal-guide hooks installed";
    case AttachLogStage::SafeAttachStable:return L"Primary swapchain stable; processing enabled";
    case AttachLogStage::D3D11PipelineInitialized:return L"D3D11 pipeline initialized";
    case AttachLogStage::D3D12On12Initialized:return L"D3D12/D3D11On12 staging pipeline initialized";
    case AttachLogStage::NeuralProcessingStarted:return L"Neural processing started";
    case AttachLogStage::NvidiaDriverDetected:return L"NVIDIA driver detected";
    case AttachLogStage::ColorSpaceChanged:return L"Swapchain color space / HDR mode changed";
    case AttachLogStage::Unloading:return L"Bridge unloading";
    case AttachLogStage::Failure:return L"Failure";
    default:return L"Unknown";
    }
}
const wchar_t* crashLabel(CrashStage stage){
    switch(stage){
    case CrashStage::Idle:return L"idle";
    case CrashStage::ProcessEntry:return L"Present processing entry";
    case CrashStage::QueueProof:return L"D3D12 queue proof";
    case CrashStage::BackbufferAcquire:return L"D3D12 backbuffer acquire";
    case CrashStage::PreCopy:return L"D3D12 backbuffer -> owned staging copy";
    case CrashStage::On12Acquire:return L"D3D11On12 AcquireWrappedResources";
    case CrashStage::NeuralProcess:return L"D3D11/Feature18 neural processing";
    case CrashStage::On12Release:return L"D3D11On12 ReleaseWrappedResources";
    case CrashStage::PostCopy:return L"D3D12 owned staging -> backbuffer copy";
    case CrashStage::ProcessComplete:return L"injected processing complete";
    case CrashStage::CallingPresent:return L"calling game/DXGI Present";
    case CrashStage::PresentReturned:return L"game/DXGI Present returned";
    default:return L"unknown";
    }
}
std::string utf8(std::wstring_view text){
    if(text.empty())return{};
    const int n=WideCharToMultiByte(CP_UTF8,0,text.data(),static_cast<int>(text.size()),nullptr,0,nullptr,nullptr);
    std::string out(static_cast<size_t>(n),'\0');
    if(n)WideCharToMultiByte(CP_UTF8,0,text.data(),static_cast<int>(text.size()),out.data(),n,nullptr,nullptr);
    return out;
}
void appendCrashLine(const wchar_t* line){
    if(!g_crashPath[0]||!line)return;
    HANDLE h=CreateFileW(g_crashPath,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h==INVALID_HANDLE_VALUE)return;
    const int n=WideCharToMultiByte(CP_UTF8,0,line,-1,nullptr,0,nullptr,nullptr);
    if(n>1){std::string text(static_cast<size_t>(n),'\0');WideCharToMultiByte(CP_UTF8,0,line,-1,text.data(),n,nullptr,nullptr);text.resize(static_cast<size_t>(n-1));DWORD written{};WriteFile(h,text.data(),static_cast<DWORD>(text.size()),&written,nullptr);const char eol[]="\r\n";WriteFile(h,eol,2,&written,nullptr);FlushFileBuffers(h);}
    CloseHandle(h);
}
LONG CALLBACK crashVeh(EXCEPTION_POINTERS* ep){
    if(!ep||!ep->ExceptionRecord)return EXCEPTION_CONTINUE_SEARCH;
    const DWORD code=ep->ExceptionRecord->ExceptionCode;
    if(code!=EXCEPTION_ACCESS_VIOLATION&&code!=EXCEPTION_ILLEGAL_INSTRUCTION&&code!=EXCEPTION_STACK_OVERFLOW&&code!=0xC0000374u)return EXCEPTION_CONTINUE_SEARCH;
    wchar_t line[768]{};
    swprintf_s(line,L"[%llu] exception observed: code=0x%08lX address=%p | crash stage: %ls",GetTickCount64(),code,ep->ExceptionRecord->ExceptionAddress,crashLabel(g_crashStage.load(std::memory_order_relaxed)));
    appendCrashLine(line);
    return EXCEPTION_CONTINUE_SEARCH;
}
}
void initializeAttachLog(){
    std::scoped_lock lock(g_mutex);
    wchar_t local[32768]{};
    const DWORD n=GetEnvironmentVariableW(L"LOCALAPPDATA",local,_countof(local));
    fs::path base=n?fs::path(std::wstring(local,n)):fs::temp_directory_path();
    base/=L"UniversalDLSS5";base/=L"logs";
    std::error_code ec;fs::create_directories(base,ec);
    std::wstringstream name;name<<L"attach-"<<GetCurrentProcessId()<<L".log";
    g_path=base/name.str();
    wcsncpy_s(g_crashPath,g_path.c_str(),_TRUNCATE);
    std::ofstream out(g_path,std::ios::binary|std::ios::trunc);
    if(out)out<<"UniversalDLSS5 v0.3.1 staged attach/crash log\r\nPID="<<GetCurrentProcessId()<<"\r\n";
    if(!g_veh)g_veh=AddVectoredExceptionHandler(0,crashVeh);
}
void logAttachStage(AttachLogStage stage,std::wstring_view detail){
    std::scoped_lock lock(g_mutex);
    if(g_path.empty())return;
    std::ofstream out(g_path,std::ios::binary|std::ios::app);
    if(!out)return;
    out<<'['<<GetTickCount64()<<"] "<<utf8(label(stage));
    if(!detail.empty())out<<" | "<<utf8(detail);
    out<<"\r\n";
}
void setCrashStage(CrashStage stage){g_crashStage.store(stage,std::memory_order_relaxed);}
CrashStage crashStage(){return g_crashStage.load(std::memory_order_relaxed);}
void removeCrashStageHandler(){if(g_veh){RemoveVectoredExceptionHandler(g_veh);g_veh=nullptr;}g_crashStage.store(CrashStage::Idle,std::memory_order_relaxed);}
}
