# UniversalDLSS5 v0.4.0 — Universal Renderer Compatibility Design

Date: 2026-09-14
Status: Proposed implementation specification
Target release: 0.4.0

## 1. Purpose

UniversalDLSS5 v0.4.0 expands renderer compatibility without placing legacy translation overhead in front of games that already use the proven v0.3.2 D3D11/D3D12 paths.

The release adds API-level compatibility frontends for D3D9, D3D10, OpenGL and Vulkan, a first-class x86-to-x64 Feature 18 route for 32-bit games, and a modern D3D12 recovery route for games such as Half Sword that expose a modern renderer but currently fall into passthrough because queue/swapchain ownership cannot be proven.

The architecture is additive. The existing native D3D11 and D3D12 pipelines remain the preferred routes and must not gain compatibility copies, legacy staging, external-host traffic or new per-frame route-detection cost merely because v0.4.0 supports older APIs.

## 2. User-visible goals

1. Keep currently supported x64 D3D11/D3D12 games on their existing low-latency v0.3.2 hot paths.
2. Support 32-bit games with real signed Feature 18 by executing Feature 18 in the existing 64-bit NRHost while keeping inter-process image transport GPU-side whenever the source API permits it.
3. Add D3D10, D3D9Ex, classic D3D9, OpenGL and Vulkan presentation frontends.
4. Add a modern D3D12 recovery path for valid D3D12 games that fail current queue/swapchain proof, with Half Sword as the reference modern-failure case.
5. Make detach safe from Processing, Passthrough, partial initialization and compatibility states. If complete DLL unload cannot be proven safe, leave an inert forwarding bridge resident rather than freezing or crashing the game.
6. Expose the selected renderer route, interop method and execution location in diagnostics.
7. Do not add a screen-capture/desktop-capture fallback.
8. Do not lower the full-resolution Feature 18 color input/output to obtain compatibility or performance.

## 3. Non-goals

- No desktop/screen-capture overlay fallback.
- No per-engine renderer implementation requirement. Engine-specific adapters may improve guide quality later, but API compatibility must work independently of engine identity.
- No requirement to make anti-cheat/protected processes injectable.
- No attempt to provide software-renderer support in v0.4.0.
- No intentional image-quality reduction of the Feature 18 color path.
- No mandatory external-host route for x64 games that already run Feature 18 in-process successfully.

## 4. Architectural invariants

### 4.1 Native-path isolation

For `NativeD3D11` and `NativeD3D12` frames:

- compatibility frontends do not execute;
- legacy staging does not execute;
- compatibility resource copies do not execute;
- external NRHost is not started unless the existing neural-route policy already requires it;
- route classification is cached after renderer establishment and not recomputed through a large decision tree every frame.

This is enforced by policy tests and diagnostic counters.

### 4.2 Full-resolution neural color path

Compatibility frontends must preserve the source frame's full color resolution into and out of Feature 18. Lower-resolution work is permitted only for auxiliary processing such as optical flow, guide analysis or diagnostics.

### 4.3 Safe failure

When a compatibility route cannot prove resource lifetime, queue ownership or synchronization correctness, it must fail to passthrough instead of guessing. Detach from that state must still be safe.

### 4.4 Engine independence

Renderer support is selected primarily by graphics API and proven resource behavior, not by game executable or engine name. Optional engine guide adapters are enhancements only.

## 5. Core route model

Add a route enum separate from `GraphicsApi`:

```cpp
enum class RendererRoute : std::uint32_t {
    Unknown = 0,
    NativeD3D11,
    NativeD3D12,
    ModernD3D12Recovery,
    CompatD3D10,
    CompatD3D9Ex,
    CompatD3D9Classic,
    CompatOpenGL,
    CompatVulkan,
    Unsupported
};
```

Expand `GraphicsApi` to represent:

```cpp
Unknown, D3D9, D3D10, D3D11, D3D12, OpenGL, Vulkan
```

`GraphicsApi` answers what the game renders with. `RendererRoute` answers how UniversalDLSS5 is processing it.

The bridge publishes both through `RuntimeStatus`.

## 6. Route selection

Route selection is event-driven and cached.

### 6.1 Native modern renderer

