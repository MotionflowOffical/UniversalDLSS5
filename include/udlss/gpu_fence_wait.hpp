#pragma once
#include <windows.h>
#include <cstdint>
namespace udlss {
// Auto-reset events are wakeups, never completion proof. An earlier timed-out
// registration may signal the same event during a later fence target.
template<class Fence> bool waitForGpuFenceValue(Fence* fence,HANDLE event,std::uint64_t target,DWORD timeoutMs){
 if(!fence||!event)return false;
 const auto deadline=GetTickCount64()+timeoutMs;
 for(;;){
  const auto completed=fence->GetCompletedValue();
  if(completed==UINT64_MAX)return false;
  if(completed>=target)return true;
  const auto now=GetTickCount64();if(now>=deadline)return false;
  ResetEvent(event);
  if(FAILED(fence->SetEventOnCompletion(target,event)))return false;
  const auto wake=WaitForSingleObject(event,static_cast<DWORD>(deadline-now));
  if(wake!=WAIT_OBJECT_0)return false;
 }
}
}
