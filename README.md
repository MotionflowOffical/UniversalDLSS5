# UniversalDLSS5

**Current version:** v0.3.1

> **Experimental graphics-injection software.** UniversalDLSS5 injects its bridge into a user-selected process and hooks DXGI/D3D rendering APIs. Some antivirus/EDR products may flag those techniques heuristically. Verify the GitHub release/hash/source and investigate warnings rather than disabling security software. Avoid anti-cheat, DRM-protected, protected system, or other processes where third-party injection is not permitted.

## Quick start for GitHub releases

1. Install `UniversalDLSS5-Setup-x64.exe` or extract `UniversalDLSS5-Portable-x64.zip`.
2. Download and extract an official NVIDIA DLSS/Streamline developer package containing the DLSS Neural Rendering runtime you are authorized to use. Start with NVIDIA's DLSS page (`https://developer.nvidia.com/rtx/dlss`), Streamline download page (`https://developer.nvidia.com/rtx/streamline/get-started`), or NVIDIA's Streamline release packages (`https://github.com/NVIDIA-RTX/Streamline/releases`).
3. Start `UniversalDLSS5.exe`, open **Application**, then use **Import NVIDIA SDK...** and select the extracted NVIDIA SDK root.
4. The importer copies only approved x64, NVIDIA-signed Neural Rendering/Streamline DLLs into the selected `runtime` folder. `nvngx_dlssnr.dll` is required; `sl.interposer.dll`, `sl.common.dll`, and `sl.dlss_nr.dll` are optional.
5. Press **Check runtime**, select a running application, then **Attach**.

The installer and portable release deliberately do **not** include NVIDIA proprietary runtime binaries. See [`docs/NVIDIA_RUNTIME_SETUP.md`](docs/NVIDIA_RUNTIME_SETUP.md) for the detailed setup and security notes.

### v0.3.1 HDR / color-space compatibility

- **Actual swapchain detection:** `Auto` follows the game's DXGI color space and backbuffer format rather than the monitor capability or Windows HDR toggle. HDR10 PQ/BT.2020 and linear scRGB are detected explicitly.
- **HDR-preserving neural proxy:** HDR is decoded to an FP16 linear working image, mapped to a bounded paper-white proxy for Feature 18, then the bounded neural change is applied back to the original high-range image before re-encoding to the game's HDR contract.
- **Mode-change safety:** fullscreen, `SetColorSpace1`, HDR metadata, `ResizeBuffers`, and `ResizeBuffers1` changes reset the color/neural state and require several stable Presents before processing resumes. This is intended to handle games that switch display mode during startup or Alt-Tab.
- **Driver compatibility guard:** exact NVIDIA user-mode driver file versions known to be risky for the direct Feature-18 D3D12 route are blocked from that direct route by default; Streamline/external-host fallback remains available. **Attempt unsupported hardware** can explicitly override the guard.
- **Device-loss evidence:** failed Presents now log both the DXGI HRESULT and `GetDeviceRemovedReason()` so a remaining Hitman failure can be distinguished from a CPU-side hook failure.
- See [`docs/V0.3.1_HDR_HITMAN_COMPAT.md`](docs/V0.3.1_HDR_HITMAN_COMPAT.md).

### v0.3.1 D3D12 queue/backbuffer safety

- Late-injected D3D12 games no longer use the most recently observed same-device DIRECT queue. UniversalDLSS5 tracks command lists that transition the swapchain backbuffers and requires repeated evidence that those lists execute on the same DIRECT queue before processing begins.
- If queue ownership cannot be proven, the bridge stays in passthrough rather than risking a crash.
- The D3D12 game backbuffer is no longer wrapped directly with D3D11On12. It is copied to an injector-owned GPU color resource, processed there, and copied back on the proven presentation queue.
- Serious exceptions are annotated with a fine crash-stage marker in the attach log so failures can be distinguished between queue proof, pre-copy, D3D11On12 acquire/release, neural processing, post-copy, and DXGI Present.
- See [`docs/V0.3.1_D3D12_QUEUE_SAFETY.md`](docs/V0.3.1_D3D12_QUEUE_SAFETY.md).

## Real game motion/depth guides and safer attach

