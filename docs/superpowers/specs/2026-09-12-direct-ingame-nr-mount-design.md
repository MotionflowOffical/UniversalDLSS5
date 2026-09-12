# UniversalDLSS5 v0.2.8 Direct In-Game NR Mount Design

## Scope

Add an in-process neural-rendering integration path for games that do not ship DLSS or Streamline. Preserve the working v0.2.7 external-host path as a fallback and keep all image data GPU-only.

## Goals

- Run DLSS-NR/feature 18 in the target game's render process when safe and supported.
- Initialize a private same-adapter D3D12 execution context for D3D11 games.
- Add an optional Streamline feature-1004 path when the user supplies the required Streamline plugin/runtime files.
- Reuse the already-working signed feature-18 forwarder path when Streamline NR is unavailable.
- Acquire higher-quality temporal guides before Present: game-native motion when positively identified, camera+depth reconstruction when matrices are available, NVIDIA Optical Flow fallback, HLSL flow last.
- Do not use CPU pixel readback, screenshots, anti-cheat bypass, engine-memory scanning, or stealth DLL proxying.

## Architecture

### Controller / Injector

The existing controller and injector remain responsible for target selection and explicit injection of `UniversalDLSS5.Bridge.dll`.

### Bridge

`UniversalDLSS5.Bridge.dll` gains four isolated subsystems:

1. **RenderResourceTracker**
   - D3D11 texture/view lifecycle metadata.
   - Tracks render/depth targets and SRV usage.
   - Scores same-resolution RG16F/RG32F resources as motion candidates.
   - Never reads texture pixels on CPU.

2. **ShaderConstantTracker**
   - Hooks D3D11 shader creation and constant-buffer updates/bindings.
   - Uses `D3DReflect` metadata when available to discover likely current/previous view/projection matrices.
   - Copies only small constant-buffer data when the game updates those buffers; no framebuffer/readback path.
   - Exposes a conservative camera-matrix snapshot only after validation.

3. **GuideComposer**
   Motion-source priority:
   1. Explicit GameGuides adapter native motion.
   2. Positively identified game-native velocity resource.
   3. Camera+real-depth GPU reconstruction.
   4. NVIDIA Optical Flow API.
   5. Existing HLSL flow fallback.
   It also manages history reset and guide provenance diagnostics.

4. **InGameNrMount**
   - Owns private same-adapter D3D12 device/queue for D3D11 sources.
   - Opens bridge-produced shared GPU resources directly.
   - Preferred NR backends:
     1. Streamline DLSS-NR feature 1004 when a valid `sl.dlss_nr.dll` stack is supplied.
     2. Existing signed feature-18 route through `nvngx.dll_UniversalDLSS5_NRForwarder.dll`.
     3. External NRHost fallback.
   - Evaluation is synchronized to the source frame and never requires CPU pixel transfer.

## Streamline Mount

The mount may load user-supplied, authorized runtime files from UniversalDLSS5's runtime directory:

- `sl.interposer.dll`
- `sl.common.dll`
- `sl.dlss_nr.dll`
- `nvngx_dlssnr.dll`

No NVIDIA binary is distributed in the source package.

When available, the bridge initializes Streamline itself:

- `slInit` with feature 1004 requested.
- `slSetD3DDevice` on the mount's D3D12 device.
- one `slGetNewFrameToken` per rendered frame.
- `slSetConstants` once per viewport/frame, including reset state.
- tag color input/output, depth, and motion resources.
- evaluate `kFeatureDLSS_NR`.

If plugin initialization is unavailable, the bridge falls back to direct signed feature 18 without changing capture/guide generation.

## Native Motion Discovery

A resource becomes an automatic native-motion candidate only if all structural checks pass:

- same or near render resolution;
- 2-channel float/snorm format appropriate for velocity;
- regularly written before post-processing;
- subsequently sampled by temporal/post passes;
- lifetime stable across frames;
- no depth/shadow-map usage;
- configurable/manual confirmation remains available.

The first implementation does not silently select weak candidates. Diagnostics expose candidate IDs and scores so a per-game profile can lock a candidate.

## Camera + Depth Reconstruction

When validated current/previous view/projection matrices and real depth are available, a compute shader reconstructs current-to-previous pixel motion:

1. current pixel + depth -> current clip/view position;
2. transform to previous camera;
3. project into previous clip/screen;
4. output `previousPixel - currentPixel` in pixel units.

An explicitly identified object-motion texture may override camera motion on moving-object pixels.

## NVOFA Fallback

The existing NVIDIA Optical Flow subsystem becomes the primary screen-derived fallback. Prefer forward/backward consistency and cost/confidence when the available NVOF API/SDK supports them. Existing HLSL flow remains a last-resort path only.

## Safety / Compatibility

- No anti-cheat, DRM, CIG, protected-process bypass.
- Protected/unsupported targets remain skipped/report-only.
- No arbitrary process-memory scanning for camera data.
- No proxy-DLL masquerading (`dxgi.dll`, `d3d11.dll`, etc.).
- No CPU framebuffer transfer or staging readback.
- Streamline/NVIDIA binaries are user supplied and excluded from packages.

## Diagnostics

Add:

- NR execution location: `IN-GAME` / `EXTERNAL HOST`.
- NR backend: `Streamline 1004` / `Signed feature 18` / fallback.
- motion source: adapter native / game-native candidate / camera+depth / NVOFA / HLSL.
- selected native-motion candidate ID/score.
- depth source and convention.
- camera matrices: current view, previous view, projection availability.
- temporal-history validity/reset reason.
- Streamline mount stages separately from NGX snippet stages.

## Testing

Portable policy/wiring tests must verify:

- direct-mount route selection and fallback ordering;
- no readback/screenshot APIs introduced;
- native-motion candidate scoring rejects obvious non-motion resources;
- camera reconstruction math on synthetic matrices/depth;
- matrix-name/reflection classification;
- one unique frame token per Streamline frame in wiring;
- Streamline resources are optional and proprietary DLLs are not packaged;
- external host remains a functional fallback;
- v0.2.7 feature-18 forwarder activation path is preserved.

Windows runtime validation remains required for actual D3D11/D3D12/Streamline/NGX execution.
