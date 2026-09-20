param([Parameter(Mandatory=$true)][string]$Executable,[Parameter(Mandatory=$true)][string]$Generator,[Parameter(Mandatory=$true)][string]$Compare,[string]$OutputDirectory="evidence/anvil")
$ErrorActionPreference="Stop"
$exe=(Resolve-Path $Executable).Path
$generatorExe=(Resolve-Path $Generator).Path
$compareExe=(Resolve-Path $Compare).Path
New-Item -ItemType Directory -Force $OutputDirectory | Out-Null
$out=(Resolve-Path $OutputDirectory).Path
& $generatorExe $out > "$out/contract.log"
if($LASTEXITCODE -ne 0){throw "Anvil field/source/growth contract failed"}
function Run-Editor([string]$Name,[string[]]$Arguments){
    $p=Start-Process -FilePath $exe -ArgumentList $Arguments -WorkingDirectory $out -PassThru -RedirectStandardOutput "$out/$Name.stdout.log" -RedirectStandardError "$out/$Name.stderr.log"
    try{if(!$p.WaitForExit(240000)){throw "$Name timed out"};if($p.ExitCode -ne 0){throw "$Name failed: $($p.ExitCode)"}}finally{if(!$p.HasExited){Stop-Process -Id $p.Id -Force}}
}
$fixtures=Get-Content "$out/manifest.json" -Raw | ConvertFrom-Json
function Run-Fixture($Fixture){
    $name=$Fixture.name
    Run-Editor $name @("--recipe",$Fixture.recipe,"--frames","90","--render-width","192","--view-steps","80","--shadow-steps","8","--cache","0","--sun-cache","0","--capture","$name.bmp","--export-hdr","$name-hdr")
    $log=Get-Content "$out/$name.stdout.log" -Raw
    foreach($marker in @("density_reference max_abs_error=","HDR verified")){if(!$log.Contains($marker)){throw "$name missing numeric verification $marker"}}
}
# Exercise the cache-request fallback immediately after its identical Direct
# baseline so a failure retains useful diagnostics without waiting for all views.
$baseline=@($fixtures | Where-Object { $_.name -eq "wide-front" })
if($baseline.Count -ne 1){throw "Expected one wide-front baseline fixture"}
Run-Fixture $baseline[0]
Run-Editor "wide-fallback" @("--recipe","wide-front.white.json","--frames","90","--render-width","192","--view-steps","80","--shadow-steps","8","--cache","128","--sun-cache","32","--capture","wide-fallback.bmp","--export-hdr","wide-fallback-hdr")
$metadata=Get-Content "$out/wide-fallback-hdr/metadata.json" -Raw | ConvertFrom-Json
if($metadata.cached_density -or $metadata.sun_tau_cache_resolution -ne 0 -or $metadata.empty_space_skipping){throw "Anvil silently rendered only its cached trunk"}
& $compareExe "$out/wide-front-hdr/linear.exr" "$out/wide-fallback-hdr/linear.exr" --strict > "$out/fallback-comparison.txt"
if($LASTEXITCODE -ne 0){throw "Cache request changed anvil Direct result"}
Run-Editor "input" @("--anvil-test","--frames","200","--capture","input.bmp")
$log=Get-Content "$out/input.stdout.log" -Raw
foreach($marker in @("anvil_width_gizmo_inspector_single_undo=true PASS","anvil_direction_gizmo_inspector_single_undo=true PASS","anvil_source_save_reload=true PASS")){if(!$log.Contains($marker)){throw "Missing $marker"}}
foreach($fixture in $fixtures){if($fixture.name -ne "wide-front"){Run-Fixture $fixture}}
Run-Editor "top-wind-slice" @("--recipe","top-wind-front.white.json","--frames","90","--cache","0","--diagnostic","1","--capture","top-wind-slice.bmp","--export-hdr","top-wind-slice-hdr")
Add-Type -AssemblyName System.Drawing
foreach($path in Get-ChildItem $out -Filter "*.bmp"){$image=[System.Drawing.Image]::FromFile($path.FullName);try{$image.Save([System.IO.Path]::ChangeExtension($path.FullName,"png"),[System.Drawing.Imaging.ImageFormat]::Png)}finally{$image.Dispose()}}
"OFF/narrow/wide/tilted, selected stage, altitude wind, manual override and top-lobe integration. Identical camera/light per view. Actual width/direction handle input, one-step Undo, Save/Open, Direct cache fallback and CPU/GPU density + HDR checks. Physical RTX and naturalness acceptance remain separate." | Set-Content "$out/README.txt"
