#include "udlss/shared_control.hpp"
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <sddl.h>
#include <cstring>
#include <vector>
#include "udlss/ipc_security.hpp"

namespace udlss {
SharedControl::~SharedControl(){ close(); }
void SharedControl::close(){ if(block_){UnmapViewOfFile(block_);block_=nullptr;} if(mapping_){CloseHandle((HANDLE)mapping_);mapping_=nullptr;} }

bool SharedControl::create(){
    close();
    SECURITY_ATTRIBUTES sa{sizeof(sa)};
    PSECURITY_DESCRIPTOR sd=nullptr;
    HANDLE token=nullptr;
    std::vector<unsigned char> tokenUser;
    LPWSTR sidString=nullptr;
    if(OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token)){
        DWORD bytes=0; GetTokenInformation(token,TokenUser,nullptr,0,&bytes);
        if(bytes){
            tokenUser.resize(bytes);
            if(GetTokenInformation(token,TokenUser,tokenUser.data(),bytes,&bytes)){
                auto* user=reinterpret_cast<TOKEN_USER*>(tokenUser.data());
                if(ConvertSidToStringSidW(user->User.Sid,&sidString)){
                    const std::wstring sddl=lowIntegrityMappingSddl(sidString);
                    if(ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(),SDDL_REVISION_1,&sd,nullptr))
                        sa.lpSecurityDescriptor=sd;
                }
            }
        }
        CloseHandle(token);
    }
    if(sidString) LocalFree(sidString);
    HANDLE h=CreateFileMappingW(INVALID_HANDLE_VALUE,sa.lpSecurityDescriptor?&sa:nullptr,PAGE_READWRITE,0,sizeof(SharedControlBlock),kControlMapName);
    if(sd) LocalFree(sd);
    if(!h) return false;
    bool fresh=GetLastError()!=ERROR_ALREADY_EXISTS;
    auto* p=(SharedControlBlock*)MapViewOfFile(h,FILE_MAP_ALL_ACCESS,0,0,sizeof(SharedControlBlock));
    if(!p){CloseHandle(h);return false;}
    mapping_=h; block_=p;
    if(fresh || p->magic!=kControlMagic || p->abi!=kControlAbi){
        std::memset(p,0,sizeof(*p)); p->magic=kControlMagic; p->abi=kControlAbi; p->settings=defaultSettings();
    }
    return true;
}

bool SharedControl::open(){
    close(); HANDLE h=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,kControlMapName); if(!h) return false;
    auto* p=(SharedControlBlock*)MapViewOfFile(h,FILE_MAP_ALL_ACCESS,0,0,sizeof(SharedControlBlock));
    if(!p){CloseHandle(h);return false;} if(p->magic!=kControlMagic||p->abi!=kControlAbi){UnmapViewOfFile(p);CloseHandle(h);return false;}
    mapping_=h; block_=p; return true;
}

Settings SharedControl::readSettings() const{
    Settings out=defaultSettings(); if(!block_) return out;
    for(int attempt=0;attempt<8;++attempt){
        LONG a=InterlockedCompareExchange((volatile LONG*)&block_->settingsSeq,0,0); if(a&1){YieldProcessor();continue;}
        MemoryBarrier(); out=block_->settings; MemoryBarrier(); LONG b=InterlockedCompareExchange((volatile LONG*)&block_->settingsSeq,0,0);
        if(a==b && !(b&1)){normalize(out);return out;}
    } normalize(out); return out;
}

void SharedControl::writeSettings(const Settings& in){ if(!block_)return; Settings s=in; normalize(s); InterlockedIncrement((volatile LONG*)&block_->settingsSeq); MemoryBarrier(); block_->settings=s; MemoryBarrier(); InterlockedIncrement((volatile LONG*)&block_->settingsSeq); InterlockedIncrement((volatile LONG*)&block_->profileGeneration); }
RuntimeStatus SharedControl::readStatus() const{
    RuntimeStatus out{}; if(!block_)return out;
    for(int attempt=0;attempt<8;++attempt){
        LONG a=InterlockedCompareExchange((volatile LONG*)&block_->statusSeq,0,0); if(a&1){YieldProcessor();continue;} MemoryBarrier();
        RuntimeStatus best{}; for(const auto& s:block_->statuses) if(s.pid && s.lastTickMs>=best.lastTickMs) best=s;
        MemoryBarrier(); LONG b=InterlockedCompareExchange((volatile LONG*)&block_->statusSeq,0,0); if(a==b && !(b&1)) return best;
    } return out;
}
void SharedControl::writeStatus(const RuntimeStatus& s){
    if(!block_)return; InterlockedIncrement((volatile LONG*)&block_->statusSeq); MemoryBarrier();
    int slot=-1,empty=-1,oldest=0; std::uint64_t oldestTick=~0ull;
    for(int i=0;i<32;++i){auto& x=block_->statuses[i]; if(x.pid==s.pid){slot=i;break;} if(!x.pid&&empty<0)empty=i; if(x.lastTickMs<oldestTick){oldestTick=x.lastTickMs;oldest=i;}}
    if(slot<0)slot=empty>=0?empty:oldest; block_->statuses[slot]=s; MemoryBarrier(); InterlockedIncrement((volatile LONG*)&block_->statusSeq);
}
void SharedControl::setRuntimePath(const wchar_t* p){ if(!block_)return; wcsncpy_s(block_->runtimePath,p?p:L"",_TRUNCATE); }
void SharedControl::requestUnload(bool value){ if(block_) InterlockedExchange((volatile LONG*)&block_->requestUnload,value?1:0); }
void SharedControl::requestHistoryReset(){ if(block_) InterlockedIncrement((volatile LONG*)&block_->historyResetGeneration); }
std::uint32_t SharedControl::historyResetGeneration() const { return block_ ? (std::uint32_t)InterlockedCompareExchange((volatile LONG*)&block_->historyResetGeneration,0,0) : 0u; }
void SharedControl::requestNeuralRetry(){ if(block_) InterlockedIncrement((volatile LONG*)&block_->neuralRetryGeneration); }
std::uint32_t SharedControl::neuralRetryGeneration() const { return block_ ? (std::uint32_t)InterlockedCompareExchange((volatile LONG*)&block_->neuralRetryGeneration,0,0) : 0u; }
}
#else
namespace udlss { SharedControl::~SharedControl(){} bool SharedControl::create(){return false;} bool SharedControl::open(){return false;} void SharedControl::close(){} Settings SharedControl::readSettings()const{return defaultSettings();} void SharedControl::writeSettings(const Settings&){} RuntimeStatus SharedControl::readStatus()const{return{};} void SharedControl::writeStatus(const RuntimeStatus&){} void SharedControl::setRuntimePath(const wchar_t*){} void SharedControl::requestUnload(bool){} void SharedControl::requestHistoryReset(){} std::uint32_t SharedControl::historyResetGeneration()const{return 0;} void SharedControl::requestNeuralRetry(){} std::uint32_t SharedControl::neuralRetryGeneration()const{return 0;} }
#endif
