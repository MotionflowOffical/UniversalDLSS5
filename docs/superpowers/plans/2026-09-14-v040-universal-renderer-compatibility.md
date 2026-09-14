# UniversalDLSS5 v0.4.0 Universal Renderer Compatibility Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add renderer-route aware compatibility for D3D9, D3D10, OpenGL, Vulkan, 32-bit games through the x64 NRHost, modern D3D12 recovery, and safe resident detach while preserving the existing D3D11/D3D12 fast paths.

**Architecture:** Keep v0.3.2 native D3D11/D3D12 processing untouched in the hot path and route only unsupported renderers through API-specific compatibility frontends that produce full-resolution D3D11 canonical textures. 32-bit games always use the existing x64 NRHost for Feature 18, and D3D12 recovery gathers stronger queue evidence before promoting to the normal D3D12 pipeline. All new hook families share quiescent detach and may fall back to an inert resident bridge instead of force-unloading.

**Tech Stack:** C++20, Win32, MinHook, D3D9/D3D10/D3D11/D3D12/DXGI, OpenGL/WGL, Vulkan loader dispatch, NVIDIA NGX/Feature 18, CMake/CTest.

**Spec:** `docs/superpowers/specs/2026-09-14-v040-universal-renderer-compatibility-design.md`

## Global Constraints

- Target version is exactly `0.4.0` / `0.4.0.0`.
- Native `NativeD3D11` and `NativeD3D12` frames must not execute compatibility code or increment compatibility/legacy-copy counters.
- No screen-capture or desktop-capture fallback may be added.
- Feature 18 color input/output remains full resolution on every route; only auxiliary guide work may be lower resolution.
- x86 games use real Feature 18 through `UniversalDLSS5.NRHost.exe` x64.
- Unsupported or unsafe interop remains passthrough with a specific diagnostic.
- Detach must prefer `DetachedResident` over destroying live objects or trampolines when hooks do not quiesce safely.
- The uploaded logs are regression evidence: D3D12 can reach neural processing and later access-violate while idle, and some attaches remain at `Waiting for stable Present stream`.

---

### Task 1: Core renderer-route model, diagnostics, counters, and v0.4.0 metadata

**Files:**
- Create: `include/udlss/renderer_route_policy.hpp`
- Modify: `include/udlss/shared_control.hpp`
- Modify: `include/udlss/renderer_selection_policy.hpp`
- Modify: `include/udlss/version.hpp`
- Modify: `src/controller/ui.cpp`
- Modify: `src/bridge/attach_logger.cpp`
- Modify: `src/neural/ngx_nr.cpp`
- Modify: `src/neural/streamline_nr.cpp`
- Modify: `src/host/main.cpp`
- Modify: `resources/UniversalDLSS5.rc`
- Modify: `BUILD_WINDOWS.bat`
- Modify: `BUILD_RELEASE.bat`
- Modify: `runtime/README.txt`
- Modify: `OVERWRITE_INSTALL.txt`
- Modify: `CMakeLists.txt`
- Test: `tests/renderer_route_policy_tests.cpp`
- Test: `tests/v040_version_wiring_tests.cpp`

**Interfaces:**
- Produces `enum class RendererRoute`, `enum class CompatInterop`, route-name helpers, and native-fast-path predicates.
- Extends `GraphicsApi` with D3D9/D3D10/OpenGL/Vulkan.
- Extends `RuntimeStatus` with route/interoperability/counter/architecture/detach fields used by all later tasks.

