# UniversalDLSS5 D3D12-Unified Neural Backend and Diagnostics UI Design

## Goal

Make DLSS-NR feature 18 execute through one native D3D12 NGX path for both D3D11 and D3D12 source applications, while keeping all frame pixels GPU-resident and making every initialization/evaluation stage visible and copyable in the controller.

## Runtime architecture

- D3D12 source: capture the real D3D12 device/queue, use D3D11On12 only for existing guide generation, and execute NGX feature 18 on the native D3D12 device/queue.
- D3D11 source: keep the existing D3D11 guide pipeline, create a D3D12 device on the same DXGI adapter, and transfer guide/output resources through shareable GPU textures.
- Cross-API synchronization uses a shared D3D12/D3D11 fence. No staging textures, Map/readback, BitBlt, screenshots, or CPU pixel copies.
- A small command allocator/list ring prevents allocator reuse while work is in flight. If all slots are busy, bypass the neural pass instead of blocking the render thread.
- The user-supplied `nvngx_dlssnr.dll` is the only mandatory neural runtime file. No Streamline NR plugin is required.
- The application never rejects RTX 20/30/40 by model name. It attempts the selected runtime and reports the actual NGX result. It does not spoof GPU architecture, patch NVIDIA binaries, or bypass driver/runtime policy checks.

## Diagnostics

`RuntimeStatus` carries a stage bitmask and a failure stage. Stages cover bridge injection, hook installation, Present observation, graphics API detection, D3D12 neural device/queue readiness, guide resources, NGX core init, feature creation, feature evaluation, and output composition. Backend failures persist until retry/reset and preserve the exact NGX/SEH result.

## Controller UI

- Resizable two-pane window with a minimum usable size.
- Top application/runtime bar keeps the grouped icon-based application picker.
- Left pane contains compact processing/quality/motion controls.
- Right pane is a read-only multiline diagnostics editor with vertical/horizontal scrolling and text selection.
- Status badge exposes `ACTIVE`, `PASSTHROUGH`, `WAITING`, or `ERROR`.
- `Copy diagnostics` writes the full report to the clipboard.
- `Save diagnostics` writes a timestamped UTF-8 text report under `%LOCALAPPDATA%\\UniversalDLSS5\\logs`.
- Diagnostics report source API, neural API, injected PID, dimensions, frame counters, runtime path, flow path, stage checklist, result code, and backend message.

## Compatibility reporting

- D3D11 and D3D12 are explicit supported source paths.
- Vulkan/OpenGL remain unsupported in this build and must be reported as such rather than appearing as an NGX failure.
- Protected/CIG/anti-cheat guarded processes remain skipped; no mitigation bypass is added.

## Verification

Portable policy tests verify route selection, stage formatting, persistent errors, grouped app selection, and source wiring. The no-readback audit remains mandatory. Windows compile/runtime validation remains user-side because the build environment here does not provide MSVC/Windows GPU execution.
