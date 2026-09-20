# HDR comparison export (Issue 17)

`--export-hdr NEW_DIRECTORY --frames 90 --capture preview.bmp --recipe scene.white.json`
exports `linear.exr`, `display.ppm`, and `metadata.json` after a completed frame.
EXR uses lossless ZIP and FLOAT channels, not HALF. Display PPM is standard
binary RGB8, directly viewable/convertible by image tools. It is the internal
render resolution; the BMP is the actual editor including UI.

Working RGB is linear Rec.709/D65 with no spectral claim. Current transport
already composites the fixed linear background (0.015, 0.022, 0.035):
RGB = L_scatter + T * background. EXR A stores T, NOT opacity. Do not alpha
composite this RGB again. Scatter-only RGB can be recovered by subtracting
T * the recorded background. Values greater than one are preserved.

Display applies exposure 2^EV, Reinhard x/(1+x), then the piecewise sRGB
transfer exactly once. Exposure does not dirty transport; the preceding
benchmark verified zero HDR updates over 60 exposure edits in each cache mode.
Rows start at the top; EXR names R/G/B/A remove storage channel-order ambiguity.
Sidecar includes full Scene (camera, sun, optics, seeds), frame/revision as
64-bit strings, dimensions, integration steps, cache mode, sample count,
build commit, channel semantics and transform. Configure CMake after changing
commit to refresh the compiled commit ID (CI always configures a fresh checkout).

A reserved sibling temporary directory receives all three files. Publication
is one no-replace directory rename (Windows MoveFileEx without replace;
Linux renameat2 RENAME_NOREPLACE). Write/publication failure removes only the
owned temporary directory. Existing destinations are refused, including empty
directories. This is transactional visibility, not a power-loss durability claim.
Export does not mutate the Document. Max dimension 8192 bounds codec size.

CPU checks asymmetric 3x2 RGBA values including zero, 100 and 1e-20 with exact
float roundtrip, known display bytes, orientation, metadata uint64, collision,
invalid values and injected pre-publication failure cleanup. Windows CI exports
all seven HG fixtures. Physical GPU/DCC verification is deferred by user request.

Dependency: TinyEXR v1.0.12 and bundled miniz, exact commit and notices in
THIRD_PARTY.md. Official header APIs define FLOAT output (`save_as_fp16=0`)
and named EXR channel decoding. No OCIO or final-job management is introduced.