- D3D11 swapchain/device proven -> `NativeD3D11`.
- D3D12 swapchain + trusted DIRECT queue proven -> `NativeD3D12`.

These preserve the v0.3.2 pipeline behavior.

### 6.2 Modern D3D12 recovery

A D3D12 game that has a valid swapchain/device but lacks sufficient queue proof enters `ModernD3D12Recovery`, not a legacy compatibility route.

Recovery gathers stronger queue evidence using:

- queue supplied during DXGI swapchain creation;
- legacy `ID3D12GraphicsCommandList::ResourceBarrier` backbuffer touches;
- D3D12 Enhanced Barrier `ID3D12GraphicsCommandList7::Barrier` backbuffer touches;
- `ExecuteCommandLists` correlation;
- repeated backbuffer/queue association across presents;
- direct-queue type validation;
- adapter/device identity consistency.

A confidence/proof policy promotes recovery to the normal D3D12 pipeline once ownership is established. It must not guess a queue from generic activity alone.

Half Sword is the reference case for this path.

### 6.3 Legacy/alternate APIs

Only processes actually rendering through D3D10, D3D9, OpenGL or Vulkan initialize the corresponding frontend.

No compatibility frontend is initialized on a proven D3D11/D3D12 route.

## 7. Compatibility frontend interface

Add a renderer frontend abstraction under `src/compat/`:

```text
src/compat/
    renderer_route.cpp/.hpp
    compat_dispatch.cpp/.hpp
    canonical_surface.cpp/.hpp
    d3d10_frontend.cpp/.hpp
    d3d9_frontend.cpp/.hpp
    opengl_frontend.cpp/.hpp
    vulkan_frontend.cpp/.hpp
    legacy_copy_ring.cpp/.hpp
```

Conceptual interface:

```cpp
struct CompatFrame {
    UINT width;
    UINT height;
    DXGI_FORMAT colorFormat;
    ID3D11Texture2D* canonicalInput;
    ID3D11Texture2D* canonicalOutput;
    // optional native depth/motion resources or metadata
};

class IRendererFrontend {
public:
    virtual RendererRoute route() const = 0;
    virtual bool beginFrame(CompatFrame&, RuntimeStatus&) = 0;
    virtual bool presentOutput(const CompatFrame&, RuntimeStatus&) = 0;
    virtual void onResizeOrRecreate() = 0;
    virtual void shutdown() = 0;
    virtual ~IRendererFrontend() = default;
};
```

The canonical compatibility representation is full-resolution D3D11 GPU textures because the existing preprocessing, guide generation and external-host bridge already consume D3D11 resources.

This interface is not placed in front of native D3D11/D3D12 processing; it is instantiated only for compatibility routes.

## 8. 32-bit game support and Feature 18

### 8.1 Constraint

A 32-bit process cannot directly load the project's x64 NGX/Feature 18 runtime. Therefore v0.4.0 defines the supported 32-bit neural route as:

```text
32-bit game
  -> UniversalDLSS5.Bridge32.dll
  -> full-resolution GPU shared resources
  -> UniversalDLSS5.NRHost.exe (x64)
  -> signed Feature 18 on D3D12
  -> GPU shared output
  -> Bridge32
  -> source renderer
```

This is real Feature 18 evaluation; only the execution process differs.

### 8.2 External-host protocol extension

Reuse the current D3D11 NT-handle + shared-fence protocol. Extend the protocol with:

- source API;
- renderer route;
- bridge architecture (x86/x64);
- canonical color format and colorspace metadata;
- optional-guide presence flags;
- frontend/resource generation;
- explicit route capability flags.

Do not add CPU frame transport to the normal x86 D3D11/D3D10/D3D9Ex/OpenGL/Vulkan GPU-interoperable paths.

### 8.3 Host lifetime

NRHost is started only when the selected neural route requires it. Native x64 in-process Feature 18 behavior remains unchanged.

## 9. D3D10 frontend

D3D10 is already DXGI-presented, so the existing DXGI hook family can detect Present/Resize but currently rejects the API.

Implementation:

