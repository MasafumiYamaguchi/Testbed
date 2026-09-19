"""Render the recorded CSV; does not fit, filter, or reweight any case."""
from pathlib import Path
import collections
import csv
import math
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

root = Path(__file__).resolve().parent
rows = list(csv.DictReader((root / 'comparison.csv').open()))
assert len(rows) == 24
assert all(int(row['samples']) == 262144 for row in rows), 'Report text requires the recorded 65,536 samples per seed.'

def value(row, key):
    return float(row[key])

def group_stats(group):
    counts = collections.Counter()
    for row in group:
        z = (value(row, 'mc_hdr') - (value(row, 'single_hdr') + value(row, 'approx_hdr')) / 2) / value(row, 'mc_standard_error')
        counts['improved' if z > 6 else 'worse' if z < -6 else 'unresolved'] += 1
    off = math.sqrt(sum(value(row, 'single_abs_error') ** 2 for row in group) / len(group))
    on = math.sqrt(sum(value(row, 'approx_abs_error') ** 2 for row in group) / len(group))
    return off, on, counts

lines = [
    '# Preview multiple-scattering candidate: CPU comparison', '',
    '**Decision: opt-in only; default OFF. No parameters were fitted to these results.**', '',
    'This report compares central-ray linear HDR in a frozen, homogeneous R32F medium. '
    'It does not measure native GPU appearance, GPU cost, or editing latency. All models '
    'share the same density, optical parameters, sun irradiance, and primary background. '
    'Exposure and tone mapping are absent from the numerical comparison.', '',
    'The candidate adds two attenuated single-scattering octaves with fixed strength '
    '0.5, extinction scale 0.5 and anisotropy scale 0.5. The analytic OFF baseline '
    'and the 2,048-step ON quadrature are compared with a multiple-scattering MC '
    'reference using four independent seeds. The approximation is not unbiased.', '',
    '| Set | Cases | OFF RMSE | ON RMSE | Resolved better / worse / unresolved |',
    '| --- | ---: | ---: | ---: | --- |',
]
for name, group in [('All', rows), ('Predeclared', [r for r in rows if r['set'] == 'predeclared']), ('Held out', [r for r in rows if r['set'] == 'held_out'])]:
    off, on, counts = group_stats(group)
    lines.append(f"| {name} | {len(group)} | {off:.7f} | {on:.7f} | {counts['improved']} / {counts['worse']} / {counts['unresolved']} |")
