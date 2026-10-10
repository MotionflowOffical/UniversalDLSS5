# Handoff Next Steps

Do these in order. Avoid broad rewrites until the new r5 diagnostics identify the failing
boundary.

## 1. First real r5 Cyberpunk validation

Build the exact handoff tree on Windows and run Cyberpunk 2077 through the Modern D3D12
recovery route. Capture diagnostics **while still attached and Processing**.

Record:

```text
Frames / Estimated FPS
Motion path
Game motion guide
Game depth guide
Temporal history
Depth guide
Guide fields (entire line)
Depth source active
Backend message
```

Also note whether the frozen old-frame overlay still occurs.

### Success criteria for stale-output fix

- no persistent old-world-frame-under-live-UI artifact;
- displayed FPS diagnostic is plausible for the actual game;
- when NRHost misses a budget, current frame passes through rather than showing stale NR;
- no device removal / GPU reset.

## 2. Use the new Streamline counters to choose the next branch

### Branch A — `tagF/tag` and `D/M` are zero, but `const/eval` rise

This is now the highest-value case to investigate. It means resource-tag calls are using a
different path than the exports currently detoured.

Investigate loaded modules and call targets rather than changing snapshot policy again:

- log module base/name for each discovered Streamline export target;
- log the actual target addresses installed for tagF/tag/const/eval;
- compare sessions where resource tags appear vs sessions where only const/eval appear;
- check whether the game/interposer caches function pointers before the bridge installs;
- check whether the resource-tag calls route through a plugin/interposer interface that is
  not the exported symbol being hooked;
- re-probe late-loaded modules and ensure deduplication does not skip a distinct trampoline.

Do not infer success merely from `inst=1111`; that only proves some exports are hooked.

### Branch B — `D/M > 0`, but `A=0`

Instrument rejection reasons in `captureSlTag` / capture admission:

- lifecycle;
- API/resource native type;
- null native pointer;
- D3D12 resource desc/format/extent;
- known/unknown state;
- capture mode;
- command-list identity;
- motion convention / scales.

Expose counters for each rejection reason instead of relying on a single aggregate.

### Branch C — `A>0`, `S=0`

Trace pending provider records against `hookExecute`:

- pending command-list identity;
- submitted command-list identity;
- queue identity/type;
- age/eviction;
- `snapshotDescSupported` result;
- slot allocation/reuse state;
- command allocator/list reset/close/execute/signal HRESULTs.

The copy must remain immediately after the matching provider list on the same queue.

### Branch D — `S>0`, `R=0`

Focus on promotion/selection:

- private snapshot-fence completed value;
- capture replacement priority;
- slot consumed flag;
- device identity;
- capture freshness windows.

Do not let a newer game-owned live tag starve a completed injector-owned snapshot in
SnapshotOnly recovery.

### Branch E — `R>0`, but still synthetic/zero

Focus on `D3D12On12Pipeline::process` and `D3D11GuideExtractor`:

- direct `OpenSharedResource1` result;
- fallback staging result;
- actual D3D12 and opened D3D11 formats;
- depth view format mapping;
- guide extent/aspect policy;
- motion convention validity and scale;
- SRV creation failures;
- whether the external `GuideProbeResult` is actually passed into `D3D11Pipeline::process`;
- whether `prepareDepth` / native motion SRV creation rejects it.

Keep explicit provider candidates less restrictive than heuristic tracker candidates when
internal render resolution is lower than output resolution.

## 3. If the stale-frame overlay persists in r5

Do not assume it is ordinary FPS instability. Instrument frame identity end-to-end:

```text
successful Present sequence / backbuffer index
pipeline request sequence
NRHost frameSeq
NRHost completedSeq
input fence value
output fence value
bridge-observed GPU completed fence
neural output accepted/rejected reason
```

Add counters for exact-output timeout, sequence mismatch, host error, stale retirement,
and passthrough. The output may only enter post-composite when the requested sequence and
GPU completion both match.

Also verify that one physical/displayed Present is not being processed through multiple
swapchains or a secondary swapchain. `processSecondarySwapchains` should remain controlled.

## 4. Regression rules

Before any production change:

1. add a minimal policy/wiring regression test;
2. verify the new test fails for the intended reason;
3. implement the narrow change;
4. run the whole portable suite;
5. run `tools/verify_package.py`;
6. run `tools/audit_no_readback.py`;
7. build on Windows before claiming D3D runtime correctness;
8. validate in a real game before declaring the bug solved.

## 5. Things NOT to reintroduce

Avoid fixes that:

- retain transient engine textures across Present without a provider lifetime guarantee;
- inject waits/signals into arbitrary game queues to make recovery convenient;
- rewrite/mutate the provider's command list;
- perform GPU->CPU readback on the hot path;
- treat unknown motion-vector encoding/direction as trusted;
- reuse an NRHost output from a previous frame because the current one missed its budget.
