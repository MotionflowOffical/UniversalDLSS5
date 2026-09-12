#pragma once
#include <cstdint>

namespace udlss {

enum class NeuralExecutionApi : std::uint32_t {
    None = 0,
    D3D12 = 12,
};

inline constexpr NeuralExecutionApi neuralExecutionForSourceApi(std::uint32_t sourceApi) {
    return (sourceApi == 11u || sourceApi == 12u) ? NeuralExecutionApi::D3D12 : NeuralExecutionApi::None;
}

} // namespace udlss