lines += [
    '',
    'An ordering is called resolved only when the MC mean lies more than six estimated '
    'standard errors from the midpoint of OFF and ON. Since ON is at least OFF, a '
    'mean above that midpoint favors ON. This is a descriptive uncertainty diagnostic, '
    'not a confidence guarantee or a generalization claim. RMSE values are point estimates '
    'over this specific set, with equal weight per case.', '',
    'There are 23 point-estimate improvements, but seven orderings are unresolved by '
    'this diagnostic. The held-out tau=7, albedo=0.35, g=0.3, cosine=+1 case worsens: '
    'OFF error 0.000262735; ON error 0.000421715. ON overshoots the MC mean. The '
    'held-out tau=7, albedo=0.995, g=0.3, cosine=-1 case still has ON error 0.0722773 '
    '(about 280 MC standard errors). That deficit is much larger than both the '
    'measured sampling noise and the quadrature change. Thick media and geometry-dependent '
    'sideways light transport remain major limitations.', '',
    '![Absolute linear HDR errors and reference sampling uncertainty](comparison.png)', '',
    'Each point shows an absolute error against the same MC mean. The grey tick shows '
    'six estimated MC standard errors; it is a scale marker, not an error of the reference '
    'or a measured upper bound. The logarithmic horizontal axis keeps small thin-medium '
    'errors visible alongside thick-medium errors.', '',
    '## Reproduction and limits', '',
    '`white_preview_reference_tests 65536 > comparison.csv 2> comparison.log`', '',
    f"- {sum(int(r['samples']) for r in rows):,} total paths: 65,536 samples per seed, four seeds, 24 cases.",
    '- Seeds: 17, 42, 4,294,967,313 and 99,991. Each seed mean and standard error is retained in the CSV.',
    '- Path cap: 256 bounces and 1,000,000 tracking events per tracking call; incomplete paths fail the executable.',
    '- Predeclared: box bounds [-1,1] on all axes; tau 0.2/5; albedo 0.2/0.8/0.98; g=0.7; cosine -1/+1.',
    '- Held out: bounds [-0.75,0.75] x [-0.5,0.5] x [-1,1]; tau 0.35/7; albedo 0.35/0.9/0.995; g=0.3; cosine -1/+1.',
    '- Camera ray: (0,0,-3) toward +Z, world interval [0,10]; constant 8x8x8 density=1; extinction=tau/2.',
    f"- Maximum absolute 1,024/2,048-step ON quadrature change: {max(value(r, 'approx_quadrature_delta') for r in rows):.9g}.",
    f"- Total recorded sampling wall time: {sum(value(r, 'elapsed_ms') for r in rows)/1000:.3f} seconds. It is CPU time on this host, not a GPU benchmark.",
    '- Default executable sample count is 4,096 per seed for bounded CI runs. Sample count changes MC uncertainty only.',
    '',
    '[Full numeric CSV](comparison.csv), [completion log](comparison.log), '
    '[protocol and source hashes](protocol.json), [model ADR](../../adr/0022-preview-approx.md).', '',
    'The reference has finite sample count and uses weighted absorption plus Russian '
    'roulette. Estimated standard errors can themselves be noisy, especially for rare '
    'paths. The report exposes all seeds and does not claim a proven deterministic '
    'error bound. All boxes are homogeneous; heterogeneous cloud image comparisons '
    'and measured GPU costs are still required before considering default adoption.', '',
]
(root / 'report.md').write_text('\n'.join(lines))
fig, axes = plt.subplots(1, 2, figsize=(14, 7.5), sharex=True)
fig.suptitle('Preview approximation: measured error vs Monte Carlo reference', fontsize=16, x=.07, ha='left')
fig.text(.07, .93, 'CPU central rays | linear HDR | 4 independent seeds x 65,536 samples | candidate remains OFF by default', fontsize=10, color='#444444')
for ax, label in zip(axes, ['predeclared', 'held_out']):
    group = [r for r in rows if r['set'] == label]
    y = list(range(len(group)))
    ax.plot([value(r, 'single_abs_error') for r in group], y, 'o', color='#3066b1', label='Single scattering error', markersize=5)
    ax.plot([value(r, 'approx_abs_error') for r in group], y, 'D', color='#d47a22', label='Approximation error', markersize=4)
    ax.plot([6*value(r, 'mc_standard_error') for r in group], y, '|', color='#555555', label='6 x MC standard error', markersize=12, markeredgewidth=1.7)
    ax.set_yticks(y, [f"tau={r['tau']}, a={r['albedo']}, mu={float(r['cosine']):+g}" for r in group], fontsize=9)
    ax.invert_yaxis()
    ax.set_xscale('log')
    ax.set_xlim(1e-6, .3)
    ax.grid(axis='x', alpha=.2, which='major')
    ax.set_xlabel('Absolute linear HDR error (log scale)')
    ax.set_title('Predeclared box, g=0.7' if label == 'predeclared' else 'Held-out rectangular box, g=0.3', fontsize=11, pad=15)
    ax.spines[['top', 'right']].set_visible(False)
handles, labels = axes[0].get_legend_handles_labels()
fig.legend(handles, labels, loc='lower center', bbox_to_anchor=(.58, .035), ncol=3, frameon=False, fontsize=9)
fig.subplots_adjust(left=.16, right=.99, bottom=.18, top=.84, wspace=.55)
fig.savefig(root / 'comparison.png', dpi=150, facecolor='white')
