# UniversalDLSS5 Release Runtime and Installer Design

## Goal

Make UniversalDLSS5 distributable as a GitHub release without redistributing proprietary NVIDIA runtime binaries, while giving users a guided way to import the required NVIDIA DLSS Neural Rendering runtime and giving the application and installer a consistent icon.

## Runtime import

The application keeps an explicit runtime destination folder. The Application page exposes an `Import NVIDIA SDK...` action that asks for an extracted official NVIDIA SDK/Streamline folder. The importer recursively searches only for an allow-list of NVIDIA Neural Rendering runtime files: required `nvngx_dlssnr.dll` and optional `sl.interposer.dll`, `sl.common.dll`, and `sl.dlss_nr.dll`.

The importer accepts only x64 Windows DLLs with a valid Authenticode signature whose signing certificate identifies NVIDIA. It copies accepted files into the selected UniversalDLSS5 runtime folder and never copies project DLLs, arbitrary DLLs, libraries, executables, or files with merely similar names. The required NGX runtime must be present after import for attach validation to succeed. Optional Streamline files may be skipped without blocking the signed Feature-18 path.

## User guidance

README and `runtime/README.txt` explain that NVIDIA runtime binaries are not redistributed. Users download/extract NVIDIA's official Streamline/DLSS developer package, then use the UI importer or manually place the required runtime in the selected runtime folder. Documentation includes an antivirus warning: DLL injection and graphics hooks can trigger heuristic security products; users should verify the GitHub release hash/source rather than disabling antivirus protection.

## Release packaging

CMake installs only UniversalDLSS5-owned binaries, shaders, documentation, and the empty runtime README. CPack produces an NSIS installer and portable ZIP. Proprietary NVIDIA runtime names are explicitly excluded from the install manifest even if they are present in a developer's local runtime folder. `BUILD_RELEASE.bat` builds x64 and x86 components, validates the source package, then generates GitHub-ready `UniversalDLSS5-Setup-x64.exe` and `UniversalDLSS5-Portable-x64.zip` outputs.

## Branding

The supplied user logo is converted to a multi-resolution `.ico` resource. It is embedded in the controller executable, loaded as the window/taskbar icon, and used for NSIS installer/uninstaller branding.

## Validation

Portable tests verify the runtime import allow-list and release wiring. Package verification fails if proprietary NVIDIA binaries enter the source or release manifest. Windows runtime verification remains required for Authenticode checking, UI folder selection, and NSIS generation.