- [ ] **Step 1: Write failing route/version tests** that assert native routes are isolated, compatibility routes are classified, all source APIs are representable, and live version strings are `0.4.0`.
- [ ] **Step 2: Run the targeted tests and verify RED** because the route types and v0.4.0 strings do not yet exist.
- [ ] **Step 3: Implement the minimal route model and status fields** using `RendererRoute::{Unknown,NativeD3D11,NativeD3D12,ModernD3D12Recovery,CompatD3D10,CompatD3D9Ex,CompatD3D9Classic,CompatOpenGL,CompatVulkan,Unsupported}` and `CompatInterop::{None,D3D11Native,D3D12On12,D3D10Shared,D3D9ExShared,D3D9ClassicTransfer,WglNvDxInterop,VulkanExternalMemory}`.
- [ ] **Step 4: Update version metadata and controller diagnostics** to display source API, renderer route, game architecture, neural execution location, interop, full-resolution color path, native/compat/legacy/external-host counters, queue proof, and detach mode.
- [ ] **Step 5: Run targeted tests and the existing native hot-path policy tests** and verify GREEN.

### Task 2: Detach lifecycle generalization and inert-resident fallback

**Files:**
- Modify: `include/udlss/hook_quiescence.hpp`
- Modify: `src/bridge/hook_lifecycle.hpp`
- Modify: `src/bridge/hook_lifecycle.cpp`
- Modify: `src/bridge/dxgi_hooks.hpp`
- Modify: `src/bridge/dxgi_hooks.cpp`
- Modify: `src/bridge/bridge_main.cpp`
- Test: `tests/detach_resident_policy_tests.cpp`
- Test: `tests/detach_lifecycle_wiring_tests.cpp`

**Interfaces:**
- Produces `enum class DetachMode { None, Unloaded, DetachedResident }` in the shared route policy.
- Produces `HookShutdownResult shutdownHookFamilies(std::chrono::milliseconds timeout)` and a pass-through-only resident mode.

- [ ] **Step 1: Write failing detach policy/wiring tests** asserting timeout does not permit teardown/unload and that resident mode leaves hooks forwarding-only.
- [ ] **Step 2: Run targeted tests and verify RED.**
- [ ] **Step 3: Change hook shutdown from an unbounded wait to a bounded quiescence decision**: disable custom work first, attempt to disable hooks, wait up to five seconds, and return `DetachedResident` without destroying pipeline/tracker state or uninitializing MinHook if callbacks remain active or hook disabling fails.
- [ ] **Step 4: Update `bridgeThread`** so `FreeLibraryAndExitThread` is called only for the proven-unloaded result; resident mode closes the shared control only after publishing status and leaves the module loaded until process exit.
- [ ] **Step 5: Run detach tests and existing detach/quiescence tests and verify GREEN.**

### Task 3: First-class x86 -> x64 Feature 18 route

**Files:**
- Create: `include/udlss/compat_neural_policy.hpp`
- Modify: `include/udlss/neural_route_policy.hpp`
- Modify: `src/neural/external_host_protocol.hpp`
- Modify: `src/neural/external_host.cpp`
- Modify: `src/host/main.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/compat_neural_policy_tests.cpp`
- Test: `tests/external_host_policy_tests.cpp`

**Interfaces:**
- Produces `selectCompatibilityNeuralLocation(bool processIs64Bit, RendererRoute route, bool hostAvailable)`.
- Extends host IPC ABI with source API, renderer route, bridge bitness, canonical format/colorspace, resource generation, and capability flags.

- [ ] **Step 1: Write failing x86 neural-route tests** asserting every supported x86 renderer route selects `ExternalProcess` while x64 native routes retain the current policy.
- [ ] **Step 2: Run targeted tests and verify RED.**
- [ ] **Step 3: Extend host IPC to ABI 5** with fixed-width enum/integer fields and publish them from `ExternalHostBackend` before frame submission.
- [ ] **Step 4: Make 32-bit bridge builds compile and select the external-host backend instead of treating NGX absence as neural unsupported.** The x86 build does not link the x64 NGX library; it launches the x64 NRHost beside the bridge and uses shared D3D11 resources/fences.
- [ ] **Step 5: Run external-host and neural-route tests and verify GREEN.**

### Task 4: Modern D3D12 recovery and Enhanced Barrier evidence

