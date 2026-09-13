UniversalDLSS5 v0.2.8 NVIDIA runtime folder
===========================================

This directory is intentionally shipped WITHOUT NVIDIA proprietary runtime DLLs.

Required for the direct signed Feature-18 DLSS Neural Rendering route:
  nvngx_dlssnr.dll

optional Streamline DLSS-NR route when a complete matching stack is available:
  sl.interposer.dll
  sl.common.dll
  sl.dlss_nr.dll
  nvngx_dlssnr.dll

Recommended setup
-----------------
1. Download/extract an official NVIDIA DLSS/Streamline developer package that contains
   the Neural Rendering runtime you are authorized to use.
2. Start UniversalDLSS5.exe.
3. Application -> NVIDIA runtime & attachment -> Import NVIDIA SDK...
4. Select the extracted NVIDIA SDK root.
5. The importer searches only for the approved filenames above, requires x64 Windows
   binaries, validates Authenticode/NVIDIA signing, and copies accepted files here.
6. Press Check runtime. nvngx_dlssnr.dll is required.

Official NVIDIA starting points:
  https://developer.nvidia.com/rtx/dlss
  https://developer.nvidia.com/rtx/streamline/get-started
  https://github.com/NVIDIA-RTX/Streamline/releases

Manual setup
------------
Use the exact Runtime destination shown by the Application page. Installed builds normally
default to %LOCALAPPDATA%\UniversalDLSS5\runtime so the SDK importer does not require
writing into Program Files. A portable/developer build with an already-populated local runtime
folder continues using that local folder. The chosen destination is remembered.

Do not substitute nvngx_dlss.dll (DLSS Super Resolution) for nvngx_dlssnr.dll.

UniversalDLSS5.Bridge.dll and nvngx.dll_UniversalDLSS5_NRForwarder.dll are project files
and stay beside UniversalDLSS5.exe. They do not need to be copied into a game directory.

Security note
-------------
UniversalDLSS5 performs user-selected DLL injection and graphics API hooking. Some
antivirus/EDR products may flag these techniques heuristically. Verify the release source
and published hashes; do not disable security software merely to run the application.

The source/release packaging rules deliberately exclude NVIDIA proprietary DLLs.
