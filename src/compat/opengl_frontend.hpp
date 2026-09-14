#pragma once
#include "renderer_frontend.hpp"
#include "canonical_surface.hpp"
#include <windows.h>
#include <gl/GL.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

namespace udlss::compat {
class OpenGLFrontend final : public IRendererFrontend {
public:
    bool initialize(HDC dc,HGLRC context,RuntimeStatus& status);
    RendererRoute route() const noexcept override { return RendererRoute::CompatOpenGL; }
    CompatInterop interop() const noexcept override { return interop_; }
    std::wstring_view name() const noexcept override { return interop_==CompatInterop::WglNvDxInterop?L"OpenGL WGL_NV_DX_interop2 frontend":L"OpenGL PBO compatibility frontend"; }
    ID3D11Device* canonicalDevice() const noexcept override { return d11_.Get(); }
    ID3D11DeviceContext* canonicalContext() const noexcept override { return d11Context_.Get(); }
    bool beginFrame(CompatFrame& frame,RuntimeStatus& status) override;
    bool endFrame(const CompatFrame& frame,RuntimeStatus& status) override;
    void reset() override;
private:
    bool createD3D11Device(RuntimeStatus& status);
    bool ensureResources(std::uint32_t width,std::uint32_t height,RuntimeStatus& status);
    bool resolveGlFunctions();
    bool tryCreateNvInterop(RuntimeStatus& status);
    bool createPboResources(RuntimeStatus& status);
    bool beginNv(CompatFrame& frame,RuntimeStatus& status);
    bool endNv(RuntimeStatus& status);
    bool beginPbo(CompatFrame& frame,RuntimeStatus& status);
    bool endPbo(RuntimeStatus& status);
    void destroyGlResources();

    HDC dc_{};HGLRC context_{};
    Microsoft::WRL::ComPtr<ID3D11Device> d11_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> d11Context_;
    CanonicalSurface canonical_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> readback_;
    CompatInterop interop_{CompatInterop::None};
    std::uint32_t width_{},height_{};
    std::uint64_t generation_{};
    GLuint glTexture_{};GLuint fbo_{};GLuint packPbo_{};
    HANDLE dxInteropDevice_{};HANDLE dxInteropObject_{};

    using WglDxOpenDevice=HANDLE (WINAPI*)(void*);
    using WglDxCloseDevice=BOOL (WINAPI*)(HANDLE);
    using WglDxRegisterObject=HANDLE (WINAPI*)(HANDLE,void*,GLuint,GLenum,GLenum);
    using WglDxUnregisterObject=BOOL (WINAPI*)(HANDLE,HANDLE);
    using WglDxLockObjects=BOOL (WINAPI*)(HANDLE,GLint,HANDLE*);
    using WglDxUnlockObjects=BOOL (WINAPI*)(HANDLE,GLint,HANDLE*);
    WglDxOpenDevice wglDXOpenDeviceNV_{};WglDxCloseDevice wglDXCloseDeviceNV_{};WglDxRegisterObject wglDXRegisterObjectNV_{};WglDxUnregisterObject wglDXUnregisterObjectNV_{};WglDxLockObjects wglDXLockObjectsNV_{};WglDxUnlockObjects wglDXUnlockObjectsNV_{};

    using GlGenFramebuffers=void (APIENTRY*)(GLsizei,GLuint*);using GlDeleteFramebuffers=void (APIENTRY*)(GLsizei,const GLuint*);using GlBindFramebuffer=void (APIENTRY*)(GLenum,GLuint);using GlFramebufferTexture2D=void (APIENTRY*)(GLenum,GLenum,GLenum,GLuint,GLint);using GlBlitFramebuffer=void (APIENTRY*)(GLint,GLint,GLint,GLint,GLint,GLint,GLint,GLint,GLbitfield,GLenum);
    GlGenFramebuffers glGenFramebuffers_{};GlDeleteFramebuffers glDeleteFramebuffers_{};GlBindFramebuffer glBindFramebuffer_{};GlFramebufferTexture2D glFramebufferTexture2D_{};GlBlitFramebuffer glBlitFramebuffer_{};
    using GlGenBuffers=void (APIENTRY*)(GLsizei,GLuint*);using GlDeleteBuffers=void (APIENTRY*)(GLsizei,const GLuint*);using GlBindBuffer=void (APIENTRY*)(GLenum,GLuint);using GlBufferData=void (APIENTRY*)(GLenum,ptrdiff_t,const void*,GLenum);using GlMapBufferRange=void* (APIENTRY*)(GLenum,ptrdiff_t,ptrdiff_t,GLbitfield);using GlUnmapBuffer=GLboolean (APIENTRY*)(GLenum);
    GlGenBuffers glGenBuffers_{};GlDeleteBuffers glDeleteBuffers_{};GlBindBuffer glBindBuffer_{};GlBufferData glBufferData_{};GlMapBufferRange glMapBufferRange_{};GlUnmapBuffer glUnmapBuffer_{};
};
}
