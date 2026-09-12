#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <chrono>
#include <cmath>
using Microsoft::WRL::ComPtr;
LRESULT CALLBACK Wnd(HWND h,UINT m,WPARAM w,LPARAM l){if(m==WM_DESTROY){PostQuitMessage(0);return 0;}return DefWindowProcW(h,m,w,l);}
int WINAPI wWinMain(HINSTANCE hi,HINSTANCE,LPWSTR,int){WNDCLASSW wc{};wc.lpfnWndProc=Wnd;wc.hInstance=hi;wc.lpszClassName=L"UDLSS5Test";wc.hCursor=LoadCursor(nullptr,IDC_ARROW);RegisterClassW(&wc);HWND wnd=CreateWindowW(wc.lpszClassName,L"UniversalDLSS5 DX11 Test",WS_OVERLAPPEDWINDOW|WS_VISIBLE,100,100,1280,720,nullptr,nullptr,hi,nullptr);DXGI_SWAP_CHAIN_DESC sd{};sd.BufferCount=2;sd.BufferDesc.Width=1280;sd.BufferDesc.Height=720;sd.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;sd.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;sd.OutputWindow=wnd;sd.SampleDesc.Count=1;sd.Windowed=TRUE;sd.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;ComPtr<IDXGISwapChain> sc;ComPtr<ID3D11Device>d;ComPtr<ID3D11DeviceContext>c;D3D_FEATURE_LEVEL fl;if(FAILED(D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&sd,&sc,&d,&fl,&c)))return 2;auto start=std::chrono::steady_clock::now();MSG msg{};while(msg.message!=WM_QUIT){while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}float t=std::chrono::duration<float>(std::chrono::steady_clock::now()-start).count();ComPtr<ID3D11Texture2D>bb;sc->GetBuffer(0,IID_PPV_ARGS(&bb));ComPtr<ID3D11RenderTargetView>rtv;d->CreateRenderTargetView(bb.Get(),nullptr,&rtv);float col[4]={0.12f+0.08f*std::sin(t),0.10f+0.06f*std::sin(t*.7f),0.18f+0.06f*std::cos(t*.9f),1};c->ClearRenderTargetView(rtv.Get(),col);sc->Present(1,0);}return 0;}
