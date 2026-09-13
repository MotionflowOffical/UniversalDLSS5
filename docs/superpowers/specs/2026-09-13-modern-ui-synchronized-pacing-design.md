# Modern Controller UI and Frame-Matched Neural Pacing Design

## Goal
Replace the legacy child-control Win32 controller surface with a Direct2D/DirectWrite canvas UI and eliminate neural trailing caused by replaying stale asynchronous Feature-18 output.

## Rendering scheduler
Add three frame pacing modes:
- Synchronized (default): submit the current neural job, wait for that job's completion, and present only the matching output.
- Adaptive: permit at most one-frame-old completed neural output. If output would become older than one frame, synchronize to the current job before presentation.
- Asynchronous: preserve the existing maximum-throughput latest-completed-output behavior and report exact output age.

Every submitted slot records the source Present sequence and submission timestamp. Runtime diagnostics report neural output age in frames and milliseconds plus pacing mode. Queue pressure is not a backend failure.

For low-confidence guides (zero motion + synthetic depth, with no valid camera motion), temporal history is reset every frame. This intentionally converts the NR pass to frame-local refinement rather than allowing unsupported temporal accumulation/ghosting.

Multipass work remains atomic per source frame: passes 2-4 execute inside the same D3D12 command list/job as pass 1 and therefore cannot be overtaken by a newer game frame.

## Controller UI
Keep a native Win32 top-level window but render all visible UI using Direct2D and DirectWrite. Do not create visible tab controls, trackbars, checkboxes, edit controls, or stock combo boxes.

Use a left navigation rail with Application, Neural Rendering, Motion, Composition, Diagnostics, and Advanced pages. Controls are custom D2D buttons, segmented choices, toggles, sliders, cards and scrollable text. The layout is DIP-based, DPI-aware and responsive.

Light, Dark, and System themes use neutral surfaces, one blue accent, rounded 8-12 DIP radii, DirectWrite text and no gradients. Runtime/process maintenance stays on the existing background thread. UI polling only reads shared-memory status and invalidates the window when values change.

## Compatibility
Preserve the signed Feature-18 contract, per-slot resource ownership, primary-renderer election, 1-4 passes, profile persistence, runtime folder selection, diagnostics copy/save, process-tree injection, and single-folder overwrite-ready packaging.
