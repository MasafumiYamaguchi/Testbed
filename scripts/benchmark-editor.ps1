param(
 [Parameter(Mandatory=$true)][string]$Executable,
 [string]$OutputDirectory="evidence/benchmark",
 [ValidateRange(30,10000)][int]$Updates=300,
 [ValidateSet(160,640)][int]$InternalWidth=640,
 [switch]$DisableValidation
)
$ErrorActionPreference="Stop"
Add-Type -AssemblyName System.Drawing
$exe=(Resolve-Path $Executable).Path
New-Item -ItemType Directory -Force $OutputDirectory | Out-Null
$out=(Resolve-Path $OutputDirectory).Path
Copy-Item "$PSScriptRoot/../tests/fixtures/benchmark/eight-cells.white.json" "$out/recipe.white.json"
foreach($track in @("density","camera","exposure")){
 foreach($grid in @(0,128,256)){
  $stem="$track-cache-$grid"
  $arguments=@("--recipe","recipe.white.json","--cache","$grid","--render-width","$InternalWidth","--view-steps","64","--shadow-steps","8","--benchmark-updates","$Updates","--benchmark-track",$track,"--benchmark-output","$stem.csv","--capture","$stem.bmp")
  if($DisableValidation){$arguments+="--no-validation"}
  $p=Start-Process -FilePath $exe -ArgumentList $arguments -WorkingDirectory $out -PassThru -RedirectStandardOutput "$out/$stem.stdout.log" -RedirectStandardError "$out/$stem.stderr.log"
  try{
   if(!$p.WaitForExit(600000)){throw "Benchmark timed out: $stem"}
   if($p.ExitCode -ne 0){throw "Benchmark failed: $stem ($($p.ExitCode))"}
   $rows=@(Import-Csv "$out/$stem.csv")
   if($rows.Count -ne $Updates){throw "Incomplete sample count: $stem"}
   foreach($row in $rows){if([int]$row.width -ne $InternalWidth -or [int]$row.height -ne $InternalWidth*9/16){throw "Benchmark silently reduced render resolution: $stem"}}
   $img=[System.Drawing.Image]::FromFile("$out/$stem.bmp")
   try{$img.Save("$out/$stem.png",[System.Drawing.Imaging.ImageFormat]::Png)}finally{$img.Dispose()}
  }finally{if(!$p.HasExited){Stop-Process -Id $p.Id -Force}}
 }
}
Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion,AdapterRAM | Format-List | Out-File "$out/adapters.txt"
Get-FileHash "$out/recipe.white.json" -Algorithm SHA256 | Format-List | Out-File "$out/recipe-sha256.txt"
"updates=$Updates`nwidth=$InternalWidth`nview_steps=64`nshadow_steps=8`nwarmup=30`nframes_in_flight=1`nvalidation=$(!$DisableValidation)`nphysical_acceptance=unverified`ngpu_timestamps=unavailable`nscanout=unavailable" | Out-File "$out/protocol.txt"
