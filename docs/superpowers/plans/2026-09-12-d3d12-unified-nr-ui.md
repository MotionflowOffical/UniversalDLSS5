# D3D12-Unified Neural Backend and Diagnostics UI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** Execute DLSS-NR through a unified D3D12 NGX backend for D3D11/D3D12 source applications and expose full stage diagnostics in a resizable/copyable controller UI.

**Architecture:** Existing D3D11 guide generation remains. The NGX backend opens GPU-shared D3D11 resources in D3D12 and synchronizes with a shared GPU fence; D3D12 games pass their native device/queue into the backend, while D3D11 games create a same-adapter private D3D12 device/queue. The controller renders stage diagnostics in a dedicated scrollable pane.

**Tech Stack:** C++20, Win32, DXGI, D3D11, D3D11On12, D3D12, NVIDIA NGX, CMake, MinHook.

**Spec:** `docs/superpowers/specs/2026-09-12-d3d12-unified-nr-ui-design.md`

## Global Constraints

- Frame pixels remain GPU resident; no CPU pixel readback or screenshot path.
- `nvngx_dlssnr.dll` is user-supplied and not redistributed.
- No GPU architecture spoofing, NVIDIA binary patching, anti-cheat bypass, or mitigation bypass.
- D3D11/D3D12 only in this version; Vulkan/OpenGL explicitly reported unsupported.

---

### Task 1: Route and diagnostic policies
**Files:** create `include/udlss/neural_route_policy.hpp`, modify `include/udlss/shared_control.hpp`, create/modify policy tests.
- [x] Write failing tests asserting D3D11 and D3D12 source APIs both select D3D12 as neural API and diagnostics preserve stage/failure information.
- [x] Run the portable tests and confirm the new tests fail.
- [x] Implement route/stage policy types and formatting helpers.
- [x] Re-run tests until green.

### Task 2: Backend interface and native D3D12 context
**Files:** modify `src/neural/backend.hpp`, `src/gpu/d3d11_pipeline.*`, `src/gpu/d3d12_on12.*`.
- [x] Add source-wiring regression assertions before production edits.
- [x] Add `BackendInitContext` carrying D3D11 device/context and optional native D3D12 device/queue.
- [x] Pass the native D3D12 device/queue from the D3D12 swapchain route; leave them null for D3D11 source.
- [x] Verify portable wiring tests.

### Task 3: D3D12-only NGX feature-18 backend
**Files:** replace `src/neural/ngx_nr.cpp` implementation.
- [x] Extend wiring tests to reject `NVSDK_NGX_D3D11_CreateFeature/EvaluateFeature` in the NR implementation and require D3D12 equivalents.
- [x] Initialize NGX through `NVSDK_NGX_D3D12_Init_with_ProjectID`.
- [x] For D3D11 source, create a same-adapter D3D12 device/queue.
- [x] Create shareable input/output/motion/depth textures and a cross-API fence.
- [x] Copy D3D11 guides into shared resources, GPU-signal D3D11, GPU-wait D3D12, evaluate feature 18, GPU-signal D3D12, GPU-wait D3D11, and copy output back.
- [x] Use a 3-slot allocator/list ring; bypass instead of blocking when a slot is still busy.
- [x] Preserve exact create/evaluate failures and stage bits.
- [x] Verify portable tests and source audit.

### Task 4: Stage diagnostics through bridge
**Files:** modify `src/bridge/bridge_main.cpp`, `src/bridge/dxgi_hooks.cpp`, pipeline code.
- [x] Add tests for stage formatter and persistent failure behavior.
- [x] Set stage bits at injection, hook, Present, API detection, queue readiness, guide readiness, NGX init/create/evaluate, and processed output.
- [x] Ensure unsupported/no-queue paths identify the exact failure stage.
- [x] Verify tests.

### Task 5: Resizable diagnostics UI
**Files:** modify `src/controller/ui.cpp` and `ui.hpp` as needed.
- [x] Add wiring tests requiring multiline read-only diagnostics control, copy/save commands, and resize handling.
- [x] Build a two-pane resizable layout with minimum dimensions and a compact status badge.
- [x] Render a complete diagnostic report with stage checklist.
- [x] Implement clipboard copy and timestamped `%LOCALAPPDATA%\\UniversalDLSS5\\logs` save.
- [x] Verify portable wiring tests.

### Task 6: Documentation/package verification
**Files:** modify `README.md`, package scripts if needed.
- [x] Document D3D12-unified NR route and RTX 20/30/40 experimental-runtime policy.
- [x] Run all portable tests.
- [x] Run `tools/audit_no_readback.py`.
- [x] Run strict `tools/verify_package.py` with no proprietary DLL included.
- [x] Create source ZIP, extract it cleanly, and rerun the same verification on the extracted archive.

### Task 7: Feature-18 runtime compatibility completion
**Files:** `src/neural/ngx_nr.cpp`, `tests/windows_build_wiring_tests.cpp`, `tools/verify_package.py`.
- [x] Add a failing wiring regression requiring runtime application-ID discovery, scaling-ratio callback, and perf-quality create parameter.
- [x] Resolve `NVSDK_NGX_GetApplicationId`/`NVSDK_NGX_GetSnippetVersion` when exported and use the runtime-reported application ID with a logged fallback.
- [x] Supply `DLSSNRComputeScalingRatioCallback`, render preset, and `NVSDK_NGX_Parameter_PerfQualityValue` before feature creation.
- [x] Keep caller spoofing/IAT patching/binary modification out of scope and surface caller-gate rejection as diagnostics.
- [x] Re-run portable tests and package verification.
