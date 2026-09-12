#pragma once
#include "udlss/camera_matrix_policy.hpp"
#include <d3d11.h>
#include <array>
#include <cstdint>

namespace udlss::gpu {

struct CameraMatrixSnapshot {
    std::array<float,16> currentViewProjection{};
    std::array<float,16> previousViewProjection{};
    bool currentValid{};
    bool previousValid{};
    std::uint32_t confidence{};
};

class D3D11CameraTracker {
public:
    void onVertexShaderCreated(const void* bytecode,SIZE_T length,ID3D11VertexShader* shader);
    void onVertexShaderBound(ID3D11DeviceContext* context,ID3D11VertexShader* shader);
    void onConstantBuffersBound(ID3D11DeviceContext* context,UINT start,UINT count,ID3D11Buffer* const* buffers);
    void onUpdateResource(ID3D11Resource* resource,const void* data);
    void onMap(ID3D11Resource* resource,void* data);
    void onUnmap(ID3D11Resource* resource);
    void onDraw(ID3D11DeviceContext* context);
    void finalizeFrame(ID3D11Device* device);
    CameraMatrixSnapshot snapshot(ID3D11Device* device) const;
    void reset();
};

D3D11CameraTracker& globalD3D11CameraTracker();
bool installD3D11CameraTrackingHooks(ID3D11Device* sampleDevice,ID3D11DeviceContext* sampleContext);

} // namespace udlss::gpu
