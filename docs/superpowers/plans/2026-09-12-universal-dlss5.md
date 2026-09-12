# UniversalDLSS5 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Produce a fully capable Windows prototype that injects into selected application process trees, intercepts DXGI presentation, synthesizes GPU temporal guides, exposes live tuning controls, and evaluates the installed Streamline DLSS NR runtime without CPU frame capture.

**Architecture:** Win32 controller + architecture-specific injector + injected bridge. The bridge hooks DXGI, processes D3D11 directly and D3D12 through D3D11On12, and uses a replaceable neural backend. Live settings/status travel through named shared memory.

**Tech Stack:** C++20, Win32/Common Controls, D3D11, D3D12, D3D11On12, DXGI, HLSL/D3DCompile, MinHook, NVIDIA Streamline public headers/runtime.

**Spec:** `docs/superpowers/specs/2026-09-12-universal-dlss5-design.md`

## Global Constraints
- Windows 10/11 x64 controller.
- No CPU pixel readback or screenshot APIs in the frame path.
- Fail-open rendering: errors bypass processing and call original Present.
- Runtime DLSS binaries are user-supplied.
- MinHook and Streamline source/header dependencies are pinned by CMake.

---

### Task 1: Shared settings and profile model
**Files:** `include/udlss/settings.hpp`, `include/udlss/shared_control.hpp`, `src/common/profile.cpp`, tests.
- [x] Add failing normalization test.
- [x] Implement normalized settings model and verify test passes.
- [x] Add profile roundtrip test, implement codec, verify.
- [x] Add shared-memory seqlock control/status transport.

### Task 2: Controller and process-tree attachment
**Files:** `src/controller/*`, `src/injector/*`.
- [x] Enumerate processes and descendants.
- [x] Add architecture/protected-process checks.
- [x] Add remote LoadLibrary injector helper.
- [x] Build Win32 controller UI with process list, attach/detach, runtime path, status and sliders.
- [x] Persist profile per executable path.

### Task 3: DXGI interception
**Files:** `src/bridge/dxgi_hooks.*`, `src/bridge/bridge_main.cpp`.
- [x] Initialize MinHook outside DllMain.
- [x] Create dummy DXGI/D3D11 objects and hook Present, Present1, ResizeBuffers.
- [x] Hook factory swapchain creation to retain D3D12 command queues.
- [x] Add thread-local recursion protection and fail-open behavior.

### Task 4: GPU guide pipeline
**Files:** `src/gpu/d3d11_pipeline.*`, `shaders/*.hlsl`.
- [x] Compile compute shaders at runtime.
- [x] Maintain current/history GPU textures.
- [x] Generate reduced luminance, block-match flow, full-resolution motion vectors, confidence and protection mask entirely on GPU.
- [x] Add exposure/sharpen/protected-detail composite.

### Task 5: D3D12 compatibility
**Files:** `src/gpu/d3d12_on12.*`.
- [x] Record command queue from DXGI factory hook.
- [x] Create D3D11On12 device on that queue.
- [x] Wrap current D3D12 backbuffer, Acquire/Release it around the shared D3D11 pipeline, flush before Present.

### Task 6: Streamline DLSS NR backend
**Files:** `src/neural/streamline_nr.*`, `src/neural/backend.hpp`.
- [x] Dynamically load Streamline core APIs.
- [x] Initialize feature 1004 with frame-based tagging and manual hooking.
- [x] Set the intercepted device and query support/required tags.
- [x] Tag uplift input/output and synthesized motion resources.
- [x] Evaluate feature and report missing required guides precisely.
- [x] Always provide passthrough fallback.

### Task 7: Packaging and verification
**Files:** `CMakeLists.txt`, `cmake/Dependencies.cmake`, `BUILD_WINDOWS.bat`, `README.md`.
- [x] Pin dependencies and configure runtime layout.
- [x] Add x64 and x86 build scripts.
- [x] Run portable unit tests in the current environment.
- [x] Perform static forbidden-API scan for screenshot/readback APIs.
- [x] Zip source package with runtime placeholder and build instructions.
