# Third-party components

UniversalDLSS5 source does not redistribute NVIDIA proprietary runtime DLLs.

## MinHook

- Project: https://github.com/TsudaKageyu/minhook
- Pinned release: v1.3.4
- License: 2-clause BSD
- Used for ordinary DXGI/D3D interception.

## NVIDIA DLSS / NGX SDK

- Project: https://github.com/NVIDIA/DLSS
- Pinned commit: `374959484e79a640feaba44c93ac8cfb0a03f5b5`
- Used at build time for the public NGX headers and Windows x64 import library.
- The build does not package NVIDIA's repository or its DLSS runtime binaries into the UniversalDLSS5 source ZIP.
- `nvngx_dlssnr.dll` is user-supplied and is never fetched or redistributed by this project. `UniversalDLSS5.NRHost.exe` is built from this project's own source and is not an NVIDIA binary.

## NVIDIA Streamline (optional)

- Project: https://github.com/NVIDIA-RTX/Streamline
- Pinned commit: `2122257e0fce486f91b385aa63b9a09b0a34b363` (2.14.1-era source)
- The x64 Windows build enables the public Streamline headers so UniversalDLSS5 can compile its direct in-game feature-1004 mount.
- Runtime binaries (`sl.interposer.dll`, `sl.common.dll`, `sl.dlss_nr.dll`) are user-supplied and are never fetched or redistributed by UniversalDLSS5.
- If a complete authorized Streamline NR stack is unavailable, the mount falls back to the signed feature-18 route and then to the external NRHost.

## NVIDIA Optical Flow SDK (optional)

When the user provides NVIDIA Optical Flow SDK headers, the bridge can compile its NVOFA path. No Optical Flow SDK binary is included in this source package. If unavailable, the GPU HLSL optical-flow fallback remains enabled.
