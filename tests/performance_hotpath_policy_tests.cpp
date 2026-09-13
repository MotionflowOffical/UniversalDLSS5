#include "udlss/motion_route_policy.hpp"
#include "udlss/d3d11_surface_policy.hpp"
#include "udlss/neural_scheduler_policy.hpp"
#include "udlss/settings.hpp"
#include <iostream>
using namespace udlss;

int main(){
    if(!shouldMaintainHlslFlowHistory(MotionSource::SynthesizedOpticalFlow)){
        std::cerr << "explicit HLSL optical flow must maintain its low-resolution history\n";
        return 1;
    }
    if(shouldMaintainHlslFlowHistory(MotionSource::Auto) ||
       shouldMaintainHlslFlowHistory(MotionSource::Zero)){
        std::cerr << "non-HLSL motion modes must not pay for low-resolution HLSL history\n";
        return 2;
    }
    if(shouldCacheCurrentNeuralOutput(FramePacingMode::Synchronized)){
        std::cerr << "synchronized mode must not copy the current neural result into the stale-output cache\n";
        return 3;
    }
    if(!shouldCacheCurrentNeuralOutput(FramePacingMode::Adaptive) ||
       !shouldCacheCurrentNeuralOutput(FramePacingMode::Asynchronous)){
        std::cerr << "non-blocking pacing modes require the completed-output cache\n";
        return 4;
    }
    if(needsNeuralRefinementScratch(1) || !needsNeuralRefinementScratch(2)){
        std::cerr << "refinement scratch must only exist for multipass NR\n";
        return 5;
    }
    if(needsNeuralControlMask(true, false) || needsNeuralControlMask(false, true) ||
       !needsNeuralControlMask(true, true)){
        std::cerr << "control-mask resources must only exist when both enabled and supplied\n";
        return 6;
    }
    if(needsAsyncNeuralCache(FramePacingMode::Synchronized) ||
       !needsAsyncNeuralCache(FramePacingMode::Adaptive) ||
       !needsAsyncNeuralCache(FramePacingMode::Asynchronous)){
        std::cerr << "neural cache allocation must follow pacing mode\n";
        return 7;
    }
    if(allowDirectD3D11SurfaceReuse(false) || !allowDirectD3D11SurfaceReuse(true)){
        std::cerr << "direct D3D11 surface reuse must be limited to injector-owned D3D12On12 staging resources\n";
        return 8;
    }
    return 0;
}
