param([Parameter(Mandatory=$true)][string]$Executable,[Parameter(Mandatory=$true)][string]$Generator,[Parameter(Mandatory=$true)][string]$Compare,[string]$OutputDirectory="evidence/freeze")
$ErrorActionPreference="Stop"
$exe=(Resolve-Path $Executable).Path
$generatorExe=(Resolve-Path $Generator).Path
$compareExe=(Resolve-Path $Compare).Path
New-Item -ItemType Directory -Force $OutputDirectory | Out-Null
$out=(Resolve-Path $OutputDirectory).Path
& $generatorExe $out > "$out/contract.log"
if($LASTEXITCODE -ne 0){throw "Frozen field contract failed"}
function Run-Editor([string]$Name,[string[]]$Arguments){
    $p=Start-Process -FilePath $exe -ArgumentList $Arguments -WorkingDirectory $out -PassThru -RedirectStandardOutput "$out/$Name.stdout.log" -RedirectStandardError "$out/$Name.stderr.log"
    try{if(!$p.WaitForExit(240000)){throw "$Name timed out"};if($p.ExitCode -ne 0){throw "$Name failed: $($p.ExitCode)"}}finally{if(!$p.HasExited){Stop-Process -Id $p.Id -Force}}
}
Run-Editor "input" @("--freeze-test","--frames","220","--capture","input.bmp")
$log=Get-Content "$out/input.stdout.log" -Raw
foreach($marker in @("freeze_native=adopt completed_candidate=true single_undo=true redo=true","freeze_native=finish content_hash_stable=true detail_camera_light_generation_jobs=0","freeze_native=cancel fixed_cloud_preserved=true retry_started=true","freeze_native=replace single_undo=true previous_success_retained=true save_reload=true reload_generation_jobs=0")){if(!$log.Contains($marker)){throw "Missing $marker"}}
foreach($wind in @("calm","wind")){foreach($view in @("front","side")){
    foreach($state in @("selected","frozen")){
        $name="$wind-$view-$state"
        Run-Editor $name @("--recipe","$name.white.json","--frames","90","--render-width","192","--view-steps","80","--shadow-steps","8","--cache","0","--capture","$name.bmp","--export-hdr","$name-hdr")
        $numeric=Get-Content "$out/$name.stdout.log" -Raw
        foreach($marker in @("density_reference max_abs_error=","HDR verified")){if(!$numeric.Contains($marker)){throw "$name missing numeric verification $marker"}}
    }
    & $compareExe "$out/$wind-$view-selected-hdr/linear.exr" "$out/$wind-$view-frozen-hdr/linear.exr" --strict > "$out/$wind-$view-freeze-comparison.txt"
    if($LASTEXITCODE -ne 0){throw "Freeze changed the selected GPU field for $wind-$view"}
}}
foreach($case in @("first","detail","smoke")){
    $name="reopened-$case"
    Run-Editor $name @("--recipe","freeze-$case.white.json","--frames","90","--render-width","192","--view-steps","80","--shadow-steps","8","--cache","128","--sun-cache","32","--capture","$name.bmp","--export-hdr","$name-hdr")
    $metadata=Get-Content "$out/$name-hdr/metadata.json" -Raw | ConvertFrom-Json
    if($metadata.scene.cloud.kind -ne "frozen"){throw "Reload lost authoritative fixed state"}
    $numeric=Get-Content "$out/$name.stdout.log" -Raw
    if(!$numeric.Contains("frozen_reload_generation_jobs=0")){throw "Reload did not verify zero generation jobs"}
}
Add-Type -AssemblyName System.Drawing
foreach($path in Get-ChildItem $out -Filter "*.bmp"){$image=[System.Drawing.Image]::FromFile($path.FullName);try{$image.Save([System.IO.Path]::ChangeExtension($path.FullName,"png"),[System.Drawing.Imaging.ImageFormat]::Png)}finally{$image.Dispose()}}
"Actual Generate/Freeze/finish/cancel/retry/adopt/Undo/Save/Open lifecycle. Selected-vs-frozen GPU HDR comparisons under calm/altitude wind, front and side. Separate app processes reopen first/detail/replaced fixed files with generation count zero. Layer and detail edits preserve immutable content identity. Numeric/visual evidence does not substitute for physical RTX or naturalness review." | Set-Content "$out/README.txt"