**Files:**
- Create: `include/udlss/d3d12_recovery_policy.hpp`
- Modify: `src/gpu/d3d12_resource_tracker.hpp`
- Modify: `src/gpu/d3d12_resource_tracker.cpp`
- Modify: `src/bridge/dxgi_hooks.cpp`
- Test: `tests/d3d12_recovery_policy_tests.cpp`
- Test: `tests/d3d12_enhanced_barrier_wiring_tests.cpp`

**Interfaces:**
- Produces `D3D12RecoveryEvidence` and `evaluateD3D12Recovery(...)` with a threshold that requires backbuffer-specific DIRECT-queue evidence.
- Adds capability-safe observation of `ID3D12GraphicsCommandList7::Barrier` when CommandList7 exists.

- [ ] **Step 1: Write failing recovery policy/wiring tests** for creation proof, legacy barrier proof, enhanced-barrier proof, repeated execute correlation, and rejection of generic queue activity.
- [ ] **Step 2: Run targeted tests and verify RED.**
- [ ] **Step 3: Add enhanced-barrier touch capture** by creating a capability probe command list, querying `ID3D12GraphicsCommandList7`, hooking its Barrier vtable slot only when available, and reporting resource touches into the existing tracker.
- [ ] **Step 4: Route D3D12 swapchains with valid device/backbuffers but no trusted queue to `ModernD3D12Recovery`**, keep passthrough while evidence is insufficient, and promote to the existing D3D12On12 pipeline only after proof.
- [ ] **Step 5: Run recovery tests plus present-queue/native-hot-path tests and verify GREEN.**

### Task 5: Compatibility dispatch and canonical full-resolution surface abstraction

**Files:**
- Create: `src/compat/renderer_frontend.hpp`
- Create: `src/compat/canonical_surface.hpp`
- Create: `src/compat/canonical_surface.cpp`
- Create: `src/compat/compat_dispatch.hpp`
- Create: `src/compat/compat_dispatch.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/compat_dispatch_policy_tests.cpp`
- Test: `tests/native_fastpath_isolation_tests.cpp`

**Interfaces:**
- Produces `CompatFrame`, `IRendererFrontend`, `CompatDispatch`, and canonical D3D11 input/output texture ownership.
- `CompatDispatch` is never constructed for `NativeD3D11`/`NativeD3D12`.

- [ ] **Step 1: Write failing dispatch/isolation tests** that prove native routes never create a compatibility frontend and compatibility routes do.
- [ ] **Step 2: Run targeted tests and verify RED.**
- [ ] **Step 3: Implement canonical surfaces** as full-resolution D3D11 textures with explicit generation, format, adapter identity, and recreation on size/format change.
- [ ] **Step 4: Implement compatibility dispatch** with frontend registration/selection by `RendererRoute`, per-route counters, and failure-to-passthrough semantics.
- [ ] **Step 5: Run tests and verify GREEN.**

### Task 6: D3D10 DXGI frontend

**Files:**
- Create: `src/compat/d3d10_frontend.hpp`
- Create: `src/compat/d3d10_frontend.cpp`
- Modify: `src/bridge/dxgi_hooks.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/d3d10_frontend_wiring_tests.cpp`

**Interfaces:**
- Produces `D3D10Frontend` implementing `IRendererFrontend`.
- Uses same-adapter shared D3D10 textures opened by D3D11 and copies output back before original Present.

- [ ] **Step 1: Write failing wiring tests** for D3D10 device detection, `CompatD3D10`, shared-resource creation/opening, and `d3d10` link dependency.
- [ ] **Step 2: Run targeted test and verify RED.**
- [ ] **Step 3: Implement D3D10 frontend** using `IDXGISwapChain::GetDevice(ID3D10Device)`, `D3D10_RESOURCE_MISC_SHARED_KEYEDMUTEX` textures, D3D11 `OpenSharedResource`, keyed mutex synchronization, full-resolution copy in/out, and resize recreation.
- [ ] **Step 4: Integrate D3D10 only into the DXGI unsupported-device branch**, leaving D3D11/12 branches unchanged.
- [ ] **Step 5: Run tests and verify GREEN.**

### Task 7: D3D9/D3D9Ex frontend and classic-D3D9 transfer ring

