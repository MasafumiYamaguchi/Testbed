param([Parameter(Mandatory=$true)][string]$Executable,[Parameter(Mandatory=$true)][string]$Generator,[Parameter(Mandatory=$true)][string]$Compare,[string]$OutputDirectory="evidence/modifiers")
$ErrorActionPreference="Stop"
$exe=(Resolve-Path $Executable).Path
$generatorExe=(Resolve-Path $Generator).Path
$compareExe=(Resolve-Path $Compare).Path
New-Item -ItemType Directory -Force $OutputDirectory | Out-Null
$out=(Resolve-Path $OutputDirectory).Path
& $generatorExe $out > "$out/contract.log"
if($LASTEXITCODE -ne 0){throw "Finishing layer contract failed"}
function Run-Editor([string]$Name,[string[]]$Arguments){
    $p=Start-Process -FilePath $exe -ArgumentList $Arguments -WorkingDirectory $out -PassThru -RedirectStandardOutput "$out/$Name.stdout.log" -RedirectStandardError "$out/$Name.stderr.log"
    try{if(!$p.WaitForExit(240000)){throw "$Name timed out"};if($p.ExitCode -ne 0){throw "$Name failed: $($p.ExitCode)"}}finally{if(!$p.HasExited){Stop-Process -Id $p.Id -Force}}
}
function Assert-Direct([string]$Name){
    $metadata=Get-Content "$out/$Name-hdr/metadata.json" -Raw | ConvertFrom-Json
    if($metadata.cached_density -or $metadata.sun_tau_cache_resolution -ne 0 -or $metadata.empty_space_skipping -or ($metadata.actual_density_extent | Where-Object {$_ -ne 0})){throw "$Name used an unsupported combined cache"}
    $numeric=Get-Content "$out/$Name.stdout.log" -Raw
    foreach($marker in @("density_reference max_abs_error=","grouped_direct_points samples=256","HDR verified","frozen_reload_generation_jobs=0")){if(!$numeric.Contains($marker)){throw "$Name missing numeric verification $marker"}}
    $count=$metadata.scene.cloud.finishing.layers.Count
    if($count -gt 0){
        $marker="modifier_gpu_probes=$($count*12) mask_boundary_and_feather=true hard_interiors_zero=1 "
        if(!$numeric.Contains($marker)){throw "$Name missing modifier boundary and hard-interior verification $marker"}
    }
}
Run-Editor "input" @("--modifier-test","--frames","125","--capture","input.bmp")
$log=Get-Content "$out/input.stdout.log" -Raw
foreach($marker in @("modifier_native=add hard_cut=true single_undo=true redo=true","modifier_native=detail_and_density hard_cut_final=true layers_retained=true","modifier_native=precision_rejection current_retained=true revision_unchanged=true undo_redo_retained=true","modifier_native=reorder_duplicate_delete missing_reference_rejected=true content_hash_stable=true generation_jobs=0 save_reload=true")){if(!$log.Contains($marker)){throw "Missing $marker"}}
foreach($frame in @(55,85,110)){if(!(Test-Path "$out/modifier-$frame.bmp" -PathType Leaf)){throw "Missing native modifier lifecycle capture at frame $frame"}}
$saved=Get-Content "$out/modifier-smoke.white.json" -Raw | ConvertFrom-Json
if($saved.schema_version -ne 11 -or $saved.cloud.kind -ne "frozen" -or $saved.cloud.finishing.layers.Count -ne 3){throw "Finishing source schema mismatch"}
$fixtures=Get-Content "$out/manifest.json" -Raw | ConvertFrom-Json
if($fixtures.Count -ne 6 -or @($fixtures.content_hash | Select-Object -Unique).Count -ne 1){throw "Finishing fixtures changed fixed structural identity"}
foreach($fixture in $fixtures){
    $name=$fixture.name
    Run-Editor $name @("--recipe",$fixture.recipe,"--frames","90","--render-width","192","--view-steps","80","--shadow-steps","8","--cache","0","--capture","$name.bmp","--export-hdr","$name-hdr")
    Assert-Direct $name
    $metadata=Get-Content "$out/$name-hdr/metadata.json" -Raw | ConvertFrom-Json
    if($metadata.scene.cloud.source.content_hash -ne $fixture.content_hash){throw "$name lost immutable content identity"}
}
Run-Editor "protected-fallback" @("--recipe","protected.white.json","--frames","90","--render-width","192","--view-steps","80","--shadow-steps","8","--cache","128","--sun-cache","32","--empty-skip","--capture","protected-fallback.bmp","--export-hdr","protected-fallback-hdr")
Assert-Direct "protected-fallback"
& $compareExe "$out/protected-hdr/linear.exr" "$out/protected-fallback-hdr/linear.exr" --strict > "$out/direct-fallback-comparison.txt"
if($LASTEXITCODE -ne 0){throw "Cache requests changed the finished Direct field"}
Run-Editor "reopened" @("--recipe","modifier-smoke.white.json","--frames","90","--render-width","192","--view-steps","80","--shadow-steps","8","--cache","128","--capture","reopened.bmp","--export-hdr","reopened-hdr")
Assert-Direct "reopened"
$metadata=Get-Content "$out/reopened-hdr/metadata.json" -Raw | ConvertFrom-Json
if($metadata.scene.cloud.source.payload_hash -ne $saved.cloud.source.payload_hash -or ($metadata.scene.cloud.finishing | ConvertTo-Json -Depth 100 -Compress) -ne ($saved.cloud.finishing | ConvertTo-Json -Depth 100 -Compress)){throw "Fresh-process reload changed the fixed field or finishing graph"}
Add-Type -AssemblyName System.Drawing
foreach($path in Get-ChildItem $out -Filter "*.bmp"){$image=[System.Drawing.Image]::FromFile($path.FullName);try{$image.Save([System.IO.Path]::ChangeExtension($path.FullName,"png"),[System.Drawing.Imaging.ImageFormat]::Png)}finally{$image.Dispose()}}
"Native finishing commands: cut/density/detail protection, single Undo, reorder/duplicate/delete, missing-reference rollback and Save/Open. Six recipes share fixed geometry, camera and light. Full GPU field and mask probes, HDR transmittance validation, strict Direct-cache-fallback equality and fresh-process zero-generation reload. UI screenshots require visual review; physical RTX and naturalness acceptance remain separate." | Set-Content "$out/README.txt"
