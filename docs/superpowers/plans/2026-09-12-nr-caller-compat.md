# NR Caller Compatibility Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Route DLSS-NR signed-snippet calls through a caller-compatible forwarder and optionally enable NRHost-local RTX 20/30/40 architecture compatibility.

**Architecture:** Preserve the current external GPU transport. Add one forwarder DLL adjacent to NRHost, route snippet entry points through it, and add an opt-in MinHook detour for NVAPI architecture reporting inside NRHost only.

**Tech Stack:** C++20, Win32, D3D12, NVIDIA NGX headers/import library, NVAPI dynamic resolution, MinHook, CMake/CTest.

**Spec:** `docs/superpowers/specs/2026-09-12-nr-caller-compat-design.md`

## Global Constraints
- No proprietary NVIDIA runtime DLL in the source package.
- No CPU frame readback/screenshot path.
- Do not alter game-process security mitigations.
- `Attempt unsupported hardware` is the only switch enabling architecture compatibility.
- Blackwell is never remapped.

---

### Task 1: Compatibility wiring regression
**Files:**
- Modify: `tests/windows_build_wiring_tests.cpp`
- Modify: `src/neural/external_host_protocol.hpp`

**Interfaces:**
- Produces: IPC fields `allowUnsupportedHardware`, `realGpuArchitecture`, `reportedGpuArchitecture`, `architectureCompatibilityActive`.

- [ ] Add source-wiring assertions for the named forwarder target, wrapper exports, non-tail-call sink, host forwarder routing, MinHook architecture hook, and IPC fields.
- [ ] Run `cmake --build build-portable && ctest --test-dir build-portable --output-on-failure` and confirm the new test fails before production changes.
- [ ] Bump host IPC ABI and add the four compatibility fields.
- [ ] Re-run the targeted test after implementation.

### Task 2: Forwarder DLL
**Files:**
- Create: `src/host/nr_forwarder.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces exports: `UdlssNrFwdLoad`, `UdlssNrFwdPath`, `UdlssNrFwdInitExt`, `UdlssNrFwdPopulate`, `UdlssNrFwdCreate`, `UdlssNrFwdEvaluate`, `UdlssNrFwdRelease`, `UdlssNrFwdShutdown`, `UdlssNrFwdGetApplicationId`, `UdlssNrFwdGetSnippetVersion`.

- [ ] Build a forwarder target with output name `nvngx.dll_UniversalDLSS5_NRForwarder`.
- [ ] Resolve the staged snippet exports inside the forwarder.
- [ ] Make every NGX wrapper `__declspec(noinline)` and perform an `InterlockedExchange` after the real call to preserve a return address in the forwarder.
- [ ] Put the forwarder beside NRHost in the runtime output directory.

### Task 3: NRHost routing and RTX compatibility
**Files:**
- Modify: `src/host/main.cpp`
- Modify: `src/neural/external_host.cpp`

**Interfaces:**
- Consumes: forwarder exports from Task 2 and IPC fields from Task 1.
- Produces: host-local architecture compatibility diagnostics.

- [ ] Copy `Settings::attemptUnsupportedHardware` into shared host IPC during external backend initialization.
- [ ] Resolve/load the forwarder from NRHost's executable directory and route signed-snippet operations through it.
- [ ] Dynamically load NVAPI and resolve Initialize, EnumPhysicalGPUs and GetArchInfo.
- [ ] If compatibility is enabled and exactly one physical NVIDIA GPU reports Turing/Ampere/Ada, install a MinHook detour on GetArchInfo; preserve all real fields except the reported architecture tuple for that cached physical handle.
- [ ] Do nothing on Blackwell, disabled compatibility, failed NVAPI resolution, or ambiguous multi-GPU configurations; report the reason.
- [ ] Disable/remove the hook during host teardown.

### Task 4: Diagnostics, docs, packaging
**Files:**
- Modify: `include/udlss/shared_control.hpp`
- Modify: `src/neural/external_host.cpp`
- Modify: `src/controller/ui.cpp`
- Modify: `README.md`
- Modify: `runtime/README.txt`
- Modify: `tools/verify_package.py`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: v0.2.5 diagnostics and source package.

- [ ] Add architecture fields to runtime status and copy them from host IPC.
- [ ] Print real/reported architecture and compatibility-active state in copied diagnostics.
- [ ] Update version/docs and package verifier required sources.
- [ ] Run clean portable configure/build, full CTest, no-readback audit, strict package audit.
- [ ] Create a source ZIP, extract it to a clean directory, and repeat verification against the extracted archive.
