#include "udlss/compat_neural_policy.hpp"
#include <cassert>
using namespace udlss;
int main(){
    assert(selectCompatibilityNeuralLocation(false,RendererRoute::NativeD3D11,true)==NeuralExecutionLocation::ExternalHost);
    assert(selectCompatibilityNeuralLocation(false,RendererRoute::NativeD3D12,true)==NeuralExecutionLocation::ExternalHost);
    assert(selectCompatibilityNeuralLocation(false,RendererRoute::CompatD3D9Classic,true)==NeuralExecutionLocation::ExternalHost);
    assert(selectCompatibilityNeuralLocation(false,RendererRoute::CompatOpenGL,true)==NeuralExecutionLocation::ExternalHost);
    assert(selectCompatibilityNeuralLocation(true,RendererRoute::NativeD3D11,true)==NeuralExecutionLocation::InGame);
    assert(selectCompatibilityNeuralLocation(true,RendererRoute::ModernD3D12Recovery,true)==NeuralExecutionLocation::ExternalHost);
    assert(selectCompatibilityNeuralLocation(true,RendererRoute::CompatD3D10,true)==NeuralExecutionLocation::InGame);
    assert(selectCompatibilityNeuralLocation(false,RendererRoute::CompatD3D9Classic,false)==NeuralExecutionLocation::Unknown);
    return 0;
}
