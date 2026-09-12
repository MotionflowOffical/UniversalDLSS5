# UniversalDLSS5 Fully Capable Prototype Design

## Goal
Build a Windows graphics interception application that can attach to ordinary GPU-rendered executables (including browser GPU processes and 2D games), intercept their DXGI swapchain before Present, synthesize temporal guides entirely on the GPU when the application has no native motion vectors, evaluate NVIDIA Streamline DLSS Neural Rendering when the runtime is available, and present the processed GPU texture without desktop screenshots or CPU pixel readback.

## Non-negotiable requirements
- Pixels stay on the GPU during the rendering path. No BitBlt, Windows Graphics Capture, staging/readback texture, Map of frame pixels, PNG/JPEG encode, or CPU image copy.
- Controller exposes live tuning controls and persists per-application profiles.
- DXGI/D3D11 is a first-class working path; D3D12 uses D3D11On12 over the application's actual command queue to reuse the same GPU pipeline.
- Controller can attach a selected process tree so Chromium/Firefox-style GPU child processes are covered.
- Protected/system/anti-cheat targets are not bypassed. Failed injection is reported and skipped.
- Streamline and nvngx DLSS NR runtime binaries are supplied by the user in `runtime/`; they are not redistributed.
- The NR backend uses only public Streamline ABI available from the pinned SDK. It queries feature requirements at runtime instead of inventing hidden NR option structures.

## Architecture
`UniversalDLSS5.exe` enumerates applications, manages profiles and live sliders, creates a named shared-memory control block, and invokes the architecture-matching injector. `UniversalDLSS5.Bridge.dll` is injected into matching processes in the selected process tree and hooks DXGI Present/Present1/ResizeBuffers plus swapchain creation. The bridge recognizes D3D11 or D3D12 swapchains and runs an in-process GPU pipeline immediately before Present.

D3D11 uses the native device/context. D3D12 records the command queue at swapchain creation and uses D3D11On12 to wrap the current backbuffer. The common GPU pipeline copies the input to private shader resources, synthesizes motion vectors from current/history frames, computes confidence and UI/text protection masks, calls the selected neural backend, composites protected detail, then writes the result back to the wrapped/native backbuffer.

## DLSS 5 / Streamline backend
The Streamline backend dynamically loads `sl.interposer.dll`, requests feature `kFeatureDLSS_NR`, calls `slSetD3DDevice`, asks `slGetFeatureRequirements` for required buffer tags, and supports public tags including `kBufferTypeUpliftInputColor`, `kBufferTypeUpliftOutputColor`, and `kBufferTypeMotionVectors`. If the installed plugin requests an unavailable mandatory guide, the backend reports the exact missing tag and bypasses safely rather than fabricating undocumented ABI.

Live controls influence guide synthesis, motion-vector normalization, temporal policy, protection/composite masks, exposure/sharpness post-processing, queue depth, and any public backend option discovered through the installed SDK. Hidden NR-specific setters are never guessed.

## Tuning controls
- Enable/disable processing
- Backend selection (Streamline DLSS 5 / passthrough)
- Motion source (Auto / synthesized optical flow / zero motion)
- Sharpness
- Exposure
- Temporal strength
- Motion scale and Y inversion
- Flow search radius and downsample factor
- Flow confidence threshold
- Disocclusion threshold
- Text protection
- UI protection
- Edge threshold
- Control mask strength
- Reactive strength
- History clamp
- HDR policy
- Latency mode / frames in flight
- Secondary swapchain processing
- D3D11On12 compatibility toggle

## Failure behavior
Every hook and backend call is fail-open: if initialization, resource creation, shader compilation, Streamline support, or feature evaluation fails, the original frame is presented. Errors are written to a per-process log and surfaced in the controller shared status block.

## Validation
- Cross-platform unit tests cover settings normalization/profile encoding.
- Windows build contains a deterministic DX11 test target with moving sprites/text to verify interception and live settings.
- Bridge diagnostics assert that no CPU readback path exists.
- Build script produces x64 controller/bridge/injector and an optional x86 injector/bridge for 32-bit targets.
