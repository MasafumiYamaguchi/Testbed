param([Parameter(Mandatory=$true)][string]$Executable,[Parameter(Mandatory=$true)][string]$Compare,[string]$OutputDirectory="evidence/developed")
$ErrorActionPreference="Stop"
$exe=(Resolve-Path $Executable).Path
$compareExe=(Resolve-Path $Compare).Path
New-Item -ItemType Directory -Force $OutputDirectory | Out-Null
$out=(Resolve-Path $OutputDirectory).Path
function Run-Editor([string]$Name,[string[]]$Arguments) {
    $p=Start-Process -FilePath $exe -ArgumentList $Arguments -WorkingDirectory $out -PassThru -RedirectStandardOutput "$out/$Name.stdout.log" -RedirectStandardError "$out/$Name.stderr.log"
    try {if(!$p.WaitForExit(180000)){throw "$Name timed out"};if($p.ExitCode -ne 0){throw "$Name failed: $($p.ExitCode)"}}
    finally{if(!$p.HasExited){Stop-Process -Id $p.Id -Force}}
}
Run-Editor "input" @("--developed-test","--frames","185","--capture","developed-initial.bmp")
$log=Get-Content "$out/input.stdout.log" -Raw
foreach($marker in @("developed_move_gizmo_command_undo=true PASS","developed_independent_stretch_source_save_reload=true PASS","developed_delete_undo=true PASS","developed_empty_source_undo=true PASS")){if(!$log.Contains($marker)){throw "Missing $marker"}}
$scene=Get-Content "$out/developed-smoke.white.json" -Raw | ConvertFrom-Json
if($scene.schema_version -ne 11 -or $scene.algorithm_version -ne 3 -or $scene.cloud.kind -ne "developed"){throw "Developed source schema mismatch"}
foreach($cache in @(0,128)){
    $name="two-cells-cache-request-$cache"
    Run-Editor $name @("--recipe","developed-smoke.white.json","--frames","90","--render-width","160","--view-steps","64","--shadow-steps","8","--cache","$cache","--sun-cache","32","--empty-skip","--capture","$name.bmp","--export-hdr","$name-hdr")
    $manifest=Get-Content "$out/$name-hdr/metadata.json" -Raw | ConvertFrom-Json
    if($manifest.cached_density -or $manifest.sun_tau_cache_resolution -ne 0 -or $manifest.empty_space_skipping -or ($manifest.actual_density_extent | Where-Object {$_ -ne 0})){throw "Two independent groups incorrectly used shared dense cache or acceleration"}
}
& $compareExe "$out/two-cells-cache-request-0-hdr/linear.exr" "$out/two-cells-cache-request-128-hdr/linear.exr" --strict > "$out/direct-fallback-comparison.txt"
if($LASTEXITCODE -ne 0){throw "Requested caches changed Direct fallback output"}
# Contact and coincidence exercise the fusion bridge and independent hard masks.
# Only the saved source is edited; loading reconstructs the complete CPU/GPU proxy.
foreach($offset in @(35,0)){
    $name="overlap-$offset"
    $fixture=Get-Content "$out/developed-smoke.white.json" -Raw | ConvertFrom-Json
    $source=$fixture.cloud.source
    if($source.cells.Count -ne 2){throw "Overlap fixture requires two independent developments"}
    $source.fusion_width=12
    $source.overlap=0.25
    $source.cells[0].translation=@(0,0,0)
    $source.cells[1].translation=@($offset,0,0)
    $source.cells[0].shape.source.parameters.cloud_base=25
    $source.cells[1].shape.source.parameters.cloud_base=0
    $source.cells[0].shape.source.modifiers.base_enabled=$true
    $source.cells[1].shape.source.modifiers.base_enabled=$true
    $source.cells[0].shape.source.modifiers.cuts=@([pscustomobject]@{id="900001";center=@(5,60,0);radii=@(12,18,12);transition=3})
    $source.cells[1].shape.source.modifiers.cuts=@()
    $fixture.camera.position=@(20,80,360)
    $fixture.camera.target=@(20,80,0)
    $fixture | ConvertTo-Json -Depth 100 | Set-Content "$out/$name.white.json" -Encoding utf8
    Run-Editor $name @("--recipe","$name.white.json","--frames","90","--render-width","160","--view-steps","64","--shadow-steps","8","--cache","128","--sun-cache","32","--empty-skip","--capture","$name.bmp","--export-hdr","$name-hdr")
    $metadata=Get-Content "$out/$name-hdr/metadata.json" -Raw | ConvertFrom-Json
    if($metadata.cached_density -or $metadata.sun_tau_cache_resolution -ne 0 -or $metadata.empty_space_skipping -or ($metadata.actual_density_extent | Where-Object {$_ -ne 0})){throw "Overlapping developments used unsupported aggregate cache"}
    if($metadata.scene.cloud.source.fusion_width -ne 12 -or $metadata.scene.cloud.source.cells[1].translation[0] -ne $offset){throw "Overlap source was not preserved"}
    if($metadata.scene.cloud.source.cells[0].shape.source.modifiers.cuts.Count -ne 1 -or $metadata.scene.cloud.source.cells[1].shape.source.modifiers.cuts.Count -ne 0 -or $metadata.scene.cloud.source.cells[1].shape.profile[1].density_scale -ne 0.35){throw "Independent cut/profile fixture was lost"}
    $numeric=Get-Content "$out/$name.stdout.log" -Raw
    foreach($marker in @("density_reference max_abs_error=","grouped_direct_points samples=256","HDR verified")){if(!$numeric.Contains($marker)){throw "Missing overlap numeric gate: $marker"}}
    "offset_x=$offset fusion_width=12 overlap=0.25 first_base=25 second_base=0 first_cut_only=true independent_profile=true density_grid=65x67x69 direct_points=256" | Set-Content "$out/$name-contract.txt"
}
foreach($case in @(@("empty",0),@("single",128))){
    $name=$case[0];$cache=$case[1]
    Run-Editor $name @("--recipe","developed-$name.white.json","--frames","90","--render-width","160","--cache","$cache","--capture","$name.bmp","--export-hdr","$name-hdr")
    $metadata=Get-Content "$out/$name-hdr/metadata.json" -Raw | ConvertFrom-Json
    if($name -eq "single" -and !$metadata.cached_density){throw "Single development did not preserve dense cache path"}
}
Add-Type -AssemblyName System.Drawing
foreach($path in Get-ChildItem $out -Filter "*.bmp") {
    $image=[System.Drawing.Image]::FromFile($path.FullName)
    try{$image.Save([System.IO.Path]::ChangeExtension($path.FullName,"png"),[System.Drawing.Imaging.ImageFormat]::Png)}finally{$image.Dispose()}
}
"Actual Windows development move gizmo/Command/Undo, independent stretch, source Save/Open and delete Undo. One group migrates exactly. Two groups render Direct even when dense/sun/empty-skip requested. Contact X35 and coincidence X0 use fusion12, independent base/cut/profile, full GPU voxel and 256 point comparisons plus CPU image transmittance. Physical review deferred." | Set-Content "$out/README.txt"