- **Game-supplied guides first, with lifetime validation:** when a game submits DLSS/NGX or Streamline temporal inputs, the bridge captures depth/motion metadata, scale, reset/camera-cut state and resource extent. On D3D12, a game-owned resource is consumed at Present only when the provider explicitly guarantees that it remains valid through Present; transient NGX inputs are reported for diagnostics but are not wrapped later after their lifetime may have ended. UniversalDLSS5 suppresses capture around its own NGX/Streamline calls so it cannot mistake its generated resources for game resources.
- **D3D12 guide tracking:** D3D12 resource-barrier activity is tracked after the primary swapchain becomes stable. Conservative scoring can automatically select a stable scene-depth resource at internal/dynamic resolution. Heuristic D3D12 motion candidates remain diagnostic-only unless their convention is known; an unknown velocity encoding is safer to reject than to feed incorrect motion into Feature 18.
- **D3D11 dynamic-resolution tracking:** native depth/motion candidates no longer need to exactly match the presentation resolution. Candidate stability, aspect ratio, write/read behavior and internal render size are scored across frames, then guide shaders resample them to output resolution.
- **Guide priority:** explicit GameGuides adapter -> game-supplied guides with a safe lifetime -> D3D12 tracker -> D3D11 tracker -> camera/depth reconstruction -> optical flow -> zero motion. Ephemeral D3D12 NGX/Streamline inputs remain visible in diagnostics instead of being touched after their declared lifetime.
- **Safe attach:** graphics-heavy D3D12 initialization is delayed until a real direct queue and a stable primary Present stream have been observed. The injector now writes `%LOCALAPPDATA%\UniversalDLSS5\logs\inject-<PID>.log` before loading any bridge code, and each started bridge writes a staged `attach-<PID>.log` in the same folder. The **Diagnostics -> Open attach logs** action opens that folder. If a title such as Hitman fails before the bridge starts, use the injector log; if it fails later, the attach log shows the last completed graphics/neural stage.
- **Global appearance:** System/Light/Dark is a controller preference rather than a per-game profile. Switching target applications or restarting the controller preserves the selected appearance.

Diagnostics now report the provider plus motion/depth guide resolution and confidence when a real guide is selected. Guide capture is intentionally conservative: availability depends on the game's integration and unknown motion-vector conventions are not guessed.
See [`docs/GAME_GUIDES_SAFE_ATTACH.md`](docs/GAME_GUIDES_SAFE_ATTACH.md) for guide-source behavior, Hitman-style attach logging, and troubleshooting.

## Stable renderer ownership, scheduler, multipass, and controller UI update

- **Primary-renderer election:** all injected bridge statuses are scored by render area, API, process role, activity and plausible present rate. A 2.5-second freshness grace plus hysteresis prevents helper/overlay swapchains from taking ownership because they publish more frequently.
- **Single active renderer:** the controller publishes the elected PID through shared control. Other injected processes remain observable but skip neural work until elected.
- **Frame-matched synchronized pacing (default):** the bridge waits for the Feature-18 result belonging to the current Present before compositing it. Expensive 2x-4x processing lowers delivered FPS instead of replaying stale neural frames and producing long trails.
- **Adaptive / asynchronous alternatives:** Adaptive may reuse at most a one-frame-old completed result before synchronizing; Asynchronous keeps the maximum-throughput latest-completed policy for users who explicitly prefer it. Diagnostics expose output age and pacing-wait time.
- **Soft GPU backpressure:** direct Feature 18 uses a 3-slot ring that can grow to 8 slots. Queue pressure is scheduling state rather than a neural failure, so it does not create the previous Feature-18 failure/reset flicker.
- **Per-slot shared resources:** each in-flight slot owns color/output/scratch/motion/depth/control resources. Later Presents cannot overwrite resources still consumed by D3D12.
- **Weak-guide history protection:** when the only guides are zero motion + synthetic depth with no trusted camera/native motion, frame-to-frame Feature-18 history is reset instead of accumulating unsupported temporal geometry.
- **Neural passes 1x-4x:** pass 1 is the normal Feature-18 evaluation. Passes 2-4 use a separate reset-only same-frame refinement feature and ping-pong resources, and all requested passes remain one frame-matched job. If the second feature is rejected by a driver/runtime, the primary path safely remains at 1x.
- **Lower-overhead controller:** process-tree discovery/reinjection runs on a background `std::jthread` at a multi-second cadence; the 500 ms status UI performs shared-memory reads only and redraws when live data changes.
- **Direct2D/DirectWrite controller:** all visible controls are custom-rendered on one DPI-aware canvas with rounded controls, responsive scrolling, anti-aliased text, and System / Light / Dark neutral themes. Stock Win32 tabs, trackbars, combo boxes, and multiline diagnostics controls are not used.


