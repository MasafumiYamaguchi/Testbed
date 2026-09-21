param([Parameter(Mandatory=$true)][string]$Executable,[Parameter(Mandatory=$true)][string]$Compare,[string]$OutputDirectory="evidence/top-lobes")
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
Run-Editor "input" @("--top-lobes-test","--frames","200","--capture","top-lobes-off.bmp")
$log=Get-Content "$out/input.stdout.log" -Raw
foreach($marker in @("top_lobes_mode=parent","top_lobes_mode=children","top_lobes_detail_seed_topology_preserved=true single_undo=true save_reload=true PASS")){if(!$log.Contains($marker)){throw "Missing $marker"}}
foreach($mode in @("off","parent","children")){
    $scene=Get-Content "$out/top-lobes-$mode.white.json" -Raw | ConvertFrom-Json
    if($scene.schema_version -ne 8 -or $scene.algorithm_version -ne 3 -or $scene.cloud.kind -ne "top_lobes"){throw "Top hierarchy source schema mismatch"}
    foreach($view in @("front","side")){
        $recipe=if($view -eq "front"){"top-lobes-$mode.white.json"}else{"top-lobes-$mode-side.white.json"}
        $name="$mode-$view"
        Run-Editor $name @("--recipe",$recipe,"--frames","90","--render-width","160","--view-steps","64","--shadow-steps","8","--cache","0","--sun-cache","0","--capture","$name.bmp","--export-hdr","$name-hdr")
        $metadata=Get-Content "$out/$name-hdr/metadata.json" -Raw | ConvertFrom-Json
        if($mode -ne "off" -and ($metadata.cached_density -or $metadata.sun_tau_cache_resolution -ne 0 -or $metadata.empty_space_skipping)){throw "Active top hierarchy incorrectly used combined cache acceleration"}
    }
}
Run-Editor "children-fallback" @("--recipe","top-lobes-children.white.json","--frames","90","--render-width","160","--view-steps","64","--shadow-steps","8","--cache","128","--sun-cache","32","--capture","children-fallback.bmp","--export-hdr","children-fallback-hdr")
$metadata=Get-Content "$out/children-fallback-hdr/metadata.json" -Raw | ConvertFrom-Json
if($metadata.cached_density -or $metadata.sun_tau_cache_resolution -ne 0 -or $metadata.empty_space_skipping){throw "Requested acceleration bypassed active top hierarchy Direct mode"}
& $compareExe "$out/children-front-hdr/linear.exr" "$out/children-fallback-hdr/linear.exr" --strict > "$out/children-fallback-comparison.txt"
if($LASTEXITCODE -ne 0){throw "Requested caches changed top hierarchy Direct output"}
Run-Editor "children-slice" @("--recipe","top-lobes-children.white.json","--frames","90","--cache","0","--diagnostic","1","--capture","children-slice.bmp","--export-hdr","children-slice-hdr")
Add-Type -AssemblyName System.Drawing
foreach($path in Get-ChildItem $out -Filter "*.bmp"){$image=[System.Drawing.Image]::FromFile($path.FullName);try{$image.Save([System.IO.Path]::ChangeExtension($path.FullName,"png"),[System.Drawing.Imaging.ImageFormat]::Png)}finally{$image.Dispose()}}
"Bounded single-development top hierarchy: OFF/parent/children under identical light and camera, front/side/slice captures, exact lower density, detail-seed topology preservation and source Undo/Save/Open. Source generation CPU timings and renderer submit/fence wall timings are in stdout; hardware GPU timestamps unavailable. Active hierarchy across multiple developments remains unsupported and Issue31 stays open. Physical review deferred." | Set-Content "$out/README.txt"
