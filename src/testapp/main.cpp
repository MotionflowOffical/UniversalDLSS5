#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#if defined(_M_IX86)
#pragma comment(linker,"/EXPORT:UdlssGameGuides_GetFrameV2=_UdlssGameGuides_GetFrameV2")
#endif
#include <shellapi.h>
#include <d3d11.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include "udlss/game_guides_api.hpp"
#include <filesystem>
#include <chrono>
#include <vector>
#include <fstream>
using Microsoft::WRL::ComPtr;
namespace {
ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
ComPtr<ID3D11Texture2D> sceneDepth,sceneMotion;
UINT frameIndex{},renderWidth{},renderHeight{};
LRESULT CALLBACK Wnd(HWND h,UINT m,WPARAM w,LPARAM l){if(m==WM_DESTROY){PostQuitMessage(0);return 0;}return DefWindowProcW(h,m,w,l);}
bool compile(const std::filesystem::path&p,const char*entry,const char*target,ComPtr<ID3DBlob>&b){ComPtr<ID3DBlob>e;return SUCCEEDED(D3DCompileFromFile(p.c_str(),nullptr,D3D_COMPILE_STANDARD_FILE_INCLUDE,entry,target,D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&b,&e));}
}
extern "C" __declspec(dllexport) bool UdlssGameGuides_GetFrameV2(void*d,void*,void*,udlss::GameGuideFrameV2*out){
 if(!out||d!=device.Get()||!sceneDepth||!sceneMotion)return false;
 *out={};auto&r=out->resources;r.flags=udlss::GameGuide_DepthConventionKnown|udlss::GameGuide_MotionPixelCurrentToPrevious;
 if(frameIndex%240==0)r.flags|=udlss::GameGuide_CameraCut;
 r.depthTexture=sceneDepth.Get();sceneDepth->AddRef();r.motionTexture=sceneMotion.Get();sceneMotion->AddRef();r.depthFormat=DXGI_FORMAT_R32_FLOAT;r.motionFormat=DXGI_FORMAT_R32G32_FLOAT;
 wcscpy_s(r.providerName,L"Deterministic 3D test scene");auto&t=out->temporal;t.flags=udlss::Temporal_SourceFrameKnown|udlss::Temporal_JitterKnown;t.sourceFrame=frameIndex;t.viewport=1;t.renderWidth=t.outputWidth=renderWidth;t.renderHeight=t.outputHeight=renderHeight;if(frameIndex%240==0)t.flags|=udlss::Temporal_CameraCut;return true;
}
int WINAPI wWinMain(HINSTANCE hi,HINSTANCE,LPWSTR,int show){
 if(!GetProcAddress(GetModuleHandleW(nullptr),udlss::kGameGuidesExportV2))return 10;
 int argc=0;auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);UINT frameLimit=0,pressureMiB=0;bool hdr=false,resize=false,vsync=true;std::filesystem::path report;
 for(int i=1;i<argc;i++){std::wstring a=argv[i];if(a==L"--hdr")hdr=true;else if(a==L"--resize")resize=true;else if(a==L"--no-vsync")vsync=false;else if(a==L"--frames"&&i+1<argc)frameLimit=std::min(36000u,(UINT)_wtoi(argv[++i]));else if(a==L"--pressure-mb"&&i+1<argc)pressureMiB=std::min(4096u,(UINT)_wtoi(argv[++i]));else if(a==L"--report"&&i+1<argc)report=argv[++i];}LocalFree(argv);
 WNDCLASSW wc{};wc.lpfnWndProc=Wnd;wc.hInstance=hi;wc.lpszClassName=L"UDLSS5Test";wc.hCursor=LoadCursor(nullptr,IDC_ARROW);RegisterClassW(&wc);HWND wnd=CreateWindowW(wc.lpszClassName,L"UniversalDLSS5 deterministic 3D test",WS_OVERLAPPEDWINDOW|(show==SW_HIDE?0:WS_VISIBLE),100,100,1280,720,nullptr,nullptr,hi,nullptr);
 DXGI_SWAP_CHAIN_DESC sd{};sd.BufferCount=2;sd.BufferDesc.Width=1280;sd.BufferDesc.Height=720;sd.BufferDesc.Format=hdr?DXGI_FORMAT_R16G16B16A16_FLOAT:DXGI_FORMAT_R8G8B8A8_UNORM;sd.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;sd.OutputWindow=wnd;sd.SampleDesc.Count=1;sd.Windowed=TRUE;sd.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;ComPtr<IDXGISwapChain> swap;
 if(FAILED(D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&sd,&swap,&device,nullptr,&context)))return 2;
 if(hdr){ComPtr<IDXGISwapChain3> s3;if(FAILED(swap.As(&s3))||FAILED(s3->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709)))return 3;}
 wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,_countof(exe));auto shader=std::filesystem::path(exe).parent_path()/L"shaders/test_scene.hlsl";ComPtr<ID3DBlob>vb,pb;ComPtr<ID3D11VertexShader>vs;ComPtr<ID3D11PixelShader>ps;
 if(!compile(shader,"VSMain","vs_5_0",vb)||!compile(shader,"PSMain","ps_5_0",pb)||FAILED(device->CreateVertexShader(vb->GetBufferPointer(),vb->GetBufferSize(),nullptr,&vs))||FAILED(device->CreatePixelShader(pb->GetBufferPointer(),pb->GetBufferSize(),nullptr,&ps)))return 4;
 D3D11_BUFFER_DESC cbd{};cbd.ByteWidth=16;cbd.Usage=D3D11_USAGE_DEFAULT;cbd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;ComPtr<ID3D11Buffer>cb;if(FAILED(device->CreateBuffer(&cbd,nullptr,&cb)))return 5;
 ComPtr<ID3D11RenderTargetView> colorRtv,depthRtv,motionRtv;
 auto surfaces=[&](UINT w,UINT h){context->ClearState();colorRtv.Reset();depthRtv.Reset();motionRtv.Reset();sceneDepth.Reset();sceneMotion.Reset();ComPtr<ID3D11Texture2D>back;if(FAILED(swap->GetBuffer(0,IID_PPV_ARGS(&back)))||FAILED(device->CreateRenderTargetView(back.Get(),nullptr,&colorRtv)))return false;
  auto make=[&](DXGI_FORMAT f,ComPtr<ID3D11Texture2D>&t,ComPtr<ID3D11RenderTargetView>&v){D3D11_TEXTURE2D_DESC d{};d.Width=w;d.Height=h;d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;d.Format=f;d.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;return SUCCEEDED(device->CreateTexture2D(&d,nullptr,&t))&&SUCCEEDED(device->CreateRenderTargetView(t.Get(),nullptr,&v));};renderWidth=w;renderHeight=h;return make(DXGI_FORMAT_R32_FLOAT,sceneDepth,depthRtv)&&make(DXGI_FORMAT_R32G32_FLOAT,sceneMotion,motionRtv);
 };if(!surfaces(1280,720))return 6;
 // Optional bounded pressure exercises allocation order; it is never enabled
 // implicitly and the buffers are released when this test process exits.
 std::vector<ComPtr<ID3D11Buffer>> pressure;for(UINT mb=0;mb<pressureMiB;mb+=64){D3D11_BUFFER_DESC d{};d.ByteWidth=64*1024*1024;d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_UNORDERED_ACCESS;d.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;ComPtr<ID3D11Buffer>b;if(FAILED(device->CreateBuffer(&d,nullptr,&b)))return 7;D3D11_UNORDERED_ACCESS_VIEW_DESC u{};u.Format=DXGI_FORMAT_R32_TYPELESS;u.ViewDimension=D3D11_UAV_DIMENSION_BUFFER;u.Buffer.NumElements=d.ByteWidth/4;u.Buffer.Flags=D3D11_BUFFER_UAV_FLAG_RAW;ComPtr<ID3D11UnorderedAccessView>v;if(FAILED(device->CreateUnorderedAccessView(b.Get(),&u,&v)))return 7;UINT fill[4]={1,2,3,4};context->ClearUnorderedAccessViewUint(v.Get(),fill);pressure.push_back(b);}
 auto start=std::chrono::steady_clock::now();UINT presents=0,resizes=0;MSG msg{};int result=0;
 while(msg.message!=WM_QUIT&&(!frameLimit||frameIndex<frameLimit)){
  while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}if(msg.message==WM_QUIT)break;
  if(resize&&frameIndex&&frameIndex%180==0){context->ClearState();colorRtv.Reset();UINT w=renderWidth==1280?960:1280,h=w==1280?720:540;if(FAILED(swap->ResizeBuffers(0,w,h,DXGI_FORMAT_UNKNOWN,0))||!surfaces(w,h)){result=8;break;}resizes++;}
  UINT values[4]={renderWidth,renderHeight,frameIndex,hdr?1u:0u};context->UpdateSubresource(cb.Get(),0,nullptr,values,0,0);auto constants=cb.Get();context->PSSetConstantBuffers(0,1,&constants);context->VSSetShader(vs.Get(),nullptr,0);context->PSSetShader(ps.Get(),nullptr,0);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);ID3D11RenderTargetView* targets[]={colorRtv.Get(),depthRtv.Get(),motionRtv.Get()};context->OMSetRenderTargets(3,targets,nullptr);D3D11_VIEWPORT vp{0,0,(float)renderWidth,(float)renderHeight,0,1};context->RSSetViewports(1,&vp);context->Draw(3,0);context->OMSetRenderTargets(0,nullptr,nullptr);
  if(FAILED(swap->Present(vsync?1:0,0))){result=9;break;}presents++;frameIndex++;
 }
 double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();if(!report.empty()){std::ofstream out(report);out<<"{\"frames\":"<<presents<<",\"resizes\":"<<resizes<<",\"cameraCuts\":"<<(presents+239)/240<<",\"hdr\":"<<(hdr?"true":"false")<<",\"pressureMiB\":"<<pressure.size()*64<<",\"seconds\":"<<seconds<<",\"result\":"<<result<<"}\n";}
 DestroyWindow(wnd);return result;
}
