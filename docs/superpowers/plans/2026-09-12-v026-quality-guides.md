# UniversalDLSS5 v0.2.6 Quality Guides Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Eliminate the known temporal ghosting bugs, transport verified DLSS-NR controls/masks to NRHost, and add conservative GPU-only game guide extraction.

**Architecture:** Preserve the proven external NRHost/forwarder. Fix the guide generators first, then extend the host protocol for mask/tuning/reset, then add D3D11 depth + optional adapter extraction, then expose controls/debug diagnostics.

**Tech Stack:** C++20, Win32, D3D11/D3D12, HLSL cs_5_0, NGX feature 18, MinHook, CMake/CTest.

**Spec:** `docs/superpowers/specs/2026-09-12-v026-quality-guides-design.md`

## Global Constraints
- No CPU pixel readback or screenshot capture.
- No anti-cheat/CIG/protected-process bypass.
- No NVIDIA runtime redistribution.
- Existing feature-18 forwarder path remains intact.
- Bind only verified NR parameter names.

---

### Task 1: Portable guide-quality policy and settings
**Files:** create `include/udlss/guide_quality_policy.hpp`; modify `include/udlss/settings.hpp`; tests `tests/guide_quality_policy_tests.cpp`, `tests/settings_tests.cpp`, `CMakeLists.txt`.
- [ ] Add failing tests for shortest-vector tie-break, confidence/deadzone rejection, temporal-gap reset, and new setting normalization.
- [ ] Run targeted tests and confirm RED.
- [ ] Implement policy/settings and make targeted tests GREEN.

### Task 2: HLSL anti-ghosting and mask polarity
**Files:** modify `shaders/flow.hlsl`, `shaders/motion.hlsl`, `shaders/mask.hlsl`; test `tests/windows_build_wiring_tests.cpp`.
- [ ] Add source-wiring regression for zero-motion tie preference, confidence threshold use, deadzone, and inverted ControlMask polarity.
- [ ] Confirm RED, implement shaders, confirm GREEN.

### Task 3: External-host ControlMask + NR controls + reset protocol
**Files:** modify `src/neural/external_host_protocol.hpp`, `src/neural/external_host.cpp`, `src/host/main.cpp`, `src/neural/backend.hpp`; tests `tests/external_host_policy_tests.cpp`, `tests/windows_build_wiring_tests.cpp`.
- [ ] Add failing protocol/wiring tests for mask handle, reset, depth mode, style/preset/intensity/tone/structure/skin/auto-mask/UI correction and composition controls.
- [ ] Implement shared R8 ControlMask transport and verified parameter binding.
- [ ] Ensure skipped/busy neural frames force next-frame reset.

### Task 4: D3D11 depth extraction and optional GameGuides adapter
**Files:** create `include/udlss/game_guides_api.hpp`, `include/udlss/resource_extraction_policy.hpp`, `src/gpu/d3d11_guide_extractor.hpp/.cpp`; modify pipeline/CMake; tests `tests/resource_extraction_policy_tests.cpp`, `tests/windows_build_wiring_tests.cpp`.
- [ ] Add failing portable format/scoring tests and source-wiring checks.
- [ ] Implement current-DSV extraction for non-MSAA D32/D24-compatible depth.
- [ ] Implement explicit absolute-path adapter loader and metadata contract.
- [ ] Fall back safely to synthetic far depth when extraction is unavailable.

### Task 5: Controller controls/debug diagnostics
**Files:** modify `src/controller/ui.cpp`, `include/udlss/shared_control.hpp`, profile handling and shaders as needed; tests `tests/settings_tests.cpp`, `tests/windows_build_wiring_tests.cpp`.
- [ ] Add new Neural/Temporal/Composition/Debug controls and diagnostics fields.
- [ ] Wire controls into shared settings and NRHost protocol.
- [ ] Add GPU-only debug view selection including original, NR, split, difference, motion confidence, control mask, and depth.

### Task 6: Verification/package
- [ ] Clean configure/build portable tests.
- [ ] Run all CTest tests.
- [ ] Run `tools/audit_no_readback.py`.
- [ ] Run `tools/verify_package.py`.
- [ ] Package source without NVIDIA binaries, extract it cleanly, repeat tests/audits, and compute SHA-256.
