#pragma once
#include <cstdint>

namespace udlss {

enum class NeuralExecutionApi : std::uint32_t {
    None = 0,
    D3D12 = 12,
};

inline constexpr NeuralExecutionApi neuralExecutionForSourceApi(std::uint32_t sourceApi) {
    switch(sourceApi) {
    case 9u: case 10u: case 11u: case 12u: case 0x1000u: case 0x1001u:
        return NeuralExecutionApi::D3D12;
    default:
        return NeuralExecutionApi::None;
    }
}

} // namespace udlss
