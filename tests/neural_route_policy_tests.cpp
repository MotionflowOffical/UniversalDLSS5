#include "udlss/neural_route_policy.hpp"
#include <cassert>

using namespace udlss;

int main() {
    assert(neuralExecutionForSourceApi(11) == NeuralExecutionApi::D3D12);
    assert(neuralExecutionForSourceApi(12) == NeuralExecutionApi::D3D12);
    assert(neuralExecutionForSourceApi(0) == NeuralExecutionApi::None);
    assert(neuralExecutionForSourceApi(10) == NeuralExecutionApi::None);
    return 0;
}