## v0.2.8 direct in-game NR mount

- **New default:** `Direct in-game NR (recommended)` runs the neural integration inside the target render process instead of treating the game as only a finished-frame source.
- **Non-DLSS games supported:** the bridge creates its own same-adapter D3D12 neural execution context for D3D11 games; the game does not need to ship DLSS or Streamline.
- **Backend order:** authorized Streamline feature 1004 when `sl.interposer.dll` + `sl.common.dll` + `sl.dlss_nr.dll` are supplied, then the proven signed feature-18 forwarder path, then automatic external-NRHost fallback.
- **Motion-source order (Auto):** explicit GameGuides native motion -> detected game-native velocity -> camera matrices + real depth -> NVIDIA Optical Flow -> safe zero motion. The coarse HLSL block matcher is available only through the explicit experimental Optical Flow mode.
- **D3D11 tracking:** shader reflection can recognize `_CameraMotionVectorsTexture`; the bridge also tracks stable internal-resolution depth/motion candidates across the frame and current/previous view-projection candidates from constant buffers.
- **Camera reconstruction:** `camera_motion.hlsl` creates current->previous pixel motion from real depth plus validated current/previous view-projection matrices.
- **No CPU frame path:** color/depth/motion/control resources remain on GPU. No screenshot capture, framebuffer staging readback, WGC, BitBlt, or arbitrary process-memory scanning is added.
- **External host preserved:** the v0.2.7 NRHost path remains available both as an explicit backend and as an automatic fallback when the in-game mount cannot initialize.
- **Honest limitation:** game-supplied NGX/Streamline motion and depth can now be captured on D3D11 and D3D12. Generic D3D12 scene-depth discovery is also available, but heuristic D3D12 motion remains diagnostics-only until its vector convention is known; UniversalDLSS5 deliberately refuses to guess an unknown velocity encoding.

## Important design rule: no screenshots

The frame path never uses desktop/window capture, GDI screenshots, D3D staging textures, or D3D12 readback heaps. Frames move through GPU resources only. CPU access is limited to configuration, process control, diagnostics, and small constant-buffer uploads.

`tools/audit_no_readback.py` enforces this rule for the source tree.

## Current rendering paths

- **D3D11 source, default:** DXGI/D3D11 interception -> frame-complete resource/camera tracking -> native/camera/NVOFA/safe-zero guide composer -> direct in-game NR mount. The mount creates a private same-adapter D3D12 device/queue when the source game does not already provide one.
- **D3D12 source:** the D3D11On12 guide bridge captures the source device/queue, consumes game-supplied NGX/Streamline depth and motion when available, and can discover stable D3D12 scene-depth resources from barrier history. Generic D3D12 motion candidates are scored for diagnostics but are not consumed until their vector convention is known.
- **Direct in-game backend order:** Streamline DLSS-NR feature 1004 when a complete user-supplied Streamline NR stack validates; otherwise caller-compatible signed feature 18 through `nvngx.dll_UniversalDLSS5_NRForwarder.dll`.
- **Automatic fallback:** if both in-game routes fail to initialize, the bridge attempts the proven x64 External NR Host. Passthrough is only the final fallback.
- **Motion (Auto):** explicit GameGuides motion -> positively identified native game velocity -> camera+real-depth reconstruction -> NVIDIA Optical Flow Accelerator -> safe zero motion. The HLSL block-search route is opt-in/experimental rather than an automatic fallback.
- **Depth:** game-supplied NGX/Streamline depth is preferred. D3D12/D3D11 trackers can then select stable scene depth at compatible internal resolutions and use observed metadata/clear behavior as convention hints. An explicit GameGuides adapter overrides automatic discovery; synthetic far depth is the last fallback.
- **Camera data:** D3D11 vertex-shader reflection and constant-buffer update/bind tracking are used conservatively to recover a stable view-projection sequence without arbitrary process-memory scanning.
- **Multi-process applications:** the controller follows the selected process tree and preferentially attaches to children that have loaded DXGI.

Vulkan/OpenGL are not falsely advertised as complete in this revision; the current capable path is DXGI/D3D11/D3D12.

## Live tuning controls

