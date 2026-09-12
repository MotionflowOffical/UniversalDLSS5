# UniversalDLSS5 v0.2.8 Direct In-Game NR Mount Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a direct in-game DLSS-NR mount for non-DLSS games and improve motion-guide quality by preferring native velocity/camera+depth before NVOFA/HLSL fallback.

**Architecture:** Extend the existing injected bridge rather than introducing another injected proxy. A render-resource tracker and shader/constant tracker produce native guide candidates; a camera-motion compute path derives current-to-previous vectors when matrices+depth are available; an in-game neural backend evaluates either Streamline feature 1004 or the existing signed feature-18 route, with the external NRHost retained as fallback.

**Tech Stack:** C++20, Win32, D3D11, D3D12, DXGI, D3DCompiler/D3DReflect, NVIDIA NGX SDK, optional NVIDIA Streamline SDK/runtime, optional NVIDIA Optical Flow SDK, MinHook, CMake.

**Spec:** `docs/superpowers/specs/2026-09-12-direct-ingame-nr-mount-design.md`

## Global Constraints

- No anti-cheat, DRM, CIG, or protected-process bypass.
- No CPU framebuffer readback, screenshots, staging-image transfer, or BitBlt/WGC capture.
- No arbitrary process-memory scanning for camera data.
- No proxy-DLL masquerading as `dxgi.dll`, `d3d11.dll`, or system DLLs.
- NVIDIA/Streamline proprietary DLLs remain user-supplied and excluded from packages.
- Preserve the working v0.2.7 external-host signed-feature-18 path as fallback.
- Motion convention presented to DLSS-NR remains current-pixel -> previous-pixel in pixel units.

---

### Task 1: Route policy and diagnostics for in-game execution

**Files:**
- Modify: `include/udlss/settings.hpp`
- Modify: `include/udlss/backend_policy.hpp`
- Modify: `include/udlss/runtime_diagnostics.hpp`
- Modify: `include/udlss/shared_control.hpp`
- Test: `tests/backend_policy_tests.cpp`
- Test: `tests/runtime_diagnostics_tests.cpp`

**Interfaces:**
- Produces: `BackendMode::InGameNR`, `NeuralExecutionLocation`, and diagnostic names for `Streamline1004`, `SignedFeature18InGame`, `ExternalHost`.

- [ ] Write failing policy/diagnostic tests that select in-game NR by default when direct prerequisites exist and preserve external-host fallback.
- [ ] Run the two tests and confirm failure.
- [ ] Add the enum/state fields and route helpers with backward profile migration.
- [ ] Run tests and confirm pass.

### Task 2: Native D3D11 motion-resource tracker

**Files:**
- Create: `include/udlss/native_motion_policy.hpp`
- Create: `src/gpu/d3d11_resource_tracker.hpp`
- Create: `src/gpu/d3d11_resource_tracker.cpp`
- Modify: `src/bridge/dxgi_hooks.cpp`
- Test: `tests/native_motion_policy_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `NativeMotionCandidateMeta`, `scoreNativeMotionCandidate(...)`, and `D3D11ResourceTracker::bestMotionCandidate()` returning an `ID3D11Texture2D*` plus format/score metadata.

- [ ] Write failing tests for candidate scoring: same-resolution RG16F velocity-like resources score above shadow/depth/color resources; weak candidates are not auto-selected.
- [ ] Run the test and confirm failure.
- [ ] Implement portable scoring policy.
- [ ] Implement D3D11 resource/view/binding metadata tracking using graphics API hooks only.
- [ ] Wire tracker observation points into existing D3D11 hook/state path.
- [ ] Run tests and confirm pass.

### Task 3: Shader reflection and camera-matrix classification

**Files:**
- Create: `include/udlss/camera_matrix_policy.hpp`
- Create: `src/gpu/d3d11_camera_tracker.hpp`
- Create: `src/gpu/d3d11_camera_tracker.cpp`
- Test: `tests/camera_matrix_policy_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `CameraMatrixSnapshot` with validated current/previous view/projection matrices and `classifyMatrixBindingName(std::string_view)`.

- [ ] Write failing tests for common matrix-name classification (`View`, `Projection`, `ViewProjection`, `PrevViewProj`, Unity-style names) and false-positive rejection.
- [ ] Run the test and confirm failure.
- [ ] Implement portable classification/validation helpers.
- [ ] Implement D3D11 shader reflection capture at shader creation and constant-buffer update/bind tracking.
- [ ] Expose only validated snapshots; unknown layouts remain unavailable.
- [ ] Run tests and confirm pass.

### Task 4: Camera + depth motion reconstruction

