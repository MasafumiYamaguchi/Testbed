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
if($scene.schema_version -ne 7 -or $scene.algorithm_version -ne 3 -or $scene.cloud.kind -ne "developed"){throw "Developed source schema mismatch"}
foreach($cache in @(0,128)){
    $name="two-cells-cache-request-$cache"
    Run-Editor $name @("--recipe","developed-smoke.white.json","--frames","90","--render-width","160","--view-steps","64","--shadow-steps","8","--cache","$cache","--sun-cache","32","--empty-skip","--capture","$name.bmp","--export-hdr","$name-hdr")
    $manifest=Get-Content "$out/$name-hdr/metadata.json" -Raw | ConvertFrom-Json
    if($manifest.cached_density -or $manifest.sun_tau_cache_resolution -ne 0 -or $manifest.empty_space_skipping -or ($manifest.actual_density_extent | Where-Object {$_ -ne 0})){throw "Two independent groups incorrectly used shared dense cache or acceleration"}
}
& $compareExe "$out/two-cells-cache-request-0-hdr/linear.exr" "$out/two-cells-cache-request-128-hdr/linear.exr" --strict > "$out/direct-fallback-comparison.txt"
if($LASTEXITCODE -ne 0){throw "Requested caches changed Direct fallback output"}
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
"Actual Windows development move gizmo/Command/Undo, independent stretch, source Save/Open and delete Undo. One group migrates exactly. Two groups render Direct even when dense/sun/empty-skip requested. GPU density and point comparisons run during input capture. Physical review deferred." | Set-Content "$out/README.txt"
