#include <d3d12.h>
#include <wrl/client.h>
#include "../src/neural/external_host_wait.hpp"
#include "../src/neural/d3d12_gpu_timer.hpp"
#include "udlss/gpu_fence_wait.hpp"
#include <iostream>
#include <thread>
using Microsoft::WRL::ComPtr;
int main(){
    ComPtr<ID3D12Device> device;
    if(FAILED(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device))))return 77;
    bool ok=true;
    {
        ComPtr<ID3D12Fence> f;device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&f));
        auto e=CreateEventW(nullptr,FALSE,FALSE,nullptr);
        if(udlss::waitForGpuFenceValue(f.Get(),e,1,1)){std::cerr<<"Unsignaled fence accepted\n";ok=false;}
        f->Signal(1); // The old timed-out registration now leaves a stale wake.
        if(udlss::waitForGpuFenceValue(f.Get(),e,2,5)){std::cerr<<"Stale wake retired a newer fence target\n";ok=false;}
        f->Signal(2);if(!udlss::waitForGpuFenceValue(f.Get(),e,2,5)){std::cerr<<"Completed fence rejected\n";ok=false;}
        CloseHandle(e);
    }
    for(int scenario=0;scenario<4;++scenario){
        ComPtr<ID3D12Fence> fence;
        if(FAILED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence))))return 1;
        auto event=CreateEventW(nullptr,FALSE,FALSE,nullptr),done=CreateEventW(nullptr,FALSE,FALSE,nullptr);
        udlss::neural::hostipc::Shared shared{};shared.state=(LONG)udlss::neural::hostipc::State::Active;
        // A previous timed-out registration can wake this same auto-reset event.
        std::thread producer([&]{
            if(scenario==0){Sleep(5);SetEvent(event);Sleep(15);}
            else Sleep(scenario==3?300:20);
            fence->Signal(2);
            // Fence completion and CPU publication need not arrive together.
            if(scenario==1)Sleep(30);
            if(scenario!=2){InterlockedExchange(&shared.completedSeq,7);SetEvent(done);}
        });
        const auto budget=scenario==3?udlss::nrHostOutputWaitBudgetMs(udlss::FramePacingMode::Synchronized):100u;
        auto result=udlss::neural::waitForExactOutput(&shared,fence.Get(),event,nullptr,7,2,budget,done);
        producer.join();
        auto wanted=scenario==2?udlss::neural::OutputWaitResult::Timeout:udlss::neural::OutputWaitResult::Ready;
        if(result!=wanted){std::cerr<<"Exact output wait scenario "<<scenario<<" returned "<<(int)result<<" instead of "<<(int)wanted<<"\n";ok=false;}
        CloseHandle(event);CloseHandle(done);
    }
    // Exercise the real timestamp recording/readback path and its fence guard.
    ComPtr<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC q{};
    ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Fence> completed,notCompleted;
    if(FAILED(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)))||
       FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)))||
       FAILED(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)))||
       FAILED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&completed)))||
       FAILED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&notCompleted))))return 1;
    udlss::neural::D3D12GpuTimer timer;timer.initialize(device.Get(),queue.Get());
    timer.begin(list.Get());timer.end(list.Get());if(FAILED(list->Close()))return 1;
    ID3D12CommandList* lists[]={list.Get()};queue->ExecuteCommandLists(1,lists);queue->Signal(completed.Get(),1);timer.submitted(1);
    if(timer.consume(notCompleted.Get())){std::cerr<<"Timer read unresolved GPU queries\n";ok=false;}
    auto event=CreateEventW(nullptr,FALSE,FALSE,nullptr);completed->SetEventOnCompletion(1,event);
    if(WaitForSingleObject(event,3000)!=WAIT_OBJECT_0)return 1;
    auto elapsed=timer.consume(completed.Get());if(!elapsed||*elapsed<0){std::cerr<<"GPU timer did not resolve completed timestamps\n";ok=false;}
    CloseHandle(event);
    return ok?0:1;
}
