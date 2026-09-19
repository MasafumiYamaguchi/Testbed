# 0005: Finite constrained ellipsoid density

Status: implemented for Issue #7; GPU numerical/visual results recorded in PR.

One shared HLSL `densityAt` evaluates the pointwise local field for compute
generation. Later direct ray marching/bake/export include this function instead
of rewriting it. A double-precision CPU reference implements the same formula.
Validated recipe input is sorted by stable ID so list reordering cannot change
the sequential smooth-union evaluation. IDs are preserved, not renumbered.

The ellipsoid implicit value is `(length((p-center)/radii)-1)*min(radii)`.
It is not an exact distance function. Smooth union uses the polynomial
`min(a,b)-max(k-abs(a-b),0)^2/(4*k)`, with exact min when k=0. A fixed 2-local-metre
inward smoothstep converts it into coverage. Very small cells therefore have
reduced peak coverage; this prototype favors large metre-scale cloud cells.
No sphere tracing or noise is used.

Shape fusion and density overlap are separate: union coverage multiplies
`density * (1 + overlap * max(sum(cellCoverage)-1,0))`. At overlap=0, coincident
cells do not multiply the density. A conservative bound is
`rho_max=density*(1+overlap*(cellCount-1))`, or 0 for an empty field.

Apply local-Y base and every ellipsoid cut after fusion. y<=base height and
points inside/on a complete cut are exactly zero. Optional positive transition
widths ramp only on the permitted side. The explicit envelope has zero density
on/outside its faces and serves as a conservative support AABB. World support
transforms all 8 envelope corners; it does not assume axis-aligned transforms.

Five fixtures: tall cell, wide cell, fused cells, flat base and cut. GPU readback
compares all 65x67x69 voxel centers with CPU reference, tolerance 2e-5 absolute for
these default unit-density fixtures. This is not an accuracy guarantee for all
allowed huge-coordinate/tiny-radius combinations: float precision remains a
constraint and needs separate range policies before production. CPU tests also
exercise empty/overlap/extreme valid radii and density, ordering and forbidden
regions. 256-byte row pitch and nonaligned dispatch are retained.

Screenshots are density slices of the actual GPU result, not lit cloud images.
Texture interpolation can smear discontinuous constraints between voxel centers;
this cache error is distinct from pointwise constraints and is evaluated in #11.
