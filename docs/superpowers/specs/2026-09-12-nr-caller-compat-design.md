# UniversalDLSS5 NR Caller Compatibility Design

## Goal
Enable the existing external D3D12 NR host to reach `nvngx_dlssnr.dll` feature 18 on runtime builds that reject direct callers with `0xBAD00002`, while preserving UniversalDLSS5's existing GPU-only capture/transport architecture.

## Constraints
- Do not copy NeuralScreen source code; use only experimentally observed interface behavior.
- Do not redistribute NVIDIA proprietary binaries.
- Keep all frame pixels on GPU resources; no screenshot/staging/readback path.
- Keep compatibility changes inside `UniversalDLSS5.NRHost.exe`; do not patch the game process, anti-cheat, DRM, CIG, or protected-process mitigations.
- The existing `Attempt unsupported hardware` checkbox controls RTX compatibility behavior.
- Blackwell adapters are never remapped.

## Architecture
1. Add a small x64 DLL target named `nvngx.dll_UniversalDLSS5_NRForwarder.dll` next to `UniversalDLSS5.NRHost.exe`.
2. NRHost loads that DLL and asks it to load the user-supplied `runtime/nvngx_dlssnr.dll`.
3. Init, Populate, Create, Evaluate, Release, Shutdown, application-id and snippet-version operations are routed through wrapper exports in the forwarder. Wrappers perform work after the NVIDIA call so MSVC cannot optimize them into tail jumps.
4. NRHost keeps generic NGX-core feature-18 dispatch as a diagnostic first attempt. If it fails, the signed-snippet fallback uses the forwarder rather than direct calls.
5. When `attemptUnsupportedHardware=true`, NRHost dynamically resolves NVAPI, records the real physical GPU architecture, and uses a MinHook detour on `NvAPI_GPU_GetArchInfo` inside NRHost only. For a single primary Turing/Ampere/Ada NVIDIA GPU, the hook reports the runtime-accepted Blackwell architecture tuple only to code executing in NRHost. Blackwell or ambiguous multi-GPU configurations remain unchanged.
6. Shared host IPC is bumped and carries the compatibility flag plus real/reported architecture values for diagnostics.

## Diagnostics
The controller report will include real/reported GPU architecture and whether host-local compatibility was active. Backend messages will identify forwarder load/init/create failures separately from architecture compatibility setup failures.

## Testing
Portable source-wiring tests enforce the forwarder filename, non-tail-call wrapper pattern, CMake wiring, host routing through the forwarder, IPC compatibility flag, host-only MinHook use, and absence of proprietary NVIDIA DLLs. Existing no-readback and package audits remain mandatory.
