# Handoff Architecture — How UniversalDLSS5 Works

This document describes the current r5 tree, with emphasis on the path used by modern
D3D12 games such as Cyberpunk 2077.

## 1. High-level process topology

```text
UniversalDLSS5.exe (controller/UI)
        |
        | shared control mapping + settings/status
        v
UniversalDLSS5.Injector[32].exe
        |
        | LoadLibrary bridge + call exported BridgeStart
        v
UniversalDLSS5.Bridge[32].dll inside game
        |
        +--> renderer / Present hooks
        +--> Streamline + NGX temporal-guide hooks
        +--> D3D11 pipeline or D3D12 recovery/native transport
        |
        +-------------------- optional/required recovery -------------------+
                                                                           |
                                                                           v
                                                       UniversalDLSS5.NRHost.exe (x64)
                                                                           |
                                                                           v
                                                        NGX / Feature 18 evaluation
```

The controller and bridge communicate through `SharedControlBlock` in
`include/udlss/shared_control.hpp`. Settings are written by the controller; each bridge
publishes a `RuntimeStatus` containing renderer route, frame counters, guide diagnostics,
backend state, pipeline stages, errors, and Feature-18 results.

## 2. Controller and injection

### Controller

Key files:

- `src/controller/ui.cpp`
- `src/controller/processes.cpp`
- `src/controller/inject_client.cpp`
- `src/common/shared_control_win.cpp`

The controller selects a process, stores settings/runtime path in shared memory, launches
the matching injector, and renders diagnostics from `RuntimeStatus`.

### Injector

Key files:

- `src/injector/injector.cpp`
- `src/injector/main.cpp`

The injector opens the target process, loads the architecture-matching bridge DLL, and
then invokes the exported `UniversalDLSS5_BridgeStart` function. This is intentionally
separate from `DllMain`, avoiding substantial initialization under loader lock.

### Bridge bootstrap

Key file: `src/bridge/bridge_main.cpp`

`UniversalDLSS5_BridgeStart` creates the worker thread. The worker:

1. opens shared control;
2. waits through the safe-attach delay;
3. installs DXGI/D3D hooks;
4. installs/probes D3D9/OpenGL/Vulkan compatibility hooks;
5. waits for renderer activity / detach requests;
6. periodically probes late-loaded compatibility modules;
7. removes hooks quiescently on detach/exit.

## 3. Renderer interception and routing

Key file: `src/bridge/dxgi_hooks.cpp`.

DXGI hooks cover Present/Present1, resize/color/HDR events, swapchain creation, and a
D3D12 `ExecuteCommandLists` hook. Swapchain state lives in `SwapCtx`.

The D3D12 path gathers queue evidence rather than blindly trusting any queue. Route
selection uses policy helpers under `include/udlss/*route_policy*.hpp`.

The principal modern routes are:

- **D3D11 native** — `D3D11Pipeline` processes the actual D3D11 backbuffer.
- **D3D12 native/exact** — D3D11On12 wraps game resources when the presentation queue is
  known strongly enough and lifetime rules allow it.
- **Modern D3D12 recovery** — copies the backbuffer into injector-owned resources and
  uses an isolated D3D11/D3D12 transport plus external NRHost. This is the Cyberpunk
  route in the current diagnostics.

r5 Present rules:

- `DXGI_PRESENT_TEST` is never used for neural/backbuffer mutation.
- `DXGI_PRESENT_DO_NOT_WAIT` is not pre-mutated.
- nested `Present1 -> Present` dispatch is counted once;
- successful displayed presents are counted after successful `S_OK` results;
- per-swapchain processing is guarded against concurrent mutation;
- `QueryPerformanceCounter` is used for displayed-FPS estimation.

If a mutation attempt is followed by a non-consuming/failed Present, the swapchain enters
suppression/resynchronization so the next clean Present can re-establish a sane frame
boundary before neural processing resumes.

## 4. D3D11 processing pipeline

Key files:

- `src/gpu/d3d11_pipeline.cpp/.hpp`
- `src/gpu/d3d11_guide_extractor.cpp/.hpp`
- `src/gpu/d3d11_resource_tracker.cpp/.hpp`
- `src/gpu/d3d11_camera_tracker.cpp/.hpp`
- `src/gpu/nv_optical_flow.cpp/.hpp`
- `shaders/*.hlsl`

`D3D11Pipeline::process` is the central image/guide path. In simplified order it:

1. probes explicit/game guides;
2. ensures working textures/shaders/backend;
3. copies/converts the current color frame;
4. prepares real game depth if usable, otherwise synthetic far depth;
5. chooses motion route;
6. builds control/mask information;
7. calls the neural backend;
8. post-composites neural output with the current scene/UI protections;
9. writes the result back to the backbuffer and advances local history.

Motion-route priority in Auto is broadly:

```text
trusted adapter/native game motion
    -> tracked native candidate (when safe/known)
    -> camera + real depth reprojection
    -> NVIDIA Optical Flow fallback
    -> safe fallback / zero motion
```

A motion texture is only treated as adapter-native when its convention is known.
Unknown convention is intentionally rejected because the wrong direction/scale can create
severe temporal corruption.

The post shader (`shaders/post.hlsl`) blends the neural result with current-frame content
and UI protection. This is why a stale neural output historically appeared as an old world
frame with some current UI/text visible through it.

## 5. Temporal-guide capture

Key files:

- `src/gpu/game_temporal_guides.cpp/.hpp`
- `include/udlss/provider_snapshot_policy.hpp`
- `include/udlss/guide_candidate_policy.hpp`

### Providers

The system attempts to capture exact resources already supplied by the game to:

