# Current Debug State — Read Before Changing Recovery Again

## Important status distinction

The code in this handoff is **r5**. r5 has portable regression coverage, but the user has
not yet supplied a Cyberpunk 2077 diagnostic run proving r5 at runtime.

Therefore:

- r5 source/build policy tests: verified in the development environment;
- Windows/Cyberpunk r5 behavior: **unconfirmed**;
- last confirmed game behavior: r4 and earlier runs below.

## Last confirmed Cyberpunk run (r4)

Route and backend:

```text
Source API: D3D12
Renderer route: Modern D3D12 recovery
Interop: D3D11On12 staging
Backend: External NR Host (signed snippet)
Resolution: 1920 x 1080
Feature 18 evaluated: OK
Output composited: OK
```

Critical temporal diagnostics:

```text
Frames: presented=3190, pipeline=3171, neural=2587, bypassed=603
Estimated FPS: 682.6
Motion path: Zero motion vectors
Depth guide: Synthetic far depth
SL hits=3186 accepted=0 eval-binds=0 bound-q=0 snapshots=0 promoted=0 owned-read=0 volatile=0
Depth source active: SYNTHETIC
Temporal history: RESET/INVALID (Weak guides: zero motion + synthetic depth)
NGX D3D12 hits: 0 in the displayed/truncated diagnostics
```

The run was long enough. `accepted=0` after thousands of calls is not a warm-up problem.
The old aggregate `SL hits` counter could not tell which Streamline API produced those
calls. r5 replaces it with per-API counters.

The ~683 FPS value was also not credible for real displayed Cyberpunk frames. r5 changes
frame accounting to successful, outermost, non-TEST Presents using QPC timing.

## Earlier r3 run — provider snapshots DID exist

A previous Cyberpunk run showed:

```text
SL hits=168756
accepted=10970
snapshots=10970
owned-read=10957
promoted=0
Motion path: Zero motion vectors
Depth guide: Synthetic far depth
```

This proves that the code/game combination can reach Streamline temporal resources in at
least some sessions and that the injector-owned snapshot transport has operated at high
volume. At that point the snapshots still did not become active game guides.

The large session-to-session change is the reason r5 now supports multiple Streamline
export/forwarder targets and reports each API separately.

## Earlier r2/r3 transition evidence

Another run showed roughly one accepted capture per processed frame and one snapshot per
capture. This moved the debugging boundary from “can we observe provider data?” to “can
recovery select/stage/use it?” Subsequent changes added owned-snapshot priority, direct
shared opening, and delayed capture consumption.

## Frozen / old-frame overlay symptom

Observed behavior before r5:

- short freeze/hitch;
- an old world frame remains over the screen;
- current text/UI/elements can still show through;
- old image may flicker;
- restarting the injector clears it temporarily.

This matches the post-composite structure: stale neural world image + current scene/UI
protection can visually produce exactly that combination.

r5 therefore changed the NRHost contract. An output is usable only when both:

1. the host completion sequence corresponds to the requested frame; and
2. the shared GPU fence has reached that request's output fence value.

If not ready inside the pacing budget, the current game frame is passed through instead of
using an older neural output. A late result is retired later, never re-labeled as current.

This fix is not yet confirmed by a real r5 game run.

## Current temporal-guide problem statement

The project can run Feature 18, but reliable extraction of game-native depth and motion is
not solved across sessions/titles.

Older builds were more successful in some DLSS games because they retained/accessed game
resources more directly. Those techniques were unsafe for broad compatibility and could
cause crashes/GPU resets/lifetime violations. Do not solve the problem by simply reverting
to holding arbitrary transient engine resources through Present.

## r5 diagnostic interpretation

A current r5 `Guide fields` line should contain compact counters like:

```text
SL tagF=... tag=... const=... eval=... items=... D=... M=... null=... A=... S=... R=... inst=....; NGX=...
```

Interpretation:

- `tagF/tag > 0`, `items > 0`, `D/M > 0`: resource tagging is visible; debug admission/lifetime/format next.
- `const/eval > 0` but `tagF=tag=0` or `D=M=0`: Streamline is alive but the resource-tag entry points are not reaching installed detours in that session. Investigate dispatch/forwarder/module topology.
- `D/M > 0` but `A=0`: capture admission rejected tags. Inspect lifecycle, native pointer, API/resource type, state, extent/format and mode.
- `A>0`, `S=0` in recovery for volatile tags: pending provider -> ExecuteCommandLists matching/snapshot creation is broken.
- `S>0`, `R=0`: completed owned snapshot is not being selected/read.
- `R>0` but game depth/native motion inactive: focus on recovery D3D11 opening/staging, format/view compatibility, motion convention and `D3D11GuideExtractor` validation.

## NGX note

Cyberpunk test sessions so far have primarily exposed the interesting temporal path through
Streamline. `NGX D3D12 hits=0` is therefore not by itself evidence that all temporal capture
is broken. Still keep the NGX path functional: `captureNgx12` has access to the actual
NGX parameters and command list and can feed the owned-snapshot architecture where a safe
lifetime contract exists.
