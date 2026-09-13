#include <cassert>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#ifndef UDLSS_SOURCE_DIR
#error UDLSS_SOURCE_DIR required
#endif
static std::string read(const std::filesystem::path&p){std::ifstream f(p);std::ostringstream s;s<<f.rdbuf();return s.str();}
int main(){
 const std::filesystem::path root=UDLSS_SOURCE_DIR;
 const auto h=read(root/"src/gpu/d3d11_pipeline.hpp");
 const auto c=read(root/"src/gpu/d3d11_pipeline.cpp");
 const auto native=read(root/"shaders/native_motion_convert.hlsl");
 const auto camera=read(root/"shaders/camera_motion.hlsl");
 const auto convert=read(root/"shaders/convert.hlsl");
 const auto post=read(root/"shaders/post.hlsl");
 assert(c.find("d3d11_resource_tracker.hpp")!=std::string::npos);
 assert(c.find("d3d11_camera_tracker.hpp")!=std::string::npos);
 assert(c.find("motion_route_policy.hpp")!=std::string::npos);
 assert(c.find("bestMotionCandidate")!=std::string::npos);
 assert(c.find("globalD3D11CameraTracker().snapshot")!=std::string::npos);
 assert(c.find("chooseMotionRoute")!=std::string::npos);
 // Auto must not silently drop to the crude block-matching HLSL flow.
 // The user can still opt into that route with SynthesizedOpticalFlow.
 assert(c.find("availability.hlsl=settings.motionSource==MotionSource::SynthesizedOpticalFlow")!=std::string::npos);
 assert(c.find("fallbackMotionRoute(settings.motionSource,nvofAvailable)")!=std::string::npos);
 assert(c.find("MotionRoute::TrackedNative")!=std::string::npos);
 assert(c.find("MotionRoute::CameraDepth")!=std::string::npos);
 assert(c.find("MotionRoute::Nvof")!=std::string::npos);
 assert(c.find("buildCurrentClipToPreviousClip")!=std::string::npos);
 assert(c.find("native_motion_convert.hlsl")!=std::string::npos);
 assert(c.find("camera_motion.hlsl")!=std::string::npos);
 assert(h.find("nativeMotionConvert_")!=std::string::npos);
 assert(h.find("cameraMotion_")!=std::string::npos);
 assert(h.find("cameraCb_")!=std::string::npos);
 assert(native.find("UnityUvPreviousToCurrent")!=std::string::npos || native.find("Encoding == 2")!=std::string::npos);
 assert(native.find("-raw")!=std::string::npos || native.find("-v")!=std::string::npos);
 assert(camera.find("CurrentClipToPreviousClip")!=std::string::npos);
 // Feature 18 is verified on stored SDR UNORM values. If the swapchain is
 // an sRGB format, the D3D11 SRV has already decoded to linear, so re-encode
 // before the neural proxy and decode neural output before compositing.
 assert(h.find("sourceSrgb")!=std::string::npos);
 assert(c.find("isSrgbFormat")!=std::string::npos);
 assert(convert.find("LinearToSrgb")!=std::string::npos);
 assert(post.find("SrgbToLinear")!=std::string::npos);
 // A synthetic protection mask is useful for local composition, but must not
 // be passed into the private NR contract as though it were a game mask.
 assert(c.find("explicitControlMask")!=std::string::npos);
 assert(c.find("fr.controlMask=explicitControlMask?nrControlMaskTex_.Get():nullptr")!=std::string::npos);
 // Feature-18 changes can be subtle; x10 matches the working diagnostic baseline.
 assert(post.find("abs(processedProxy-base)*10.0")!=std::string::npos);
 const auto refresh=c.find("Refresh the low-resolution luminance history every frame");
 const auto fallback=c.find("if(route==MotionRoute::Nvof||route==MotionRoute::Hlsl)");
 assert(refresh!=std::string::npos && fallback!=std::string::npos && refresh<fallback);
}