**Files:**
- Create: `src/compat/legacy_copy_ring.hpp`
- Create: `src/compat/legacy_copy_ring.cpp`
- Create: `src/compat/d3d9_frontend.hpp`
- Create: `src/compat/d3d9_frontend.cpp`
- Create: `src/bridge/d3d9_hooks.hpp`
- Create: `src/bridge/d3d9_hooks.cpp`
- Modify: `src/bridge/bridge_main.cpp`
- Modify: `src/bridge/dxgi_hooks.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tools/audit_no_readback.py`
- Test: `tests/d3d9_frontend_wiring_tests.cpp`
- Test: `tests/legacy_copy_ring_policy_tests.cpp`

**Interfaces:**
- Produces D3D9/D3D9Ex Present/PresentEx/Reset/ResetEx hooks sharing `HookCallScope`.
- D3D9Ex prefers shared GPU textures; classic D3D9 uses a three-slot full-resolution transfer ring explicitly counted as legacy copies.

- [ ] **Step 1: Write failing policy/wiring tests** for D3D9 creation hooks, device Present/Reset hooks, D3D9Ex/shared route, classic-transfer route, and audit isolation marker.
- [ ] **Step 2: Run targeted tests and verify RED.**
- [ ] **Step 3: Implement D3D9 hook bootstrap** by hooking `Direct3DCreate9`/`Direct3DCreate9Ex`, wrapping the returned object creation calls through device-vtable hooks, and entering the common quiescence scope for device detours.
- [ ] **Step 4: Implement D3D9Ex GPU-sharing path** with owned shareable render-target textures and D3D11 shared-resource opening on the same adapter.
- [ ] **Step 5: Implement classic D3D9 transfer ring** with three `D3DPOOL_SYSTEMMEM` surfaces plus D3D11 dynamic upload/readback-compatible compatibility textures; transfers remain full resolution and are isolated behind `CompatD3D9Classic`.
- [ ] **Step 6: Update the readback audit** so only code inside `legacy_copy_ring.cpp` carrying the explicit `UDLSS_LEGACY_D3D9_TRANSFER` marker may contain frame readback API tokens.
- [ ] **Step 7: Run D3D9 tests, audit, and native isolation tests and verify GREEN.**

### Task 8: OpenGL/WGL frontend

**Files:**
- Create: `src/compat/opengl_frontend.hpp`
- Create: `src/compat/opengl_frontend.cpp`
- Create: `src/bridge/opengl_hooks.hpp`
- Create: `src/bridge/opengl_hooks.cpp`
- Modify: `src/bridge/bridge_main.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/opengl_frontend_wiring_tests.cpp`

**Interfaces:**
- Produces SwapBuffers/WGL lifecycle hooks and `OpenGLFrontend`.
- Prefers `WGL_NV_DX_interop2`; otherwise uses an OpenGL PBO/fence renderer-specific transfer path, never desktop capture.

- [ ] **Step 1: Write failing wiring tests** for SwapBuffers hook, WGL context tracking, NV-DX interop extension resolution, state preservation, and `CompatOpenGL` diagnostics.
- [ ] **Step 2: Run targeted test and verify RED.**
- [ ] **Step 3: Implement WGL hook bootstrap** using `wglSwapBuffers`/`SwapBuffers` plus context-change hooks, scoped with `HookCallScope`.
- [ ] **Step 4: Implement NV-DX interop path** using dynamically resolved `wglDXOpenDeviceNV`, `wglDXRegisterObjectNV`, lock/unlock, framebuffer blit into canonical input, and blit canonical output back while restoring framebuffer/viewport/program/texture state.
- [ ] **Step 5: Implement renderer-local PBO fallback** only when interop extension is unavailable, keeping full resolution and incrementing compatibility copy counters.
- [ ] **Step 6: Run tests and native isolation tests and verify GREEN.**

### Task 9: Vulkan frontend with dynamic loader dispatch

