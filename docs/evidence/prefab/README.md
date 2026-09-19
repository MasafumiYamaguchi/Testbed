# Cumulonimbus prefab: native Windows evidence

Captured on 2026-09-19 by the Release job of [Native build and evidence, run 50](https://github.com/MasafumiYamaguchi/Testbed/actions/runs/35426366577), for [PR #74](https://github.com/MasafumiYamaguchi/Testbed/pull/74).

- Tested PR head: `ff34570cf08490a9d9987e9bfd7e0f856871489e`.
- Tested PR merge commit: `555e0adc48ff6901eb70134ecce1cec3ee69aae3`, confirmed by the retained `build-info.txt`.
- Base commit: `8e5c02b4e327df7dfa1976da876ed526362049ac`.
- Release job: `105852941546`; artifact: `windows-Release-evidence`, ID `10579266922`.
- Downloaded artifact ZIP SHA-256: `14e3a6ecc89a5aef5b45c0f6f85912ad8949c6b8efed3daf9908032be476edfe` (matches GitHub's artifact digest).
- All four jobs passed: Windows Debug/Release and CPU Debug/Release. The separate CPU reference workflow for the same head also passed.

These are actual 900×600 Windows application captures, reviewed individually. The camera remains exactly frontal throughout: position `(0,60,250)`, target `(0,60,0)`. Size controls use a translated public width/height handle; the base control translates vertically. Whole-cloud selection remains active through the interactions.

| Capture | Observed state |
| --- | --- |
| [prefab-initial.png](prefab-initial.png) | Prefab created; width 80 m, height 120 m, base 0 m; public size handle visible. |
| [prefab-116.png](prefab-116.png) | Height drag: height 131.38 m; width and base unchanged. |
| [prefab-156.png](prefab-156.png) | Width drag: width 102.74 m; height 131.38 m and base unchanged. |
| [prefab-196.png](prefab-196.png) | Base drag: base 11.37 m; width and height unchanged. |
| [prefab-225.png](prefab-225.png) | Local cell edit retained through another 20 m of growth, save/reload and Custom conversion/Undo. Height 151.38 m; saved state shown. The taller top extends slightly above the fixed viewport. |

## Assertions and retained source

`prefab.stdout.log` records successful equality with the same typed parameter command and one-step Undo/Redo for all three injected ImGuizmo interactions:

| Parameter | Before | After | Result |
| --- | ---: | ---: | --- |
| Height | 120 | 131.375 | Command equality, one-step Undo and Redo passed. |
| Width | 80 | 102.736 | Command equality, one-step Undo and Redo passed. |
| Cloud base | 0 | 11.3664 | Command equality, one-step Undo and Redo passed. |

The same log records source preservation, source-only save/reload, and Custom conversion/Undo passing. `prefab-smoke.white.json` contains schema 5 / algorithm 2, with a single `cumulonimbus` source and no separately saved generated Recipe. Generated IDs `2..6`, cell 6's local offset/radius/seed override, cell 2's added local offset, and cut ID `100` are retained. The source records the full-precision final width, height and base values.

The largest logged detailed density CPU/GPU discrepancy is `1.40667e-5`, below the `1.6e-4` tolerance. HDR center-ray transmittance comparisons also pass. `prefab.stderr.log` identifies the actual D3D12 adapter as Microsoft Basic Render Driver; this is Windows software-renderer correctness evidence, not physical-GPU performance evidence.

The workflow invoked:

```powershell
./scripts/capture-prefab.ps1 -Executable ./build/windows/Release/white_app.exe
```

That script ran `--prefab-test --frames 260 --render-width 160 --view-steps 64 --shadow-steps 8 --capture prefab-initial.bmp`, checked the in-app assertions and source JSON, then converted the five native BMP captures to PNG. The retained PNGs, logs and source JSON are unchanged copies from the artifact.
