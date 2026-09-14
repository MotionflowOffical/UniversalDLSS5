# UniversalDLSS5

**Current version:** v0.4.3  
**Status:** Experimental Windows graphics injector/controller

UniversalDLSS5 is an experimental project that brings NVIDIA DLSS Neural Rendering / Feature 18 to games and applications that do not expose the feature natively. It hooks the game's presentation API, builds the required color/temporal inputs, executes the neural path, and composites the result back into the original renderer.

> UniversalDLSS5 is an independent open-source project and is **not affiliated with, sponsored by, or endorsed by NVIDIA**.

## Current renderer support

| Renderer | Current path |
| --- | --- |
| Direct3D 12 | Native fast path or late-attach recovery path |
| Direct3D 11 | Native fast path |
| Direct3D 10 / 10.1 | Compatibility frontend |
| Direct3D 9 / 9Ex | Compatibility frontend, including 32-bit games |
| OpenGL | Windows OpenGL compatibility frontend |
| Vulkan | Win32/Vulkan compatibility frontend |

Modern D3D11/D3D12 games that already work stay on their existing low-latency path. Legacy or unusual renderers are routed through compatibility frontends only when needed.

For **32-bit** games, the x86 bridge captures the renderer while the real Feature 18 work runs in the 64-bit `UniversalDLSS5.NRHost.exe` process.

## v0.4.3 highlights

- D3D9, D3D10, D3D11, D3D12, OpenGL and Vulkan renderer routing.
- x86 game support through the x64 Feature 18 host.
- UE5-safe late-attach D3D12 recovery used by games such as Half Sword.
- Automatic detach when the target application exits.
- Safer quiescent manual detach.
- Real Feature 18 GPU timing in diagnostics.
- Game-provided motion/depth support where lifetime is safe.
- Late-attach D3D12 recovery snapshots short-lived Streamline motion/depth immediately into UniversalDLSS5-owned resources, so Cyberpunk-style volatile guides can be used without retaining engine resources.
- **1x-4x NR passes** work on both in-process and x64 NRHost routes; passes above 1 use a separate reset-only refinement feature with scratch ping-pong.
- NVOFA, camera/depth and HLSL optical-flow fallbacks remain available when native game guides cannot be used safely.

## Screenshots

<p align="center">
  <img src="docs/images/half-sword.jpg" width="32%" alt="Half Sword running through UniversalDLSS5">
  <img src="docs/images/cod4-modern-warfare.jpg" width="32%" alt="Call of Duty 4 Modern Warfare running through UniversalDLSS5">
  <img src="docs/images/cyberpunk-2077.jpg" width="32%" alt="Cyberpunk 2077 running through UniversalDLSS5">
</p>

Additional captures are included under [`docs/images`](docs/images/).

## Temporal inputs

UniversalDLSS5 prefers reliable game data when available:

- game motion vectors from safe Streamline/NGX/native routes;
- game depth with detected depth convention;
- camera matrices when available;
- NVIDIA Optical Flow as the universal motion fallback;
- HLSL optical flow and zero-motion fallback modes.

For late-attached D3D12 recovery, transient game resources are never held across the frame. Streamline motion/depth tagged as short-lived or valid through Present are copied immediately in the provider command buffer into injector-owned textures; heuristic and unsafe game resources remain diagnostic-only. This keeps the UE5 lifetime protection while restoring usable game vectors/depth in titles such as Cyberpunk 2077.

The **NR passes** control supports 1x-4x processing on both direct and external-host routes. Pass 1 is temporal; additional passes use a separate reset-only refinement feature and local scratch resource.

## Installation

1. Download or build UniversalDLSS5.
2. Start `UniversalDLSS5.exe`.
3. Open the **NVIDIA Runtime** setup page and choose **Import NVIDIA SDK**.
4. Point UniversalDLSS5 at an official NVIDIA SDK/runtime package containing `nvngx_dlssnr.dll`.
5. Launch the game, select its process, then attach.

NVIDIA proprietary runtime binaries are **not distributed** in this repository. See [`docs/NVIDIA_RUNTIME_SETUP.md`](docs/NVIDIA_RUNTIME_SETUP.md).

## Usage and diagnostics

The controller exposes renderer route, source API, game/process architecture, motion source, depth source, queue confidence, Feature 18 status, GPU time and external-host state.

Logs are written under:

```text
%LOCALAPPDATA%\UniversalDLSS5\logs\
```

If a game fails, the attach/crash log is the most useful file to include with a bug report.

## Compatibility notes

UniversalDLSS5 is still experimental. Compatibility depends on swapchain behavior, anti-cheat/protected processes, renderer synchronization, HDR format, driver/runtime versions and game-specific resource lifetimes.

- Native D3D11/D3D12 games do not pay legacy compatibility overhead.
- Late-attach D3D12 recovery uses an isolated same-adapter D3D12/D3D11 transport and the x64 NRHost.
- Classic D3D9 may require a more expensive full-resolution compatibility transfer than modern APIs.
- Protected or anti-cheat processes may reject injection entirely.

Because the project uses DLL injection and graphics API hooks, some **antivirus** products may report heuristic detections. Verify release hashes and obtain builds from the official repository.

## Build

Requirements: Windows 10/11, Visual Studio 2022 with C++ tools, CMake, NVIDIA GPU/driver, and the required NVIDIA SDK/runtime files.

Build the Windows binaries:

```bat
BUILD_WINDOWS.bat
```

Build the release package/installer:

```bat
BUILD_RELEASE.bat
```

The build produces x64 components plus the x86 bridge/injector required for 32-bit games.

## Documentation

- [`docs/V0.4.3_RELEASE.md`](docs/V0.4.3_RELEASE.md) — current release notes
- [`docs/NVIDIA_RUNTIME_SETUP.md`](docs/NVIDIA_RUNTIME_SETUP.md) — NVIDIA runtime import/setup
- [`docs/GAME_GUIDES_SAFE_ATTACH.md`](docs/GAME_GUIDES_SAFE_ATTACH.md) — temporal-guide and safe-attach notes
- [`docs/V0.4.0_RELEASE.md`](docs/V0.4.0_RELEASE.md) — renderer compatibility architecture

## License

UniversalDLSS5 is licensed under the **Apache License 2.0**. See [`LICENSE`](LICENSE).

Copyright 2026 MotionflowOffical.
