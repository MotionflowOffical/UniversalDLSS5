#include "compat_dispatch.hpp"
#include "../gpu/swapchain_color.hpp"
#include <cwchar>
namespace udlss::compat {
bool CompatDispatch::process(IRendererFrontend& frontend,const Settings& settings,const std::wstring& moduleDir,const std::wstring& runtime,RuntimeStatus& status){
    const auto route=frontend.route();
    if(!routeNeedsCompatDispatch(route)||!isCompatibilityRendererRoute(route)){
        status.failureStage=PipelineStage::SourceApiUnsupported;
        wcscpy_s(status.message,L"Compatibility dispatcher rejected a native or unsupported renderer route");
        return false;
    }
    auto* device=frontend.canonicalDevice();auto* context=frontend.canonicalContext();
    if(!device||!context){status.failureStage=PipelineStage::GuideResourcesFailed;wcscpy_s(status.message,L"Compatibility frontend has no canonical D3D11 device/context");return false;}
    if(device_.Get()!=device||route_!=route){reset();device_=device;route_=route;}
    CompatFrame frame{};
    status.rendererRoute=route;status.compatInterop=frontend.interop();status.gameArchitectureBits=sizeof(void*)*8u;
    const auto displayName=frontend.name();wcsncpy_s(status.compatFrontendName,displayName.data(),_TRUNCATE);const auto interopName=compatInteropName(frontend.interop());wcsncpy_s(status.interopName,interopName.data(),_TRUNCATE);
    if(!frontend.beginFrame(frame,status)||!frame.color||!frame.width||!frame.height){if(status.failureStage==PipelineStage::None)status.failureStage=PipelineStage::GuideResourcesFailed;return false;}
    status.width=frame.width;status.height=frame.height;status.colorPathWidth=frame.width;status.colorPathHeight=frame.height;
    if(!pipeline_){pipeline_=std::make_unique<gpu::D3D11Pipeline>();if(!pipeline_->initialize(device,context,moduleDir,status)){pipeline_.reset();frontend.endFrame(frame,status);return false;}}
    gpu::SwapchainColorContext colorContext{};colorContext.format=frame.format;colorContext.colorSpace=DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;colorContext.decision={false,true,frame.colorEncoding};colorContext.colorSpaceInferred=true;
    const bool processed=pipeline_->process(frame.color,settings,runtime,status,nullptr,&colorContext);
    const bool copiedBack=frontend.endFrame(frame,status);
    ++compatFrames_;legacyCopies_+=frame.legacyCopies;if(status.neuralActive&&status.neuralLocation==NeuralExecutionLocation::ExternalHost)++externalHostFrames_;
    status.compatFrames=compatFrames_;status.legacyCopies=legacyCopies_;status.externalHostFrames=externalHostFrames_;
    // Compatibility accounting is intentionally disjoint from the native counters.
    status.nativeFrames=0;
    return processed&&copiedBack;
}
void CompatDispatch::reset(){if(pipeline_)pipeline_->reset();pipeline_.reset();device_.Reset();route_=RendererRoute::Unknown;}
}