1. Query D3D10 device from the swapchain.
2. Allocate a D3D10 shared full-resolution color texture on the same adapter.
3. Copy the game backbuffer into the shared texture using the game D3D10 device.
4. Open that shared resource on the compatibility D3D11 device.
5. Run the existing D3D11 preprocessing/neural route.
6. Make the processed output visible to D3D10 through a paired shared output resource.
7. Copy/blit the result back to the game backbuffer before original Present.

Synchronize through supported DXGI shared-resource synchronization. On resource/swapchain recreation, invalidate and rebuild only this frontend's resources.

## 10. D3D9 frontend

Hook D3D9 creation/device presentation separately from DXGI.

Required entry points include D3D9/D3D9Ex device creation plus device `Present`, `PresentEx`, `Reset` and `ResetEx` lifecycle operations.

### 10.1 D3D9Ex

Prefer GPU interop:

- create compatible shareable D3D9Ex render textures;
- copy/resolve the game render target/backbuffer into an owned shareable texture;
- open via the D3D11 compatibility device;
- process full-resolution color;
- copy back through a shareable output texture;
- present through the game device.

### 10.2 Classic D3D9

Classic D3D9 cannot rely on modern cross-API NT shared resources. Use a dedicated full-resolution asynchronous compatibility ring.

The initial correctness path may require a GPU->system-memory->D3D11 upload and reverse output transfer when no safe GPU interop mechanism is available. It must be explicitly labeled `CompatD3D9Classic` and must never be selected for modern APIs.

The ring must be multi-buffered so unavoidable transfer latency does not serialize unrelated native routes. No color-resolution reduction is permitted.

Call of Duty 4: Modern Warfare is the reference classic-D3D9/x86 test.

## 11. OpenGL frontend

Hook Windows OpenGL presentation/context lifecycle (`SwapBuffers`/WGL context operations) only when an OpenGL context is actually active.

Preferred NVIDIA path:

- create owned D3D11 canonical input/output textures;
- register them with `WGL_NV_DX_interop2` where available;
- lock/unlock only around actual GL/D3D access;
- copy/blit the current OpenGL framebuffer to the interop input;
- run the D3D11/Feature 18 pipeline;
- copy/blit the interop output back to the framebuffer;
- call original SwapBuffers.

Fallback within the OpenGL frontend may use OpenGL PBO/fence-based transfer only if NV interop is unavailable. It is still a renderer-specific compatibility path, not desktop capture.

The frontend must preserve and restore all GL state it changes.

## 12. Vulkan frontend

Hook Vulkan dispatch through `vkGetInstanceProcAddr` and `vkGetDeviceProcAddr`, tracking only the process's actual Vulkan objects.

Track at minimum:

- instance/device/physical-device identity;
- graphics/present queues;
- swapchain creation/destruction;
- swapchain images and format/extent;
- `vkQueuePresentKHR`;
- queue-family ownership required for injected copy operations.

Preferred path:

1. Create D3D11 canonical textures with Win32 shareable handles.
2. Import the handles as Vulkan external memory compatible with `VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT` or the supported equivalent for the device/format.
3. Before present, transition/copy the selected Vk swapchain image into imported canonical input.
4. Synchronize to D3D11/Feature 18 using Win32 external synchronization primitives when supported.
5. Process through Feature 18.
6. Copy canonical output back to the swapchain image.
7. Restore layout/ownership and execute original present.

If the device cannot support the required external-memory/synchronization path, remain passthrough with an explicit diagnostic rather than silently using desktop capture.

## 13. Guide strategy

Renderer compatibility and guide quality are separate concerns.

All frontends must provide color first. Guides are selected using the existing policy order where compatible:

1. trusted game-provided NGX/Streamline temporal resources;
2. native tracked game resources;
3. camera reconstruction;
4. optical flow;
5. zero motion.

Legacy APIs may initially use optical-flow/zero-motion fallback while API-specific depth/motion extraction matures.

Optional future `EngineGuideAdapter` modules may improve UE/Unity/id Tech/IW guide detection, but they are not required for renderer compatibility.

## 14. Half Sword / UE5 modern recovery

Half Sword must remain in the D3D12 family.

Add enhanced-barrier observation by hooking the relevant `ID3D12GraphicsCommandList7::Barrier` virtual method after capability-safe discovery. Record touches only while queue proof is needed, mirroring the existing v0.3.2 optimization that disables command-list touch capture after trust is established.

