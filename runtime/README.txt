UniversalDLSS5 v0.2.8 runtime folder
===================================

Direct in-game NR is the recommended backend. UniversalDLSS5 never bundles NVIDIA/Streamline runtime DLLs.

Required for the proven direct signed-feature-18 route:
  nvngx_dlssnr.dll

Optional Streamline feature-1004 route (preferred when a complete authorized stack is supplied):
  sl.interposer.dll
  sl.common.dll
  sl.dlss_nr.dll
  nvngx_dlssnr.dll

The optional Streamline files are loaded by the injected in-game mount. The game does not need to ship native DLSS or Streamline; UniversalDLSS5 initializes Streamline itself, attaches its same-adapter D3D12 device, supplies frame tokens/constants/resource tags, and evaluates feature 1004.

If the optional Streamline stack is incomplete or unavailable, the in-game mount falls back to the already-proven caller-compatible signed feature-18 route through:
  nvngx.dll_UniversalDLSS5_NRForwarder.dll

External NRHost remains available as a fallback/diagnostic backend.

Guide priority in Auto mode:
  1. explicit GameGuides native motion
  2. detected game-native velocity/motion resource
  3. camera matrices + real game depth reconstruction
  4. NVIDIA Optical Flow (when SDK headers/driver path are available)
  5. safe zero motion

The coarse HLSL block matcher is intentionally NOT an automatic fallback.
Select "Optical flow (NVOFA/HLSL experimental)" explicitly to test it.

All frame/color/depth/motion resources stay GPU-resident. UniversalDLSS5 does not use screenshot capture, staging framebuffer readback, BitBlt, WGC, or CPU pixel transfer.

BUILD_WINDOWS.bat copies this folder to:
  build\x64\bin\Release\runtime\

The source ZIP intentionally contains none of the proprietary DLLs listed above.


Deployment note:
  UniversalDLSS5.Bridge.dll and nvngx.dll_UniversalDLSS5_NRForwarder.dll stay in the built x64 output directory; they do not need to be copied beside the game executable.
