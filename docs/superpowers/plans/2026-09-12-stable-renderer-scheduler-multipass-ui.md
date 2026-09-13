# Stable Renderer / Scheduler / Multipass / UI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stabilize renderer ownership and Feature-18 scheduling, add safe 1-4 pass processing, and modernize the low-overhead controller UI.

**Architecture:** Elect one renderer across bridge statuses and publish ownership through shared memory. Replace hard queue bypasses with a bounded per-slot GPU scheduler and completed-output cache. Keep multipass refinement isolated from the main temporal history, and move process maintenance off the UI timer.

**Tech Stack:** C++20, Win32/DWM/UxTheme, DXGI/D3D11/D3D12, NVIDIA NGX Feature 18, CMake/CTest.

**Spec:** `docs/superpowers/specs/2026-09-12-stable-renderer-scheduler-multipass-ui-design.md`

## Global Constraints
- Preserve the known-working signed Feature-18 initialization and evaluate contract.
- No CPU screenshot, staging/readback, or arbitrary frame-memory path.
- Never block the render thread waiting for routine neural queue availability.
- Extra neural passes must not create extra cross-frame temporal histories.
- Export must contain one top-level `UniversalDLSS5/` folder.

---

### Task 1: Stable renderer ownership
**Files:** `include/udlss/renderer_selection_policy.hpp`, `src/controller/ui.cpp`, `src/bridge/dxgi_hooks.cpp`, `tests/renderer_selection_policy_tests.cpp`
- [x] Add a deterministic renderer score/freshness/hysteresis policy.
- [x] Verify a 1076x560 D3D11 353.8-FPS helper loses to a 1920x1080 D3D12 60-FPS renderer.
- [x] Publish the elected PID and suppress neural execution in non-primary bridges.

### Task 2: Non-flickering neural scheduler
**Files:** `include/udlss/neural_scheduler_policy.hpp`, `src/neural/ngx_nr.cpp`, `tests/neural_scheduler_policy_tests.cpp`
- [x] Add 3-8 slot scheduling with growth and soft backpressure.
- [x] Give every in-flight slot private shared color/output/scratch/guide textures.
- [x] Reuse latest completed output on pressure without marking Feature 18 failed or resetting history.

### Task 3: 1-4 pass neural refinement
**Files:** `include/udlss/settings.hpp`, `include/udlss/profile.hpp`, `src/neural/ngx_nr.cpp`, `src/controller/ui.cpp`, `tests/profile_tests.cpp`
- [x] Persist and normalize pass count 1-4.
- [x] Execute pass 1 on normal temporal feature and passes 2-4 on a reset-only refinement feature.
- [x] Ping-pong output/scratch resources and safely fall back to 1x if refinement is unavailable.

### Task 4: Low-overhead modern controller
**Files:** `src/controller/ui.cpp`, `CMakeLists.txt`
- [x] Move tree discovery/pruning/reinjection checks to `std::jthread`.
- [x] Restrict timer polling to shared-memory status and changed-text updates.
- [x] Add System/Light/Dark theme setting, DWM title bar, neutral palette and owner-drawn rounded buttons/tabs.

### Task 5: Diagnostics and regression verification
**Files:** `include/udlss/shared_control.hpp`, `src/common/shared_control_win.cpp`, `tests/v029_architecture_wiring_tests.cpp`, `tools/verify_package.py`
- [x] Bump shared-control ABI for renderer/scheduler/pass diagnostics.
- [x] Add source-wiring regression checks for renderer election, soft scheduler, multipass and modern UI.
- [x] Run clean configure/build/CTest, package verifier and no-readback audit.
- [x] Export one-folder ZIP, re-extract it, and repeat the full verification against exported contents.
