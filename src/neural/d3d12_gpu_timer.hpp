#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <optional>

namespace udlss::neural {
// Two timestamp values only; no color, motion or depth pixels leave the GPU.
class D3D12GpuTimer {
public:
    void initialize(ID3D12Device* device,ID3D12CommandQueue* queue){
        queries_.Reset();readback_.Reset();pending_=0;recording_=false;frequency_=0;
        if(!device||!queue||FAILED(queue->GetTimestampFrequency(&frequency_))||!frequency_)return;
        D3D12_QUERY_HEAP_DESC q{};q.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP;q.Count=2;
        if(FAILED(device->CreateQueryHeap(&q,IID_PPV_ARGS(&queries_))))return;
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK; // UDLSS_TIMESTAMP_QUERY_READBACK
        D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=2*sizeof(UINT64);
        desc.Height=1;desc.DepthOrArraySize=desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if(FAILED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback_))))queries_.Reset();
    }
    std::optional<float> consume(ID3D12Fence* fence){
        if(!pending_||!fence||!readback_)return{};
        const auto completed=fence->GetCompletedValue();
        if(completed==UINT64_MAX||completed<pending_)return{};
        UINT64* ticks{};D3D12_RANGE range{0,2*sizeof(UINT64)};
        if(FAILED(readback_->Map(0,&range,reinterpret_cast<void**>(&ticks))))return{};
        const auto begin=ticks[0],end=ticks[1];D3D12_RANGE written{0,0};readback_->Unmap(0,&written);pending_=0;
        if(end<begin||!frequency_)return{};
        return static_cast<float>(double(end-begin)*1000.0/double(frequency_));
    }
    void begin(ID3D12GraphicsCommandList* list){
        recording_=list&&queries_&&readback_&&!pending_;
        if(recording_)list->EndQuery(queries_.Get(),D3D12_QUERY_TYPE_TIMESTAMP,0);
    }
    void end(ID3D12GraphicsCommandList* list){
        if(!recording_)return;
        list->EndQuery(queries_.Get(),D3D12_QUERY_TYPE_TIMESTAMP,1);
        list->ResolveQueryData(queries_.Get(),D3D12_QUERY_TYPE_TIMESTAMP,0,2,readback_.Get(),0);
    }
    void submitted(UINT64 value){if(recording_)pending_=value;recording_=false;}
private:
    Microsoft::WRL::ComPtr<ID3D12QueryHeap> queries_;
    Microsoft::WRL::ComPtr<ID3D12Resource> readback_;
    UINT64 frequency_{},pending_{};
    bool recording_{};
};
}
