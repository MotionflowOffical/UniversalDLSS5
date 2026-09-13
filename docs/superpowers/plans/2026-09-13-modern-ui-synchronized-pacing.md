# Modern UI and Synchronized Neural Pacing Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver frame-matched Feature-18 presentation and a DPI-aware Direct2D/DirectWrite controller UI.

**Architecture:** Extend settings/status ABI with pacing mode and output-age telemetry; make NgxNR select synchronous/adaptive/async presentation semantics without changing Feature-18 contracts. Replace visible Win32 child controls with one retained top-level HWND painted and interacted with through Direct2D/DirectWrite.

**Tech Stack:** C++20, Win32, D3D11/D3D12, NVIDIA NGX Feature 18, Direct2D, DirectWrite, CMake/CTest.

**Spec:** `docs/superpowers/specs/2026-09-13-modern-ui-synchronized-pacing-design.md`

## Global Constraints
- Synchronized is the default pacing mode.
- Adaptive output age is capped at one frame.
- No CPU frame readback is introduced.
- Existing signed Feature-18 parameter contract remains unchanged.
- Visible controller UI must not use stock tab/trackbar/checkbox/edit/combo widgets.
- Package remains overwrite-ready with one `UniversalDLSS5` top-level folder.

---

### Task 1: Pacing policy and ABI
**Files:** `include/udlss/settings.hpp`, `include/udlss/shared_control.hpp`, `include/udlss/neural_scheduler_policy.hpp`, tests.
- [ ] Add failing tests for pacing normalization/default and stale-output decisions.
- [ ] Run tests and verify failure.
- [ ] Add `FramePacingMode`, settings/profile persistence and runtime age telemetry.
- [ ] Run tests and verify pass.

### Task 2: NGX frame-matched scheduler
**Files:** `src/neural/ngx_nr.cpp`, scheduler tests/wiring tests.
- [ ] Add failing source/wiring tests requiring synchronized wait and age telemetry.
- [ ] Run tests and verify failure.
- [ ] Implement per-slot source-present IDs/timestamps, current-job waits for Synchronized, one-frame stale cap for Adaptive, existing latest-completed behavior for Asynchronous.
- [ ] Verify tests.

### Task 3: Low-confidence temporal governor
**Files:** `src/gpu/d3d11_pipeline.cpp`, tests.
- [ ] Add failing source policy test for zero-motion + synthetic-depth history reset.
- [ ] Run failing test.
- [ ] Reset Feature-18 temporal history on every frame when no trustworthy motion/depth guidance exists.
- [ ] Verify tests.

### Task 4: Direct2D/DirectWrite UI
**Files:** `src/controller/ui.cpp`, `CMakeLists.txt`, Windows wiring tests.
- [ ] Add failing wiring test requiring D2D/DWrite libraries and absence of legacy visible widgets.
- [ ] Run failing test.
- [ ] Replace visible child-control layout with a D2D/DWrite canvas UI and custom controls/pages.
- [ ] Verify tests.

### Task 5: Clean export verification
- [ ] Run full CMake/CTest suite.
- [ ] Run package verifier and no-readback audit.
- [ ] Create single-folder overwrite-ready ZIP.
- [ ] Extract exported ZIP into a clean directory and rerun full verification.