- NVIDIA Streamline (`slSetTagForFrame`, `slSetTag`, `slSetConstants`, `slEvaluateFeature`)
- NGX D3D12/D3D11 evaluate calls
- explicit game adapters / safe tracker paths where applicable

### Capture modes

The recovery system distinguishes normal/full capture from recovery snapshot behavior.
Late-attached D3D12 recovery must not simply hold transient game resources until Present.

### Streamline r5 hook strategy

r5 scans loaded modules for exported Streamline entry points and supports up to **eight
independent hook targets per Streamline API**. This matters because games may have
interposers/forwarders/plugins exposing separate copies of the same API.

Diagnostics expose:

```text
SL tagF=<slSetTagForFrame calls>
   tag=<slSetTag calls>
   const=<slSetConstants calls>
   eval=<slEvaluateFeature calls>
   items=<ResourceTag items observed>
   D=<depth tags>
   M=<motion-vector tags>
   null=<tags with null native resource>
   A=<accepted captures>
   S=<post-submit owned snapshots>
   R=<owned snapshot reads>
   inst=<four-bit installed API bitmap>
NGX=<D3D12 evaluate-hook hits>
```

These counters are important because a generic “Streamline hit” did not tell us whether
the game was actually calling the resource-tag APIs.

### Volatile D3D12 resources

The current safe design is:

```text
provider tag / evaluate
      |
      v
pending provider record keyed by command-list identity
      |
      v
hooked ExecuteCommandLists sees matching provider list
      |
      +--> submit original provider command list
      |
      +--> immediately submit injector-owned copy list on SAME queue
      |
      +--> signal private snapshot fence
      v
injector-owned depth/MV snapshot
      |
      +--> promoted only after private fence completion
      v
recovery consumer
```

The bridge deliberately splits a batch into ordered one-list submissions only while a
provider snapshot is pending. Queue order supplies the ordering; it does **not** add an
extra game cross-queue wait and does not modify the game's command list.

Completed snapshots have generation/consumption tracking so slots are not reused while a
consumer can still be reading them.

## 6. D3D12 native/recovery transport

Key files: `src/gpu/d3d12_on12.cpp/.hpp`.

### Native/exact route

When queue/lifetime evidence is strong, resources may be wrapped through D3D11On12 and
processed without the isolated recovery path.

### Modern recovery route

Recovery maintains injector-owned color resources and an isolated D3D11 device on the
same adapter. It copies the swapchain image to owned storage, runs the D3D11 processing
pipeline, then copies/composites the result back.

Temporal-guide consumption:

1. `snapshotGameTemporalGuides()` retrieves a fresh capture / completed owned snapshot.
2. r5 tries `openOwnedSnapshotDirect()` for shareable injector-owned snapshots.
3. If direct opening is unsupported for that descriptor, `stageRecoveryGuide()` is the
   fallback copy/open path.
4. The resulting D3D11 texture is passed as an external `GuideProbeResult` to
   `D3D11Pipeline::process`.
5. Captures are consumed/released only after the recovery D3D11 work that used them has
   been proven complete.

The provider guide policy is intentionally less restrictive for exact Streamline/NGX
provider resources than for heuristic tracker candidates; DLSS internal resolution can
be significantly below output resolution.

## 7. External x64 NRHost

Key files:

- `src/neural/external_host.cpp`
- `src/neural/external_host_protocol.hpp`
- `src/host/main.cpp`
- `src/host/nr_forwarder.cpp`

The bridge-side backend creates GPU-shareable D3D11 textures and a shared D3D11 fence,
then duplicates NT handles into `UniversalDLSS5.NRHost.exe`. Control data lives in a
session-specific shared memory block with Win32 events.

Per neural request:

```text
bridge D3D11:
  copy color/motion/depth -> shared textures
  signal inputFenceValue
  publish frameSeq + parameters
  signal frameEvent

NRHost D3D12:
  wait shared fence input value
  evaluate Feature 18 (NGX core or signed-snippet fallback)
  restore output/common states
  signal outputFenceValue
  publish completedSeq/enqueuedSeq

bridge D3D11 r5:
  require completedSeq == requested seq (or exact-ready policy)
  require shared GPU fence >= requested outputFenceValue
  only then copy/composite the NRHost output
  otherwise pass through current frame and retire late result later
```

This exact-sequence + exact-fence rule is the r5 stale-frame fix. An output that belongs
to an older request must never be used as the result for a newer game frame.

Frame pacing budgets are policy-driven in `include/udlss/external_host_policy.hpp`.
Synchronized mode may wait longer for the exact result; adaptive/nonblocking modes favor
passing through the current frame rather than stalling Present.

## 8. Feature-18 host path

`src/host/main.cpp` owns the x64 D3D12 device/queue and the actual Feature-18 feature.
It can use the normal NGX core dispatch or the signed-snippet fallback, depending on what
the selected runtime exposes. It binds color, output, motion, depth, optional control
mask, motion scales, depth inversion, reset flag, style and NR parameters.

Pass 1 is temporal. Additional configured passes use a separate reset-only refinement
feature and scratch ping-pong so repeated refinement does not incorrectly advance temporal
history multiple times for one game frame.

## 9. Legacy / compatibility renderers

Key directory: `src/compat/` plus bridge hooks for D3D9/OpenGL/Vulkan.

Compatibility frontends normalize older/unusual renderers into a canonical D3D11 surface,
then reuse the common D3D11 neural pipeline. Native modern paths do not execute this work.

## 10. Diagnostics

`RuntimeStatus` is defined in `include/udlss/shared_control.hpp`; stage labels are defined
in `include/udlss/runtime_diagnostics.hpp`. The controller formats the human-readable
report in `src/controller/ui.cpp`.

For temporal-guide debugging, always capture the report **while State=Processing** and the
bridge is still attached. Detach clears live counters/state.
