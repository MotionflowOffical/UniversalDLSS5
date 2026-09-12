# UniversalDLSS5 v0.2.6 Quality Guides Design

## Goal
Keep the proven v0.2.5 feature-18 activation path unchanged while removing temporal ghosting, exposing the useful NR control surface, and adding a conservative game-guide extraction layer.

## Constraints
- GPU-only frame/guide transport; no screenshots, staging readback, WGC, BitBlt, or CPU pixel transfer.
- Do not bypass anti-cheat/CIG/protected-process mitigations.
- Do not redistribute NVIDIA runtime DLLs.
- Keep the external NR host + caller-compatible forwarder architecture intact.
- Do not bind invented normals/albedo NR parameters. Extra G-buffer candidates are diagnostic/adapter inputs until a verified runtime contract exists.

## Architecture
1. Anti-ghosting fixes in HLSL: tie-breaking favors zero/short motion, confidence is authoritative, static deadzone and ambiguity rejection zero unreliable motion, and the NR application mask is the inverse of the unreliability/protection mask.
2. Temporal continuity: any skipped neural frame, resize, backend reset, or guide discontinuity forces the next successful NR evaluation to reset history.
3. Guide extraction: the bridge/pipeline can consume a safely accessible game D3D11 depth resource and an optional explicit `UniversalDLSS5.GameGuides.dll` adapter. Synthetic depth remains a fallback and is diagnosed as such.
4. External-host protocol carries ControlMask plus per-frame NR tuning/reset/depth-convention values. The host binds only verified Feature-18 parameters.
5. Controller exposes Neural, Temporal, Composition, and Debug controls. Debug views operate inside the existing GPU pipeline.

## D3D11 depth extraction
At Present, capture the current game DSV before UniversalDLSS5 swaps device-context state. Non-MSAA D32_FLOAT and D24_UNORM_S8 resources are copied into typeless GPU working textures when format-compatible, then converted on-GPU to R32_FLOAT. Unsupported/MSAA/unavailable DSVs fall back to synthetic far depth. No CPU readback is used.

## Optional game adapter
The bridge explicitly loads `UniversalDLSS5.GameGuides.dll` from the target executable directory if present. The adapter ABI can publish D3D11 resources for depth, motion, normals, albedo, control mask, plus metadata such as reversed-Z, motion scale, camera cut, and jitter. Loading is explicit by absolute path; this is not a proxy/sideload DLL mechanism.

## Diagnostics
Runtime status reports depth source/convention, motion source, temporal reset reason, control-mask state, and adapter availability. Candidate normals/albedo remain diagnostic until a verified NR binding exists.