The controller applies settings through shared memory while the target remains running. Available controls include:

- Default / Browser / 2D Game / Video / Aggressive tuning presets
- DLSS-NR intensity, structure, tone, skin structure, style/model hint and preset hint
- Sharpness and exposure
- Motion-vector global/X/Y scale, Y inversion, confidence rejection and static-motion deadzone
- Optical-flow confidence threshold
- Disocclusion threshold
- Text and UI protection
- NR ControlMask enable/strength and semantic AutoMask/UI-correction toggles
- History clamp and reactive strength
- Edge threshold
- Flow search radius/downsample plus game-depth mode and temporal-gap reset
- Ultra Low / Balanced / Quality flow policy
- Direct in-game NR (recommended) / External NR Host fallback / passthrough backends
- Synthetic optical-flow / zero-motion selection
- Secondary swapchain processing
- D3D11On12 toggle
- Unsupported-hardware probe toggle
- Live **Reset history** button
- Runtime validation before attach, including the exact missing NVIDIA DLL names
- Live NGX initialization/result diagnostics plus guide-source fields and GPU debug views

Streamline is **optional at runtime** but its public headers are enabled in the x64 Windows build so UniversalDLSS5 can mount feature 1004 when an authorized `sl.dlss_nr.dll` stack is supplied. The direct signed-feature-18 route remains fully available with only the user-supplied `nvngx_dlssnr.dll`. NVIDIA has not published a DLSS-NR-specific NGX parameter header, so the feature-18 contract remains isolated in the neural backend and fails cleanly into the external-host/passthrough fallback chain if rejected.


## Browser compatibility notes

The controller detects Windows binary-signature mitigation (Code Integrity Guard / CIG) on target processes. A process marked `CIG` will be skipped instead of repeatedly attempting an injection Windows is expected to reject.

Chromium documents a development switch named `--allow-third-party-modules` that disables its binary-signature mitigation so development/test modules can load. If you deliberately test UniversalDLSS5 with Chrome/Chromium/Edge, close the browser completely and launch a test instance with that switch (preferably with a separate temporary browser profile). This weakens a browser security mitigation for that test instance, so do not use it as your normal browsing configuration. Firefox's Windows GPU/compositor process currently has a different sandbox model and may not require this step.

UniversalDLSS5 does not change browser mitigation policy itself and does not attempt to bypass CIG.

## Building on Windows

Requirements:

- Windows 10/11 x64
- Visual Studio 2022 with Desktop C++ and a recent Windows SDK
- CMake 3.24+
- Git
- NVIDIA driver appropriate for the hardware/runtime being tested

A proprietary NVIDIA runtime is **not required to compile the project**. It is imported or placed in the runtime folder after building/installation.

Build the development binaries and tests with:

```bat
BUILD_WINDOWS.bat
```

The script builds the x64 controller/injector/bridge/NR host plus x86 injector/bridge binaries for 32-bit target attachment, then runs the portable test suite and package/no-readback audits.

### Building GitHub release artifacts

