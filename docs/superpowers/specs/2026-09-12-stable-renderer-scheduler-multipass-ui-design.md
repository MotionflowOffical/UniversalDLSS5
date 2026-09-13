# Stable Renderer, Neural Scheduler, Multipass, and UI Design

## Goal
Prevent renderer ownership and neural-output flicker in multi-process games, lower controller overhead, add safe 1-4 pass neural refinement, and replace the legacy-looking controller presentation with a neutral modern System/Light/Dark UI.

## Renderer ownership
The controller evaluates all fresh bridge statuses rather than accepting the newest writer. Candidates are scored using render area, D3D API, root-process relationship, processing/neural activity, and implausibly high helper-surface present rates. The incumbent receives hysteresis and a 2.5-second freshness grace. The selected PID is published through shared control; other bridges publish lightweight status but skip neural execution.

## Neural scheduling
Feature 18 uses per-slot input/output/scratch/guide resources in a GPU command ring. The ring starts at three slots and can grow to the configured limit, capped at eight. Ring saturation is soft backpressure, not a Feature-18 failure: temporal history remains valid and the current Present receives the latest completed neural cache (or source image before the first completion). No render-thread CPU wait is introduced.

## Multipass
Pass 1 is the normal temporal Feature-18 evaluation. Passes 2-4 use a second reset-only Feature-18 instance within the same source frame and ping-pong between output and scratch resources. This avoids creating multiple cross-frame temporal histories. If the secondary feature cannot be created or evaluated, processing falls back to the successful 1x temporal path.

## Controller performance and UI
Process-tree discovery, reinjection checks and periodic process pruning run in a background `std::jthread`; the UI timer only reads shared-memory status and changes text when values differ. The controller supports System, Light and Dark themes, neutral surfaces, rounded owner-drawn buttons/tabs, DWM dark/light title-bar integration and no gradients.

## Diagnostics and compatibility
Shared status reports primary renderer PID, queue depth/capacity/limit, requested/executed pass count, backpressure count and reused-output count. Existing signed Feature-18 initialization/resource/parameter contract fixes remain unchanged. D3D11/D3D12 GPU resources remain GPU-only; no frame readback is added.
