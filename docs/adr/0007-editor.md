# 0007: Primitive editor and command integration

Issue #9, stacked on #6–#8. The editor owns one EditorSession/Document. Inspector,
gizmo, camera, preset, add/delete and scripted commands all update that session.
Renderer snapshots are refreshed when the document revision changes; shapes
rebake the diagnostic density field while camera/light/optics invalidate HDR.
Display-only exposure preserves HDR. Rendering is synchronous and uses the
low-cost 160-pixel/64-view-step default; async work and revisioned job scheduling
remain Issue #12.

Selection intersects editing ellipsoids, not translucent density. Cloud TRS is
applied to rays without normalizing local directions. Blue cell / orange cut
wire circles, highlighted selection, axis handles and camera distance expose
the editing geometry. Separate Select cuts mode avoids ambiguous overlapping
cell/cut picking. Front/Side/Top permit placement checks from three directions.

Left input belongs to the gizmo or primitive selection; right input belongs to
orbit. Active edits suppress orbit, popups suppress viewport interactions, and
one continuous inspector/gizmo/orbit action groups into one Undo command. Esc
restores the pre-drag scene. Invalid document edits preserve the last valid scene
and display the validation message. Save/Open accept UTF-8 paths; dirty Open
requires the explicit Discard and open button. Saving or loading during a drag
is disabled. File dialogs, free paint and multi-object selection are out of scope.

## Reproduction

1. Start the default fusion fixture, select Cell 2. Scale its green Y handle or
   increase Radii Y to stretch vertically. Undo once restores the whole drag.
2. Increase Radii X to widen it, without changing either seed.
3. Enable Flat base and set Base height to 12. The underside clips to that height.
4. Add cut, select the orange primitive, then move/scale it. Inspect with Front,
   Side and Top. Cuts affect density while outlines show their actual placement.
5. Undo/Redo, Save to a `.white.json` path, modify, and Open. Cancel preserves edits;
   Discard and open restores the saved source parameters.
6. Enter a negative radius: the previous valid geometry remains, with an error.
   Drag and press Escape: no partial command should remain in history.

## Automated evidence

CPU geometry tests cover ray picking and gizmo matrix round trips under rotated,
nonuniform cloud TRS. Windows `--self-test --frames 600` injects ImGui mouse
position/button events through the real ImGuizmo path for Y translation and
Y scale. It requires a changed selected primitive, an unchanged camera, one-step
Undo and matching Redo. This exercises UI command routing but does not replace
manual verification of operating-system input delivery, accessibility or pen use.

The same run captures the four editing operations, save/reload, three camera
views a camera inside the cloud, opposite sun direction and an empty volume. At each edit capture, full density readback
and full-frame CPU/GPU transmittance are checked. Logs and unmodified capture
images are uploaded with the executable. Hosted WARP results are not physical
RTX usability/performance acceptance.
