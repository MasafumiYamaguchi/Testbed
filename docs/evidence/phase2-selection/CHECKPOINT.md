# Evidence handoff — 2026-09-20 19:59:54 UTC

Historical pause checkpoint. Work resumed at the user's request. PR #77 later
completed successfully and its images were reviewed; see
[growth/top review](growth-top-pr77/REVIEW.md) and the updated
[acceptance table](acceptance-status.md). PR #78's separately identified
[partial anvil review](anvil-pr78-partial/REVIEW.md) records a fallback timeout.
The pending states below describe the original pause time, not current status.

Work stopped at the user's requested handoff point. No workflow was cancelled,
no Issue was closed and no main merge was made.

## PR 76: verified and preserved

Head `864e0e5c6e540bce3dc0d39ba9e67f0cb77e8a4e`; native run
`35530527905` and CPU reference run `35530527906` both passed.
The reviewed Release artifact is `10611991159`. Its unmodified selected images,
Scene inputs, HDRs and logs are in `developed-pr76/`; see `REVIEW.md` and
`files-sha256.json`. Initial evidence commit `c67b8127ff6666dfebaf5c05dbf81fa854603f24`
was preserved remotely in PR 78 commit
`ff6ed399700f8925a0929bdec3f0a6f9291f1881`. PR 76 already links that immutable
review and three actual PNGs. This follow-up also preserves the renderer's raw
PPM views and links the current-head reference run.

## PR 77: CI still running; selected images not retrieved

Head `1b1566654fea282c838aae89944d728ea8f1ed61`; tested merge
`b9e24dfbf81176f07f72c674bbc893f8d5ed9da8`.

| Run/job | State at checkpoint |
|---|---|
| Native run `35531174858` | In progress |
| Windows Release job `106131909640` | Conservative empty-space comparisons running |
| Windows Debug | Success |
| Linux CPU Debug/Release | Both success |
| CPU reference run `35531174856` | Success |

Release has passed application startup, growth comparisons, top-lobe hierarchy,
independent cells/contact/coincidence, prefab, centerline, Phase-0, HG and sun
cache comparisons. Progressive/diagnostic/approximation/benchmark and final
artifact upload remain after the current step. Do not report full Native success
until checking the final run state.

Available artifacts are Windows Debug `10611536235`, CPU Release logs
`10611570827`, and CPU Debug logs `10611268040`. The Windows Release artifact has
not yet been published. Debug was downloaded and its actual application image,
adapter and tested merge inspected; it does not contain the selected growth/top
comparisons. PR 77's body was updated to describe the implemented growth/wind
work, successful feature comparisons and the still-pending image review.

## Resume evidence collection

1. Check native run `35531174858`; obtain its `windows-Release-evidence` artifact
   once available and verify the downloaded ZIP SHA256 against the artifact's
   digest. Signed tool URLs download with `curl --location`; Python urllib was
   rejected by the storage endpoint in this environment.
2. Inspect actual `evidence/growth/` calm/shear × young/mature × front/side PNGs
   and unadorned `*-hdr/display.ppm` views. Keep initial/evaluated Scene JSONs,
   `generation-manifest.json`, raw logs and relevant HDR metadata. Inputs are
   structure seed 42, detail seed 17, stage 0.35/1, initial-height fraction 0.4,
   wind reference base 0 m and height 120 m. Calm knots are zero; shear knots are
   `(0,[0,0,0]), (0.5,[20,0,5]), (1,[70,0,25])`. These are displacement in local
   metres at stage 1, not metres/second. Exact fixture source is in
   `tests/generation_tests.cpp` at the tested head.
3. Inspect `evidence/top-lobes/` OFF/parent/children front/side/slice and
   `evidence/developed/` contact X=35/coincidence X=0. Preserve selected raw PNGs,
   settings, numeric logs and strict Direct-fallback comparison results. Keep
   the combined evidence bounded; the current preserved subset is under 1 MB.
4. Evaluate large shape, lobes and detail in that order. Record at most three
   deficiencies and distinguish shape from lighting. Naturalness is not accepted
   from successful generation; active top hierarchies across multiple
   developments remain unsupported. Carry findings into Issues 33/41.
5. Update the acceptance table and PR 77 with final statuses, exact tested SHA,
   artifact digest and immutable evidence links. Do not claim RTX performance:
   this runner reports Microsoft Hyper-V Video / D3D12 and no GPU timestamps.

Scratch copies exist at `/workspace/scratch/59c2b798b94d/pr76-release/` and
`/workspace/scratch/59c2b798b94d/pr77-debug/`; they can be recreated from the IDs
above if unavailable. The evidence worktree is
`/workspace/scratch/59c2b798b94d/evidence`, branch `codex/phase2-ci-evidence`.