Install [NSIS 3.x](https://nsis.sourceforge.io/Download) and ensure `makensis.exe` is on `PATH`, then run:

```bat
BUILD_RELEASE.bat
```

After the normal x64/x86 build succeeds, CPack creates:

```text
release\UniversalDLSS5-Setup-x64.exe
release\UniversalDLSS5-Portable-x64.zip
```

The installer creates the normal application installation, shortcuts, shaders, documentation, and an empty NVIDIA runtime folder. Packaging intentionally installs only `runtime\README.txt`; it never sweeps the developer's local `runtime` directory, so a locally supplied `nvngx_dlssnr.dll` or Streamline DLL cannot accidentally enter the GitHub release.

The supplied UniversalDLSS5 logo is embedded in the controller executable and used for the NSIS installer/uninstaller.

### Optional NVIDIA Optical Flow SDK

If you have the Optical Flow SDK, set:

```bat
set NVOF_SDK_ROOT=C:\path\to\NVIDIA_Optical_Flow_SDK
BUILD_WINDOWS.bat
```

The build looks for `NvOFInterface\nvOpticalFlowD3D11.h` or an `include` directory. If the headers are absent, **Auto** falls back to zero motion rather than feeding the neural model the coarse HLSL block matcher. The HLSL route remains available through the explicit **Optical flow (NVOFA/HLSL experimental)** motion mode.

## NVIDIA DLSS 5 / Neural Rendering runtime

UniversalDLSS5 does not redistribute NVIDIA proprietary runtime DLLs. The required signed-Feature-18 runtime is:

```text
nvngx_dlssnr.dll
```

The optional Streamline DLSS-NR route additionally uses:

```text
sl.interposer.dll
sl.common.dll
sl.dlss_nr.dll
```

In the controller's **Application** page you can:

- choose the runtime destination folder;
- open the runtime folder;
- open the official NVIDIA download page;
- select **Import NVIDIA SDK...** and point at an extracted NVIDIA SDK root.

The importer recursively searches only for the approved filenames, rejects non-x64 binaries, verifies Authenticode and an NVIDIA signer, and copies accepted files to the selected runtime destination. If the chosen SDK package does not contain `nvngx_dlssnr.dll`, the import remains incomplete and the controller tells you what is missing.

Manual placement is also supported: use the exact **Runtime destination** shown in the Application page. Installed builds default to `%LOCALAPPDATA%\UniversalDLSS5\runtime\` unless a populated next-to-app runtime already exists; portable/developer builds can continue using their local `runtime\` folder. The chosen destination is remembered. `UniversalDLSS5.Bridge.dll`, `UniversalDLSS5.Bridge32.dll`, `UniversalDLSS5.NRHost.exe`, and `nvngx.dll_UniversalDLSS5_NRForwarder.dll` are project binaries and stay beside the application; they do not go in the game directory or NVIDIA runtime folder.

See `docs/NVIDIA_RUNTIME_SETUP.md` for detailed setup.

## Running

1. Start `UniversalDLSS5.exe` as the same Windows user/integrity level as the target application.
2. Select the running root application, such as `chrome.exe` or a 2D game.
3. Select the runtime folder.
4. Choose a preset and adjust sliders if desired.
5. Leave **Direct in-game NR (recommended)** selected for normal testing, then press **Attach**. If the direct mount fails, v0.3.1 automatically attempts the External NR Host before passthrough.
6. For multi-process browsers, leave **Attach process tree** enabled; the controller continues watching for a DXGI GPU child that appears later.
7. Use **Reset history** after a large tuning change if temporal artifacts persist.
8. Press **Detach** to ask all injected bridges in the session to unload.

Avoid anti-cheat/DRM/system processes. This project does not include bypasses for those protections.

## Verification in this package

Portable tests cover settings normalization, controller-theme persistence, safe-attach timing, injection-lifecycle wiring, temporal-guide source priority/candidate scoring, legacy-profile migration, NVOFA policy, tuning presets, reset-generation handling, swapchain-primary selection, runtime-file policy (signed-feature runtime required, Streamline plugin optional), injection/CIG policy, low-integrity IPC security, runtime/stage diagnostics, D3D12 flip-model backbuffer rotation, app grouping, persistent NGX failures, D3D12 neural routing, native-motion/depth/camera policies, Streamline/NGX guide-capture wiring, external-host fallback policy, and Windows build wiring. The no-readback audit separately checks the rendering source for screenshot/staging/readback APIs.

The Linux CI-style checks used while producing this package exercise the portable components only; the Windows interception binaries must be compiled and runtime-tested on Windows because this environment does not provide the Windows SDK/MSVC graphics toolchain.

## License and trademarks

Copyright 2026 MotionflowOffical.

UniversalDLSS5 is open source under the **Apache License 2.0**. See [`LICENSE`](LICENSE) and [`NOTICE`](NOTICE). Third-party components remain under their own licenses; attribution and license information is collected in [`THIRD_PARTY.md`](THIRD_PARTY.md) and [`THIRD_PARTY_LICENSES/`](THIRD_PARTY_LICENSES/).

Copyright 2026 **MotionflowOffical**.

UniversalDLSS5 is an independent open-source project and is **not affiliated with, sponsored by, or endorsed by NVIDIA Corporation**. NVIDIA, GeForce, RTX, DLSS, NGX, and related marks are trademarks and/or registered trademarks of NVIDIA Corporation. The Apache-2.0 license applies to UniversalDLSS5's own code and documentation; it does not relicense NVIDIA SDKs or user-supplied NVIDIA runtime binaries.

The NSIS installer displays the Apache-2.0 license during setup and installs `LICENSE`, `NOTICE`, `THIRD_PARTY.md`, and the `THIRD_PARTY_LICENSES` directory beside the application.
