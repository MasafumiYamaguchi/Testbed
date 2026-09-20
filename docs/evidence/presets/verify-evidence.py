#!/usr/bin/env python3
"""Independently inspect the downloaded PR81 preset checkpoint; no GPU claims."""
import copy
import json
import pathlib
import re
import sys

root = pathlib.Path(sys.argv[1])
def read(name):
    return (root / name).read_text(encoding="utf-8-sig")
def data(name):
    return json.loads(read(name))
def require(condition, message):
    if not condition:
        raise AssertionError(message)

states = [(100, "candidate"), (125, "current"), (140, "detail"),
          (180, "scoped"), (235, "adopted"), (255, "clone"), (285, "clone-adopted")]
markers = ["candidate_preview", "discard_undo", "confirmed_reset", "current_return",
           "detail_only", "cancel", "stage_after_reset", "pending_reset", "adopt",
           "clone_candidate", "clone_preview", "clone_adopt", "finishing_reset_guard",
           "finishing_added", "finishing_detail_retained", "finishing_stage_retained",
           "finishing_scoped_retained", "finishing_adopt_retained",
           "clone_finishing_remapped", "clone_finishing_adopt_retained"]
native = read("input.stdout.log")
for marker in markers:
    require(re.search(r"^preset_native=" + marker + r" .* PASS$", native, re.M), marker)
require("preset_native=adopt single_undo=true redo=true total_generation_jobs=5 PASS" in native,
        "five generation jobs")
require("preset_native=detail_only generation_jobs=0 content_hash_stable=true PASS" in native,
        "detail zero jobs")

def assert_metadata(recipe, prefix):
    expected = data(recipe)
    actual = data(prefix + "/metadata.json")
    require(actual["scene"] == expected, prefix + " exact saved scene")
    require(expected["cloud"]["kind"] == "frozen", prefix + " frozen authority")
    require(not actual["cached_density"] and actual["sun_tau_cache_resolution"] == 0
            and not actual["empty_space_skipping"], prefix + " direct mode")
    require((root / prefix / "linear.exr").stat().st_size > 0, prefix + " HDR file")
    return expected

numeric = []
def check_reload(name, recipe):
    scene = assert_metadata(recipe, name + "-hdr")
    log = read(name + ".stdout.log")
    for marker in ["density_reference max_abs_error=", "HDR verified", "frozen_reload_generation_jobs=0"]:
        require(marker in log, name + " " + marker)
    for match in re.finditer(r"density_reference max_abs_error=([\deE.+-]+) tolerance=([\deE.+-]+)", log):
        error, tolerance = map(float, match.groups())
        require(error <= tolerance, name + " density tolerance")
        numeric.append({"name": name, "error": error, "tolerance": tolerance})
    return scene

strict = []
def check_strict(name):
    result = read(name + ".txt").strip()
    match = re.fullmatch(r"linear_RGB_max=([\deE.+-]+) mean=([\deE.+-]+) T_max=([\deE.+-]+)", result)
    require(match is not None, name + " strict result")
    rgb, mean, t = map(float, match.groups())
    require(rgb <= 2e-5 and t <= 1e-6, name + " strict HDR thresholds")
    strict.append({"name": name, "rgb_max": rgb, "rgb_mean": mean, "t_max": t})

scenes = {}
for frame, name in states:
    capture = re.search(rf"^preset_capture frame={frame} scene={name} document_revision=(\d+) rendered_revision=(\d+) generation_jobs=(\d+) PASS$", native, re.M)
    require(capture is not None, name + " capture marker")
    document, rendered, jobs = map(int, capture.groups())
    candidate = name in {"candidate", "scoped", "clone"}
    require((rendered >= 2**63 and rendered != document) if candidate else rendered == document,
            name + " render revision ownership")
    require((root / f"preset-{frame}.png").stat().st_size > 0, name + " actual screenshot")
    recipe = f"preset-{name}.white.json"
    scenes[name] = assert_metadata(recipe, f"preset-{frame}-hdr")
    require(check_reload("reopened-" + name, recipe) == scenes[name], name + " reload")
    check_strict("native-reload-" + name)

