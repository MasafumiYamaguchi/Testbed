param([Parameter(Mandatory=$true)][string]$Executable,[Parameter(Mandatory=$true)][string]$Generator,[string]$OutputDirectory="evidence/growth")
$ErrorActionPreference="Stop"
$exe=(Resolve-Path $Executable).Path
$generatorExe=(Resolve-Path $Generator).Path
New-Item -ItemType Directory -Force $OutputDirectory | Out-Null
$out=(Resolve-Path $OutputDirectory).Path
& $generatorExe $out > "$out/contract.log"
if($LASTEXITCODE -ne 0){throw "Selected-state generation contract failed"}
Add-Type -AssemblyName System.Drawing
foreach($wind in @("calm","shear")){foreach($stage in @("young","mature")){foreach($view in @("front","side")){
    $name="$wind-$stage-$view"
    $args=@("--recipe","$name.white.json","--frames","90","--render-width","256","--view-steps","96","--shadow-steps","12","--cache","0","--sun-cache","0","--capture","$name.bmp","--export-hdr","$name-hdr")
    $p=Start-Process -FilePath $exe -ArgumentList $args -WorkingDirectory $out -PassThru -RedirectStandardOutput "$out/$name.stdout.log" -RedirectStandardError "$out/$name.stderr.log"
    try{if(!$p.WaitForExit(240000)){throw "$name timed out"};if($p.ExitCode -ne 0){throw "$name failed: $($p.ExitCode)"}}finally{if(!$p.HasExited){Stop-Process -Id $p.Id -Force}}
    $log=Get-Content "$out/$name.stdout.log" -Raw
    foreach($marker in @("density_reference max_abs_error=","HDR verified")){if(!$log.Contains($marker)){throw "$name missing numeric verification $marker"}}
    $image=[System.Drawing.Image]::FromFile("$out/$name.bmp");try{$image.Save("$out/$name.png",[System.Drawing.Imaging.ImageFormat]::Png)}finally{$image.Dispose()}
}}}
"Identical initial source/seed and camera/light per view. Dimensionless stages .35 and 1; calm vs altitude shear. Selected structures generated before density evaluation. GPU readback and full-frame CPU transmittance comparisons are in logs. Naturalness and physical RTX performance are separate review gates." | Set-Content "$out/README.txt"