**Files:**
- Create: `src/compat/vulkan_abi.hpp`
- Create: `src/compat/vulkan_frontend.hpp`
- Create: `src/compat/vulkan_frontend.cpp`
- Create: `src/bridge/vulkan_hooks.hpp`
- Create: `src/bridge/vulkan_hooks.cpp`
- Modify: `src/bridge/bridge_main.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/vulkan_frontend_wiring_tests.cpp`

**Interfaces:**
- Defines only the Vulkan ABI types/function pointers required for loader interception so the bridge has no hard Vulkan SDK dependency.
- Tracks instance/device/physical-device/queues/swapchains and intercepts `vkGetInstanceProcAddr`, `vkGetDeviceProcAddr`, `vkCreateSwapchainKHR`, `vkDestroySwapchainKHR`, `vkGetSwapchainImagesKHR`, and `vkQueuePresentKHR`.

- [ ] **Step 1: Write failing wiring tests** for dynamic loader interception, required Vulkan entry points, external-memory capability checks, and explicit passthrough diagnostic when unsupported.
- [ ] **Step 2: Run targeted test and verify RED.**
- [ ] **Step 3: Implement loader hooks and object registries** without linking against `vulkan-1.lib`; resolve loader exports dynamically and return wrapped function pointers for the tracked functions.
- [ ] **Step 4: Implement external-memory capability negotiation** for Win32 D3D11 texture handles and Win32 external semaphores/fences. Create/import resources only when the physical device reports compatible handle types and formats.
- [ ] **Step 5: Implement queue-present processing** by submitting layout transitions/copies around the selected swapchain image, synchronizing through Win32 external objects, running the canonical D3D11/Feature 18 path, copying output back, restoring layout/ownership, then calling original `vkQueuePresentKHR`.
- [ ] **Step 6: Run tests and native isolation tests and verify GREEN.**

### Task 10: Process scanner, UI visibility, release docs, packaging and full verification

**Files:**
- Modify: `src/controller/processes.cpp`
- Modify: `src/controller/ui.cpp`
- Modify: `include/udlss/renderer_selection_policy.hpp`
- Modify: `README.md`
- Create: `docs/V0.4.0_RELEASE.md`
- Modify: `tools/verify_package.py`
- Modify: `CMakeLists.txt`
- Test: `tests/renderer_selection_policy_tests.cpp`
- Test: `tests/v040_compatibility_wiring_tests.cpp`
- Test: `tests/release_packaging_tests.cpp`

**Interfaces:**
- Publishes non-DXGI renderer candidates without allowing helper contexts to outrank a full-size primary renderer.
- Makes route/interop/counters/detach state visible in Diagnostics and final v0.4.0 packaging.

- [ ] **Step 1: Write failing final wiring/package tests** requiring all hook families, route diagnostics, x86 external-host route, v0.4.0 metadata, and no screen-capture implementation tokens.
- [ ] **Step 2: Run targeted tests and verify RED.**
- [ ] **Step 3: Extend process/runtime observation and UI** to display renderer route and compatibility status while preserving the current app-selector/theme behavior.
- [ ] **Step 4: Write `docs/V0.4.0_RELEASE.md` and update README/current package metadata** with current features and compatibility limitations, not a full changelog.
- [ ] **Step 5: Run a clean portable configure/build and `ctest --output-on-failure`.**
- [ ] **Step 6: Run `python3 tools/audit_no_readback.py` and `python3 tools/verify_package.py`.**
- [ ] **Step 7: Package a single top-level `UniversalDLSS5/` source ZIP and verify byte-for-byte source-tree equality against the tested tree.**

## Final manual Windows validation required before publishing v0.4.0

Run on Windows with the resulting package:

```bat
BUILD_WINDOWS.bat
BUILD_RELEASE.bat
```

Then test at minimum: The Long Drive, Cyberpunk 2077, Hitman WoA, Half Sword, Call of Duty 4 MW, one D3D10 title, one OpenGL title, and one Vulkan title. The Linux/portable verification performed during implementation cannot replace those Windows runtime checks.
