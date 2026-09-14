#include "udlss/neural_route_policy.hpp"
#include <cassert>

using namespace udlss;

int main() {
    assert(neuralExecutionForSourceApi(11) == NeuralExecutionApi::D3D12);
    assert(neuralExecutionForSourceApi(12) == NeuralExecutionApi::D3D12);
    assert(neuralExecutionForSourceApi(0) == NeuralExecutionApi::None);
    assert(neuralExecutionForSourceApi(10) == NeuralExecutionApi::D3D12);
    assert(neuralExecutionForSourceApi(9) == NeuralExecutionApi::D3D12);
    assert(neuralExecutionForSourceApi(0x1000u) == NeuralExecutionApi::D3D12);
    assert(neuralExecutionForSourceApi(0x1001u) == NeuralExecutionApi::D3D12);
    return 0;
}
