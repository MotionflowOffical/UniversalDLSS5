# Real game guides and staged attach diagnostics

UniversalDLSS5 now prefers temporal data supplied by the game itself before using reconstructed or synthetic guides.

## Guide source order

The bridge selects the highest-confidence compatible source available for the current frame:

1. `UniversalDLSS5.GameGuides.dll` explicit game adapter.
2. Motion/depth supplied by the game to NVIDIA NGX/DLSS.
3. Motion/depth tagged by the game through NVIDIA Streamline.
4. D3D12 temporal resource tracker (scene depth can be selected automatically; unknown motion-vector encodings stay diagnostic-only).
5. D3D11 temporal resource tracker.
6. Camera-matrix + real-depth reconstruction.
7. NVIDIA Optical Flow, when available and selected by policy.
8. Safe zero motion.

The bridge suppresses guide capture while UniversalDLSS5 performs its own NGX/Streamline calls, so its private Feature-18 resources cannot be mistaken for game resources.

### Dynamic resolution

Real motion/depth guides do not have to match presentation resolution. Compatible internal resolutions are retained and the conversion shaders resample them to output resolution. Streamline resource extents and native formats are preserved when the game provides them.

### Motion safety

A generic texture that merely looks like a velocity buffer is not automatically trusted. D3D12 heuristic motion candidates are scored and displayed in diagnostics, but they are not fed into Feature 18 until the vector direction/units are known. Game NGX/Streamline inputs have priority because their motion convention and scale can be obtained from the temporal integration itself.

## Hitman / difficult D3D12 attach diagnostics

Attach is staged rather than immediately bringing up every graphics subsystem:

1. Bridge thread starts.
2. Initial attach delay completes.
3. Core DXGI hooks are installed.
4. A stable Present stream is observed.
5. A real D3D12 DIRECT queue is captured.
6. The swapchain remains stable for the safe-attach window.
7. Guide hooks and trackers are installed on the real game device.
8. D3D11On12 and Feature 18 initialize.

Each injected bridge writes:

```text
%LOCALAPPDATA%\UniversalDLSS5\logs\attach-<PID>.log
```

The controller's **Diagnostics** page has **Open attach logs**. If a game still crashes during attachment, attach that log when reporting the problem. The last completed stage narrows the failure to hook bootstrap, queue capture, guide tracking, D3D11On12, or neural initialization.

## What to look for in Diagnostics

When game-provided guides are available, expect lines similar to:

```text
Game guide adapter: NGX game evaluation
Game motion guide: 1707 x 960, confidence=96%
Game depth guide: 1707 x 960, confidence=96%
```

or:

```text
Game guide adapter: Streamline game tags
```

If only a heuristic D3D12 velocity candidate is found, it may appear as:

```text
Native motion candidate: id=..., score=...
```

without becoming the active motion path. That is intentional: rejecting an unknown convention is safer than introducing severe ghosting from incorrectly interpreted vectors.

## Theme persistence

System / Light / Dark is stored as a controller-wide preference at:

```text
%LOCALAPPDATA%\UniversalDLSS5\controller.ini
```

Changing the selected game does not load a different theme, and the selected appearance survives controller restarts.
