# Release Runtime Import and Installer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add secure NVIDIA runtime import, documented setup, application branding, and GitHub-ready NSIS/ZIP release packaging without bundling NVIDIA proprietary DLLs.

**Architecture:** Runtime filename policy is portable/header-only and the Windows importer performs recursive discovery, x64 validation, Authenticode/NVIDIA signer checks, and copies only allow-listed DLLs. Release packaging uses CMake install rules plus CPack/NSIS and a release batch script; the supplied icon is embedded through a Windows resource and reused by NSIS.

**Tech Stack:** C++20, Win32, Direct2D UI, WinTrust/CryptoAPI, CMake/CPack, NSIS, Python package verifier.

**Spec:** `docs/superpowers/specs/2026-09-13-release-runtime-installer-design.md`

## Global Constraints

- Never redistribute `nvngx_dlssnr.dll`, `nvngx_dlss.dll`, Streamline runtime DLLs, or NVIDIA import libraries.
- Import only approved NVIDIA runtime filenames and valid x64 NVIDIA-signed DLLs.
- Preserve one-folder overwrite-ready source packaging.
- Use the supplied logo for the controller and NSIS installer/uninstaller.

---

### Task 1: Runtime import policy and regression tests

**Files:**
- Create: `include/udlss/runtime_import_policy.hpp`
- Create: `tests/runtime_import_policy_tests.cpp`
- Modify: `CMakeLists.txt`

- [ ] Write tests for case-insensitive allow-list lookup, required/optional classification, and rejection of UniversalDLSS5/custom/unknown files.
- [ ] Run the test and confirm it fails because the policy header does not exist.
- [ ] Add the minimal policy implementation.
- [ ] Run the test and confirm it passes.

### Task 2: Windows NVIDIA runtime importer and UI

**Files:**
- Create: `src/controller/runtime_importer.hpp`
- Create: `src/controller/runtime_importer.cpp`
- Modify: `src/controller/ui.cpp`
- Modify: `CMakeLists.txt`

- [ ] Add source-wiring regression checks for WinVerifyTrust, NVIDIA signer validation, x64 validation, recursive import, and the UI action.
- [ ] Implement signed runtime discovery/copy with a required-runtime success result.
- [ ] Add `Import NVIDIA SDK...`, `Open runtime`, and official NVIDIA download actions to the Application page.
- [ ] Add a visible antivirus/security notice without suggesting antivirus disabling.

### Task 3: Logo resources

**Files:**
- Create: `resources/UniversalDLSS5.ico`
- Create: `resources/UniversalDLSS5.rc`
- Create: `src/controller/resource.h`
- Modify: `src/controller/ui.cpp`
- Modify: `CMakeLists.txt`

- [ ] Convert the supplied logo to a multi-size Windows ICO.
- [ ] Embed it in the controller and load it for big/small application icons.
- [ ] Configure the same file as NSIS install/uninstall icon.

### Task 4: Installer and portable release packaging

**Files:**
- Create: `BUILD_RELEASE.bat`
- Create: `tests/release_packaging_tests.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tools/verify_package.py`

- [ ] Add tests requiring CPack NSIS/ZIP wiring, install rules, source-owned runtime README-only packaging, and release script output names.
- [ ] Add CMake install rules for x64 targets, copied x86 bridge/injector, shaders, docs, README/THIRD_PARTY/runtime README.
- [ ] Configure CPack NSIS and ZIP without installing the developer runtime directory wholesale.
- [ ] Add `BUILD_RELEASE.bat` that requires NSIS for installer generation and creates GitHub-ready output names.

### Task 5: Documentation and export verification

**Files:**
- Modify: `README.md`
- Modify: `runtime/README.txt`
- Modify: `THIRD_PARTY.md`
- Create: `docs/NVIDIA_RUNTIME_SETUP.md`

- [ ] Document official NVIDIA download/setup, UI import/manual placement, files, runtime destination, and antivirus warning.
- [ ] Run the full portable test suite.
- [ ] Run `tools/audit_no_readback.py` and `tools/verify_package.py`.
- [ ] Export a one-folder overwrite-ready source ZIP, extract it fresh, rerun tests/audits/verifier, and report the verification boundary for Windows/MSVC/NSIS.
