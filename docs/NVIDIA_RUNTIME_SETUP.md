# NVIDIA DLSS 5 / Neural Rendering runtime setup

UniversalDLSS5 does **not** redistribute NVIDIA proprietary DLSS or Streamline runtime binaries. The GitHub installer and portable package contain only UniversalDLSS5-owned binaries, shaders, documentation, and an empty `runtime` directory with setup instructions.

## 1. Obtain the NVIDIA SDK/runtime from NVIDIA

Use NVIDIA's official developer sources:

- DLSS developer page: https://developer.nvidia.com/rtx/dlss
- Streamline SDK download/get-started page: https://developer.nvidia.com/rtx/streamline/get-started
- NVIDIA Streamline release packages: https://github.com/NVIDIA-RTX/Streamline/releases

DLSS Neural Rendering is a newer SDK feature and availability can depend on the NVIDIA package/version and developer access. Download and extract the official Windows x64 package that contains the Neural Rendering runtime you are authorized to use.

UniversalDLSS5 requires this file for its signed Feature-18 path:

```text
nvngx_dlssnr.dll
```

These files are optional and enable the Streamline DLSS-NR route when a complete matching stack is available:

```text
sl.interposer.dll
sl.common.dll
sl.dlss_nr.dll
```

Do **not** substitute `nvngx_dlss.dll` (DLSS Super Resolution) for `nvngx_dlssnr.dll`.

## 2. Import through the application

1. Install or extract UniversalDLSS5.
2. Start `UniversalDLSS5.exe`.
3. Open **Application**.
4. Under **NVIDIA runtime & attachment**, leave the default runtime destination or choose another folder.
5. Press **Import NVIDIA SDK...**.
6. Select the root folder of the extracted NVIDIA SDK/package.
7. UniversalDLSS5 recursively searches only for the approved DLSS-NR/Streamline runtime filenames, checks that candidates are x64 Windows binaries, validates Authenticode, verifies an NVIDIA signer, and copies accepted files into the selected runtime folder.
8. Press **Check runtime**. `nvngx_dlssnr.dll` must be present before the neural backend can attach.

The importer does not copy UniversalDLSS5 DLLs or arbitrary files from the SDK tree.

## Manual placement

If you prefer manual setup, use the exact **Runtime destination** shown on the Application page. On a normal installed build, when no next-to-app runtime has already been provisioned, UniversalDLSS5 defaults to a user-writable path:

```text
%LOCALAPPDATA%\UniversalDLSS5\runtime\nvngx_dlssnr.dll
```

Developer/portable builds that already contain `nvngx_dlssnr.dll` beside the application continue using their local `runtime` folder. Optional Streamline runtime DLLs go in the same selected runtime folder. The selected destination is remembered for future launches.

The following UniversalDLSS5 files stay beside the application and do **not** belong in the game directory or NVIDIA runtime folder:

```text
UniversalDLSS5.Bridge.dll
UniversalDLSS5.Bridge32.dll
nvngx.dll_UniversalDLSS5_NRForwarder.dll
UniversalDLSS5.NRHost.exe
```

## Antivirus / security software warning

UniversalDLSS5 injects its own bridge DLL into a selected process and hooks graphics APIs. Those techniques are also used by overlays, debuggers, capture software, and some malware, so heuristic antivirus/EDR products may flag an unsigned or newly released build.

If a warning appears:

1. verify that the package came from the expected GitHub release;
2. compare the published SHA-256 hash when one is provided;
3. inspect/build the source yourself if you need stronger assurance;
4. submit false-positive samples to the security vendor when appropriate.

Do **not** disable antivirus or endpoint security just to run UniversalDLSS5. Avoid anti-cheat, DRM-protected, protected system, or other processes where third-party injection is not permitted.