for key in ["camera", "sun", "exposure_ev", "preview_approx"]:
    require(scenes["candidate"][key] == scenes["current"][key], "shared Current/Candidate " + key)
    require(scenes["adopted"][key] == scenes["clone"][key] == scenes["clone-adopted"][key],
            "clone preserves shared view " + key)

base = data("preset-finishing-base.white.json")["cloud"]["finishing"]
require(len(base["layers"]) == 3, "three-layer fixture")
stage = check_reload("reopened-stage-preserved", "preset-stage-preserved.white.json")
for name, scene in [("stage", stage)] + [(name, scenes[name]) for name in ["detail", "scoped", "adopted"]]:
    require(scene["cloud"]["finishing"] == base, name + " finishing preserved exactly")
detail = scenes["detail"]["cloud"]["source"]
candidate = scenes["candidate"]["cloud"]["source"]
require(candidate["content_hash"] == detail["content_hash"] and candidate["selection"] == detail["selection"],
        "detail retains immutable structure")
require(stage["cloud"]["source"]["optics"] == detail["optics"], "stage retains optics")
for before, after in zip(detail["fields"], stage["cloud"]["source"]["fields"], strict=True):
    require(before["development_id"] == after["development_id"] and before["layers"] == after["layers"]
            and before["recipe"]["noise"] == after["recipe"]["noise"]
            and before["recipe"]["detail_seed"] == after["recipe"]["detail_seed"], "stage retains field detail")

old = scenes["adopted"]["cloud"]
new = scenes["clone"]["cloud"]
require(old["source"]["id"] != new["source"]["id"] and old["source"]["content_hash"] != new["source"]["content_hash"], "clone fresh identity")
targets = {old["source"]["id"]: new["source"]["id"]}
for a, b in zip(old["source"]["fields"], new["source"]["fields"], strict=True):
    targets[a["development_id"]] = b["development_id"]
old_ids = {layer["id"] for layer in old["finishing"]["layers"]}
new_ids = {layer["id"] for layer in new["finishing"]["layers"]}
require(len(new_ids) == 3 and old_ids.isdisjoint(new_ids), "clone fresh finishing IDs")
for a, b in zip(old["finishing"]["layers"], new["finishing"]["layers"], strict=True):
    expected = copy.deepcopy(a)
    expected["id"] = b["id"]
    expected["target"]["id"] = targets[a["target"]["id"]]
    require(expected == b, "clone remaps only finishing references/identity")
require(new["finishing"] == scenes["clone-adopted"]["cloud"]["finishing"], "clone adoption finishing")
check_strict("adopted-clone-same-appearance")
check_strict("clone-adoption-same-appearance")

manifest = data("preset-manifest.json")
require(manifest["preset_version"] == 1 and manifest["algorithm_version"] == 1
        and len(manifest["fixtures"]) == 8, "versioned eight presets")
for preset in ["cumulonimbus", "wide", "multiple", "anvil"]:
    pair = [f for f in manifest["fixtures"] if f["preset"] == preset]
    require(len(pair) == 2 and {f["wind"] for f in pair} == {"calm", "upper_shear"}
            and len({f["initial_hash"] for f in pair}) == 1
            and len({f["content_hash"] for f in pair}) == 2, preset + " shared initial source")
    for fixture in pair:
        scene = check_reload(fixture["name"], fixture["recipe"])
        for key in ["content_hash", "payload_hash"]:
            require(scene["cloud"]["source"][key] == fixture[key], fixture["name"] + " " + key)

report = {"status": "PASS", "native_markers": len(markers), "native_states": len(states),
          "presets": len(manifest["fixtures"]), "growth_jobs": 5, "strict_comparisons": strict,
          "density_checks": numeric}
print(json.dumps(report, indent=2))
