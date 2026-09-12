# UniversalDLSS5

## v0.2.8 direct in-game NR mount

- **New default:** `Direct in-game NR (recommended)` runs the neural integration inside the target render process instead of treating the game as only a finished-frame source.
- **Non-DLSS games supported:** the bridge creates its own same-adapter D3D12 neural execution context for D3D11 games; the game does not need to ship DLSS or Streamline.
- **Backend order:** authorized Streamline feature 1004 when `sl.interposer.dll` + `sl.common.dll` + `sl.dlss_nr.dll` are supplied, then the proven signed feature-18 forwarder path, then automatic external-NRHost fallback.
- **Motion-source order:** explicit GameGuides native motion -> detected game-native velocity -> camera matrices + real depth -> NVIDIA Optical Flow -> HLSL optical flow -> zero motion.
- **Unity/D3D11 tracking:** shader reflection can recognize `_CameraMotionVectorsTexture`; the bridge also tracks dominant full-resolution depth across the frame and current/previous view-projection candidates from constant buffers.
- **Camera reconstruction:** `camera_motion.hlsl` creates current->previous pixel motion from real depth plus validated current/previous view-projection matrices.
- **No CPU frame path:** color/depth/motion/control resources remain on GPU. No screenshot capture, framebuffer staging readback, WGC, BitBlt, or arbitrary process-memory scanning is added.
- **External host preserved:** the v0.2.7 NRHost path remains available both as an explicit backend and as an automatic fallback when the in-game mount cannot initialize.
- **Honest limitation:** automatic native-resource tracking is currently D3D11-focused. D3D12 source games still use the existing D3D12On12 guide path unless an explicit game adapter provides richer guides.

## Important design rule: no screenshots

The frame path never uses desktop/window capture, GDI screenshots, D3D staging textures, or D3D12 readback heaps. Frames move through GPU resources only. CPU access is limited to configuration, process control, diagnostics, and small constant-buffer uploads.

`tools/audit_no_readback.py` enforces this rule for the source tree.

## Current rendering paths

- **D3D11 source, default:** DXGI/D3D11 interception -> frame-complete resource/camera tracking -> native/camera/NVOFA/HLSL guide composer -> direct in-game NR mount. The mount creates a private same-adapter D3D12 device/queue when the source game does not already provide one.
- **D3D12 source:** the existing D3D11On12 guide bridge captures the source device/queue and passes those native D3D12 objects into the same in-game neural backend. Native D3D12 G-buffer discovery is not yet as complete as the D3D11 tracker.
- **Direct in-game backend order:** Streamline DLSS-NR feature 1004 when a complete user-supplied Streamline NR stack validates; otherwise caller-compatible signed feature 18 through `nvngx.dll_UniversalDLSS5_NRForwarder.dll`.
- **Automatic fallback:** if both in-game routes fail to initialize, the bridge attempts the proven x64 External NR Host. Passthrough is only the final fallback.
- **Motion:** explicit GameGuides motion -> positively identified native game velocity -> camera+real-depth reconstruction -> NVIDIA Optical Flow Accelerator -> HLSL block-search fallback -> zero motion.
- **Depth:** D3D11 tracks the dominant exact-size scene DSV across the completed frame and uses its clear value as a reversed-Z/conventional-Z hint. An explicit GameGuides adapter can override it; synthetic far depth is the last fallback.
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
- Your authorized `nvngx_dlssnr.dll` runtime

Run:

```bat
BUILD_WINDOWS.bat
```

The script builds the x64 controller/injector/bridge/test app and also builds x86 injector/bridge binaries for 32-bit target attachment.
The build also produces a 32-bit bridge/injector. The default external-host architecture is designed so a 32-bit source process can hand GPU-shared resources to the separately built x64 `UniversalDLSS5.NRHost.exe`, avoiding a requirement for a 32-bit DLSS-NR runtime. This cross-bitness path still requires Windows runtime validation on real hardware.

### Optional NVIDIA Optical Flow SDK

If you have the Optical Flow SDK, set:

```bat
set NVOF_SDK_ROOT=C:\path\to\NVIDIA_Optical_Flow_SDK
BUILD_WINDOWS.bat
```

The build looks for `NvOFInterface\nvOpticalFlowD3D11.h` or an `include` directory. If the headers are absent, the GPU HLSL fallback remains available automatically.

## Runtime folder

The simplest setup is to place your authorized, matching runtime set in the source `runtime\` folder **before** running `BUILD_WINDOWS.bat`. The build copies the entire folder into `build\x64\bin\Release\runtime\`, next to the compiled controller/bridge. You may instead copy the same files directly into that built `runtime\` folder after compilation.

The runtime validator requires `nvngx_dlssnr.dll` because it is also the fallback used when the optional Streamline plugin cannot initialize:

```text
nvngx_dlssnr.dll
```

`BUILD_WINDOWS.bat` also builds `UniversalDLSS5.NRHost.exe` and `nvngx.dll_UniversalDLSS5_NRForwarder.dll` into the x64 output folder. Both are UniversalDLSS5 project code, not NVIDIA runtime binaries.

For the optional **Streamline feature-1004** route, also place `sl.interposer.dll`, `sl.common.dll`, and `sl.dlss_nr.dll` in the same runtime folder. The build obtains only public Streamline/NGX headers from pinned source repositories; all proprietary runtime DLLs remain user-supplied. The controller defaults to this output runtime directory but also lets you select another folder.

Proprietary NVIDIA runtime DLLs are intentionally not included in this repository or ZIP.

## Running

1. Start `UniversalDLSS5.exe` as the same Windows user/integrity level as the target application.
2. Select the running root application, such as `chrome.exe` or a 2D game.
3. Select the runtime folder.
4. Choose a preset and adjust sliders if desired.
5. Leave **Direct in-game NR (recommended)** selected for normal testing, then press **Attach**. If the direct mount fails, v0.2.8 automatically attempts the External NR Host before passthrough.
6. For multi-process browsers, leave **Attach process tree** enabled; the controller continues watching for a DXGI GPU child that appears later.
7. Use **Reset history** after a large tuning change if temporal artifacts persist.
8. Press **Detach** to ask all injected bridges in the session to unload.

Avoid anti-cheat/DRM/system processes. This project does not include bypasses for those protections.

## Verification in this package

Portable tests cover settings normalization, legacy-profile migration to the direct in-game backend, NVOFA policy, tuning presets, reset-generation handling, swapchain-primary selection, runtime-file policy (signed-feature runtime required, Streamline plugin optional), injection/CIG policy, low-integrity IPC security, runtime/stage diagnostics, D3D12 flip-model backbuffer rotation, app grouping, persistent NGX failures, D3D12 neural routing, native-motion/depth/camera policies, Streamline mount wiring, external-host fallback policy, and Windows build wiring. The no-readback audit separately checks the rendering source for screenshot/staging/readback APIs.

The Linux CI-style checks used while producing this package exercise the portable components only; the Windows interception binaries must be compiled and runtime-tested on Windows because this environment does not provide the Windows SDK/MSVC graphics toolchain.