**Files:**
- Create: `include/udlss/camera_motion_math.hpp`
- Create: `shaders/camera_motion.hlsl`
- Modify: `src/gpu/d3d11_pipeline.hpp`
- Modify: `src/gpu/d3d11_pipeline.cpp`
- Test: `tests/camera_motion_math_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: GPU `R16G16_FLOAT` current->previous motion texture from real depth + camera matrices.

- [ ] Write failing synthetic math tests: identity camera -> zero vector; known translation/rotation -> expected previous-screen displacement within tolerance; invalid W/out-of-bounds -> invalid vector.
- [ ] Run test and confirm failure.
- [ ] Implement portable matrix/reprojection reference math.
- [ ] Add `camera_motion.hlsl` with equivalent current->previous pixel convention.
- [ ] Compile/load shader in D3D11 pipeline and run only when real depth + validated matrices exist.
- [ ] Run tests and confirm pass.

### Task 5: Motion-source composer and provenance

**Files:**
- Create: `include/udlss/motion_route_policy.hpp`
- Modify: `src/gpu/d3d11_pipeline.cpp`
- Modify: `src/gpu/d3d11_guide_extractor.hpp`
- Modify: `src/gpu/d3d11_guide_extractor.cpp`
- Modify: `include/udlss/runtime_diagnostics.hpp`
- Test: `tests/motion_route_policy_tests.cpp`

**Interfaces:**
- Produces route order: explicit adapter native -> tracked native velocity -> camera+depth -> NVOFA -> HLSL -> zero.

- [ ] Write failing route tests for all availability combinations.
- [ ] Run test and confirm failure.
- [ ] Implement route policy and diagnostic source names.
- [ ] Connect tracker/camera resources into `D3D11Pipeline::process` before NVOFA/HLSL.
- [ ] Ensure history reset occurs when motion source changes.
- [ ] Run tests and confirm pass.

### Task 6: In-game direct signed feature-18 backend

**Files:**
- Create: `src/neural/ingame_nr.cpp`
- Modify: `src/neural/backend.hpp`
- Modify: `src/neural/ngx_nr.cpp` to factor reusable D3D12 feature-18 session logic if needed.
- Modify: `src/gpu/d3d11_pipeline.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/ingame_nr_policy_tests.cpp`
- Test: `tests/windows_build_wiring_tests.cpp`

**Interfaces:**
- Produces: `createInGameNR()` backend using the source process and private/supplied D3D12 queue, with signed-snippet forwarder dispatch and no NRHost IPC.

- [ ] Write failing wiring/policy tests requiring the in-game backend target and forbidding NRHost IPC calls from this route.
- [ ] Run tests and confirm failure.
- [ ] Factor/reuse the already-working feature-18 initialization/evaluation contract.
- [ ] Open shared D3D11 guide resources on the in-process D3D12 device and evaluate directly.
- [ ] Keep caller-compatible forwarder and explicit failure diagnostics.
- [ ] Run tests and confirm pass.

### Task 7: Optional Streamline feature-1004 backend

**Files:**
- Replace/expand: `src/neural/streamline_nr.cpp`
- Modify: `src/neural/backend.hpp`
- Modify: `CMakeLists.txt`
- Modify: `runtime/README.txt`
- Test: `tests/streamline_mount_policy_tests.cpp`
- Test: `tests/windows_build_wiring_tests.cpp`

**Interfaces:**
- Produces optional in-game Streamline backend that requests feature 1004, attaches D3D12, creates one frame token per frame, sets constants once, tags input/output/depth/motion resources, and evaluates DLSS-NR.

- [ ] Write failing source-wiring tests for `slInit`, feature 1004, `slSetD3DDevice`, `slGetNewFrameToken`, `slSetConstants`, and resource tagging/evaluation.
- [ ] Run tests and confirm failure.
- [ ] Implement SDK-gated compile path and runtime DLL validation without bundling binaries.
- [ ] Implement frame-token/constants/resource-tag lifecycle.
- [ ] Prefer Streamline backend only when plugin/runtime prerequisites validate; otherwise use signed feature 18.
- [ ] Run tests and confirm pass.

### Task 8: Controller/UI settings and candidate diagnostics

**Files:**
- Modify: `src/controller/ui.cpp`
- Modify: `src/common/profile.cpp`
- Modify: `include/udlss/profile.hpp`
- Modify: `include/udlss/settings.hpp`
- Test: `tests/profile_tests.cpp`
- Test: `tests/settings_tests.cpp`
- Test: `tests/windows_build_wiring_tests.cpp`

**Interfaces:**
- Adds `Direct in-game NR (recommended)` backend choice and motion-source diagnostics/candidate score display. Manual candidate lock is persisted by stable signature, not pointer.

- [ ] Write failing persistence/wiring tests.
- [ ] Run tests and confirm failure.
- [ ] Implement settings/profile migration and UI labels.
- [ ] Add diagnostics for candidate ID/score, matrix availability, execution location, and reset reason.
- [ ] Run tests and confirm pass.

### Task 9: Audits, package verifier, docs, and release package

**Files:**
- Modify: `tools/audit_no_readback.py`
- Modify: `tools/verify_package.py`
- Modify: `README.md`
- Modify: `THIRD_PARTY.md`
- Modify: `BUILD_WINDOWS.bat`
- Modify: `CMakeLists.txt` version to `0.2.8`

**Interfaces:**
- Produces a source ZIP with no proprietary NVIDIA/Streamline binaries.

- [ ] Extend no-readback audit to new tracker/mount files.
- [ ] Extend package verifier for direct mount, optional Streamline, camera-motion shader, route tests, and proprietary-binary exclusion.
- [ ] Run a clean portable configure/build/CTest suite.
- [ ] Run no-readback and package audits.
- [ ] Create source ZIP, extract exact ZIP to clean directory, and repeat tests/audits there.
- [ ] Record SHA-256 and package contents.
