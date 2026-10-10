# Handoff File Map

## Top level

- `CMakeLists.txt` — targets, portable tests, Windows binaries, optional NVIDIA features.
- `BUILD_WINDOWS.bat` — standard Windows source build.
- `BUILD_RELEASE.bat` — release/package build.
- `README.md` — public project overview.
- `TEMPORAL_GUIDE_HOTFIX.txt` — cumulative r5 temporal/recovery change summary.
- `LICENSE`, `NOTICE`, `THIRD_PARTY.md` — licensing.

## `include/udlss/`

Mostly platform-independent policy/state helpers. Important files:

- `settings.hpp` — user/runtime settings and presets.
- `shared_control.hpp` — controller/bridge shared memory ABI and `RuntimeStatus`.
- `runtime_diagnostics.hpp` — pipeline-stage enum and formatted stage state.
- `d3d12_route_policy.hpp`, `d3d12_recovery_policy.hpp`, `present_queue_policy.hpp` — queue/route decisions.
- `present_processing_policy.hpp` — r4/r5 Present safety/accounting decisions.
- `provider_snapshot_policy.hpp` — owned provider snapshot priority/consumption rules.
- `guide_candidate_policy.hpp` — guide extent/aspect/provider acceptance rules.
- `motion_route_policy.hpp` — native/camera/NVOF/HLSL/zero selection.
- `external_host_policy.hpp` — NRHost sequence/fence readiness and output wait budgets.
- `game_guides_api.hpp` — optional explicit game-guide adapter ABI.

## `src/controller/`

Desktop controller/UI, process picker, settings, diagnostics, runtime import and attach.

- `ui.cpp` — controller UI + diagnostic text formatting.
- `inject_client.cpp` — selects x86/x64 injector/bridge and launches injection.
- `runtime_importer.cpp` — NVIDIA runtime import/setup.

## `src/injector/`

Remote process injection and bridge-start invocation.

- `injector.cpp` — remote LoadLibrary / bridge bootstrap and injection logging.

## `src/bridge/`

Code that runs inside the target process.

- `bridge_main.cpp` — bridge worker lifecycle and detach behavior.
- `dxgi_hooks.cpp` — D3D11/D3D12 swapchain hooks, D3D12 queue evidence, Present processing, r5 Present accounting.
- `d3d9_hooks.cpp` — D3D9 attach/compat hooks.
- `opengl_hooks.cpp` — OpenGL compatibility hook path.
- `vulkan_hooks.cpp` — Vulkan compatibility hook path.
- `hook_lifecycle.*` — quiescence / forwarding-only lifecycle safety.
- `attach_logger.*` — attach/crash-stage logs.

## `src/gpu/`

Renderer-independent/common GPU processing and temporal-guide extraction.

- `d3d11_pipeline.cpp` — main color/guide/neural/post-composite pipeline.
- `d3d11_guide_extractor.cpp` — turns provider/tracker data into usable D3D11 guide textures.
- `d3d11_resource_tracker.cpp` — heuristic D3D11 temporal-resource tracking.
- `d3d11_camera_tracker.cpp` — camera matrix tracking.
- `d3d12_resource_tracker.cpp` — D3D12 semantic tracking used where safe.
- `game_temporal_guides.cpp` — Streamline/NGX hooks, pending providers, post-submit owned snapshots, diagnostics.
- `d3d12_on12.cpp` — native D3D11On12 and modern D3D12 recovery transport.
- `nv_optical_flow.cpp` — NVIDIA Optical Flow fallback.
- `swapchain_color.hpp` — SDR/HDR color-path decisions.

## `src/neural/`

Neural backend abstraction and implementations.

- `backend.hpp` — `FrameResources` and backend interface.
- `external_host.cpp` — bridge-side x64 NRHost transport; r5 exact output sequence/fence verification.
- `external_host_protocol.hpp` — shared-memory/event/fence protocol.
- `ingame_nr.cpp`, `ngx_nr.cpp`, `streamline_nr.cpp` — in-process neural routes.
- `passthrough.cpp` — safe fallback.
- `nvidia_driver_win.cpp` — driver detection/risk handling.

## `src/host/`

- `main.cpp` — x64 NRHost D3D12 device/queue, resource opening, Feature-18 creation/evaluation, output fence signal.
- `nr_forwarder.cpp` — runtime forwarding support.

## `src/compat/`

Legacy/alternate renderer normalization into the common processing path.

- `d3d9_frontend.*`
- `d3d10_frontend.*`
- `opengl_frontend.*`
- `vulkan_frontend.*`
- `canonical_surface.*`
- `compat_dispatch.*`
- `legacy_copy_ring.*`

## `shaders/`

D3D11 compute/post shaders for conversion, optical-flow processing, motion conversion,
depth conversion, masks, post-composite and blit.

`post.hlsl` is especially important when debugging stale-frame appearance because it mixes
neural output with current-frame content/UI protection.

## `tests/`

Portable policy tests and source-wiring regression tests. r5-specific coverage includes:

- `external_host_policy_tests.cpp`
- `present_processing_policy_tests.cpp`
- `present_processing_wiring_tests.cpp`
- `provider_snapshot_policy_tests.cpp`
- `provider_snapshot_shared_resource_wiring_tests.cpp`
- `r5_stale_frame_and_streamline_wiring_tests.cpp`
- updated v0.4.3/v0.4.4 recovery/NGX/Streamline wiring tests

Many wiring tests intentionally inspect source strings/structure; they are regression
checks, not substitutes for a Windows D3D12 runtime test.

## `tools/`

- `verify_package.py` — expected package/source invariants.
- `audit_no_readback.py` — checks that the performance path has not regressed into GPU->CPU readback.
- additional release/runtime helper scripts.
