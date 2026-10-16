#pragma once
#include "external_host_protocol.hpp"
#include "udlss/external_host_policy.hpp"

namespace udlss::neural {
enum class OutputWaitResult { Ready, Timeout, HostExited, HostError, WaitFailed, SequenceMismatch };

template<class Fence>
OutputWaitResult waitForExactOutput(hostipc::Shared* shared,Fence* fence,HANDLE fenceEvent,HANDLE hostProcess,LONG requestedSeq,UINT64 outputValue,DWORD timeoutMs,HANDLE doneEvent) {
    if(!shared||!fence||!fenceEvent||!doneEvent||!outputValue)return OutputWaitResult::WaitFailed;
    const ULONGLONG deadline=GetTickCount64()+timeoutMs;
    // A timed-out fence registration can signal this event during a later
    // request. Events are wakeups only; sequence AND GPU completion are proof.
    ResetEvent(fenceEvent);
    if(FAILED(fence->SetEventOnCompletion(outputValue,fenceEvent)))return OutputWaitResult::WaitFailed;
    HANDLE waits[]={fenceEvent,doneEvent,hostProcess};
    const DWORD count=hostProcess?3u:2u;
    for(;;){
        const LONG completed=InterlockedCompareExchange(&shared->completedSeq,0,0);
        const auto gpuCompleted=fence->GetCompletedValue();
        if(gpuCompleted==UINT64_MAX)return OutputWaitResult::WaitFailed;
        if(nrHostOutputReady(completed,requestedSeq,gpuCompleted,outputValue))return OutputWaitResult::Ready;
        if(completed>requestedSeq)return OutputWaitResult::SequenceMismatch;
        if(hostProcess&&WaitForSingleObject(hostProcess,0)==WAIT_OBJECT_0)return OutputWaitResult::HostExited;
        const auto state=static_cast<hostipc::State>(InterlockedCompareExchange(&shared->state,0,0));
        if(state==hostipc::State::Error||state==hostipc::State::Stopping)return OutputWaitResult::HostError;
        const auto now=GetTickCount64();
        if(now>=deadline)return OutputWaitResult::Timeout;
        const DWORD w=WaitForMultipleObjects(count,waits,FALSE,static_cast<DWORD>(deadline-now));
        if(w==WAIT_FAILED)return OutputWaitResult::WaitFailed;
        if(hostProcess&&w==WAIT_OBJECT_0+2)return OutputWaitResult::HostExited;
        // The host may publish completedSeq after the GPU signals its fence.
        // Done wakes that case without spinning or extending the total budget.
    }
}

}