Recovery acceptance requires consistent evidence tying the swapchain backbuffer to a DIRECT queue. Once trusted, disable recovery tracking and instantiate the normal D3D12 pipeline.

If recovery times out, publish a reasoned passthrough diagnostic, e.g.:

```text
Source API: D3D12
Renderer route: Modern D3D12 recovery
State: Passthrough
Reason: swapchain graphics queue could not be proven safely
```

## 15. Detach lifecycle and inert-resident fallback

The existing quiescent-detach mechanism is extended across all new hook families.

Every D3D9/D3D10/OpenGL/Vulkan/Enhanced-Barrier hook must enter the shared `HookCallScope`.

Detach sequence:

1. set global unloading state;
2. disable UniversalDLSS5 custom work in all hooks;
3. disable hook entry points where safe;
4. new calls immediately forward to original APIs;
5. wait for in-flight hook scopes to drain;
6. destroy compatibility/frontend resources outside global registry locks;
7. stop neural/external-host state;
8. uninitialize hook libraries only after quiescence.

If hooks fail to quiesce within the safe timeout, do **not** destroy live objects, free trampolines or call `FreeLibrary` forcibly. Transition to `DetachedResident`:

- all hooks are pass-through only;
- no neural or compatibility processing occurs;
- controller reports that the bridge is inert and retained until process exit.

This state must be supported even when initialization stopped in Passthrough or partial-failure state.

## 16. Process scanner and primary renderer election

Extend renderer observation so non-DXGI APIs can publish candidates.

`RendererCandidate` gains an API/route score policy that can represent D3D9, D3D10, OpenGL and Vulkan without making a tiny helper context outrank the main renderer.

Native D3D12/D3D11 do not lose preference merely because additional frontend DLLs are loaded. Candidate scoring remains based primarily on proven presentation size, freshness, root-process relation and active processing, with API preference only as a tie-breaker.

## 17. Diagnostics and UI

Publish at least:

```text
Source API
Renderer route
Compatibility frontend
Game architecture: x86/x64
Neural execution: in-game/external x64 host
Interop method
Color path resolution
Compatibility frames
Legacy transfer/copy count
Queue proof status
Detach mode: unloaded/detached-resident
```

Examples:

```text
Source API: D3D9
Renderer route: CompatD3D9Classic
Game architecture: x86
Neural API: D3D12
Execution location: EXTERNAL x64 HOST
Interop: full-resolution legacy transfer ring
```

```text
Source API: D3D12
Renderer route: NativeD3D12
Compatibility frames: 0
Legacy copies: 0
```

The controller may expose an Advanced renderer-route override for debugging, but automatic routing remains the default.

## 18. Performance accounting

Add monotonic counters for:

- `nativeFrames`;
- `compatFrames`;
- `legacyCopies`;
- `externalHostFrames`;
- per-frontend processing time where measurable.

Regression invariant for proven native x64 D3D11/D3D12 games:

```text
compatFrames == 0
legacyCopies == 0
```

No compatibility object may be constructed during their steady-state processing path.

## 19. Build and packaging

### 19.1 Windows libraries

Add build dependencies only to the targets that need them:

- D3D10 frontend: `d3d10`, `dxgi`;
- D3D9 frontend: `d3d9`;
- OpenGL frontend: `opengl32`; extension functions resolved dynamically;
- Vulkan frontend: avoid a hard dependency on a specific Vulkan loader SDK when practical by dynamically resolving loader exports; headers may be vendored/SDK-provided according to repository licensing policy.

### 19.2 Architectures

Continue producing:

- x64 controller/injector/bridge/NRHost;
- x86 injector/bridge.

The x86 bridge must contain compatibility frontends that are valid for 32-bit games and must select external-host neural execution rather than compile out neural support entirely.

### 19.3 Version

All current version metadata becomes `0.4.0` / `0.4.0.0`. Historical v0.3.x documentation remains unchanged.

## 20. Security and failure containment

- Never duplicate/open GPU handles across processes without validating the expected host PID and session.
- Preserve current shared-memory ACL/security behavior.
- Validate imported Vulkan/D3D shared handles and adapter identity.
- Never dereference game resources after lifetime proof is lost.
- Compatibility hooks must be independently disable-able on failure.
- Unsupported APIs/formats stay passthrough and publish the reason.

## 21. Test strategy

### 21.1 Portable policy/wiring tests

Add tests for:

- renderer-route selection;
- native fast-path isolation;
- route caching;
- x86 -> external-host Feature 18 selection;
- modern D3D12 recovery proof policy;
- enhanced-barrier wiring;
- D3D10/D3D9/OpenGL/Vulkan hook wiring;
- compatibility resource generation/recreation policy;
- inert-resident detach fallback;
- diagnostics strings/counters;
- v0.4.0 version metadata;
- release packaging.

Use TDD for each implementation slice.

### 21.2 Windows build validation

Required before release:

- x64 Release build;
- x86 bridge/injector build;
- installer/portable package build;
- package verification;
- no-frame-readback audit updated so classic D3D9's explicit compatibility transfer is documented/isolated rather than globally weakening the audit.

### 21.3 Runtime game matrix

Reference tests:

- The Long Drive — existing native D3D11 regression/performance.
- Cyberpunk 2077 — existing native D3D12 regression/performance.
- Hitman World of Assassination — complex D3D12 attach regression.
- Half Sword — UE5 modern D3D12 recovery and Passthrough detach.
- Call of Duty 4: Modern Warfare — x86 classic D3D9.
- Tomb Raider (2013) DX9 — second D3D9 implementation.
- Tomb Raider (2013) DX11 — x86 modern renderer to x64 Feature 18.
- Crysis DX10 — D3D10.
- Far Cry 2 DX10 — second D3D10 engine.
- Doom 3 or Quake 4 — older OpenGL.
- DOOM (2016) OpenGL — modern OpenGL.
- DOOM (2016) Vulkan — Vulkan A/B against same game.
- Wolfenstein II — second Vulkan engine.

For every currently supported native title, compare v0.4.0 against v0.3.2 for route, output correctness, Feature 18 timing, frame pacing and additional-copy counters.

## 22. Implementation slices

The implementation is intentionally staged so regressions can be isolated:

1. Core route model, diagnostics, counters and v0.4.0 metadata.
2. Detach lifecycle generalization and inert-resident fallback.
3. x86 external-host neural route as a first-class policy.
4. Modern D3D12 recovery + Enhanced Barriers; validate Half Sword before legacy APIs.
5. D3D10 frontend.
6. D3D9Ex frontend.
7. Classic D3D9 transfer-ring frontend; validate CoD4.
8. OpenGL frontend.
9. Vulkan frontend.
10. Process-scanner/UI route visibility and final packaging/docs.

Each slice must leave the portable suite green and must preserve the native fast-path invariant.

## 23. Release acceptance criteria

v0.4.0 is release-ready only when all of the following are true:

- all portable tests pass;
- Windows x64 and x86 release builds succeed;
- current D3D11/D3D12 test games report native routes with zero compatibility frames/copies;
- Half Sword either processes through a safely proven D3D12 queue or remains passthrough with safe detach and explicit reason;
- at least one D3D10 game reaches processing;
- at least one D3D9/x86 game reaches Feature 18 through the x64 host;
- at least one OpenGL game reaches processing;
- at least one Vulkan game reaches processing;
- no compatibility route lowers the Feature 18 color resolution;
- detach does not freeze/crash tested games; unsafe unload falls back to `DetachedResident`;
- no screen-capture fallback exists;
- diagnostics correctly identify route, execution location and interop method.

## 24. Known risks

- Classic D3D9 may require CPU-visible transfer because its resource sharing model predates modern interop. That route is expected to be slower and is intentionally isolated.
- OpenGL interop behavior differs by driver/context/profile; state preservation must be audited carefully.
- Vulkan external-memory format compatibility and synchronization vary by driver/device extension support.
- D3D12 Enhanced Barrier hook discovery must be capability-safe and must not assume a CommandList7 interface exists.
- Some games use multiple render processes or helper swapchains; renderer election must continue to avoid overlays/helper contexts.
- Anti-cheat/protected titles may remain unsupported by policy or process protection rather than renderer compatibility.

