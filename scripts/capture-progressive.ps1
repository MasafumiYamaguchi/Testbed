param([Parameter(Mandatory=$true)][string]$Executable,[Parameter(Mandatory=$true)][string]$Compare)
$ErrorActionPreference="Stop"
Add-Type -AssemblyName System.Drawing
$exe=(Resolve-Path $Executable).Path
$compareExe=(Resolve-Path $Compare).Path
New-Item -ItemType Directory -Force evidence/progressive | Out-Null
$out=(Resolve-Path evidence/progressive).Path
Copy-Item "$PSScriptRoot/../tests/fixtures/phase/isotropic-forward.white.json" "$out/scene.white.json"
foreach($mode in @('fixed','8','64','64-repeat')){
    $arguments=@('--recipe','scene.white.json','--cache','128','--frames','200','--capture',"$mode.bmp",'--export-hdr',"$mode-hdr")
    if($mode -ne 'fixed'){$arguments+=@('--progressive',$(if($mode -eq '8'){'8'}else{'64'}))}
    $process=Start-Process $exe -ArgumentList $arguments -WorkingDirectory $out -PassThru -RedirectStandardOutput "$out/$mode.stdout.log" -RedirectStandardError "$out/$mode.stderr.log"
    try{
        if(!$process.WaitForExit(240000)){throw "Progressive timeout: $mode"}
        if($process.ExitCode -ne 0){throw "Progressive failed: $mode"}
        $image=[System.Drawing.Image]::FromFile("$out/$mode.bmp")
        try{$image.Save("$out/$mode.png",[System.Drawing.Imaging.ImageFormat]::Png)}finally{$image.Dispose()}
    }finally{if(!$process.HasExited){Stop-Process -Id $process.Id -Force}}
}
& $compareExe "$out/64-hdr/linear.exr" "$out/64-repeat-hdr/linear.exr" --strict | Out-File "$out/repeatability.txt"
if($LASTEXITCODE -ne 0){throw 'Progressive results not reproducible'}
& $compareExe "$out/fixed-hdr/linear.exr" "$out/64-hdr/linear.exr" --report | Out-File "$out/fixed-difference.txt"
if($LASTEXITCODE -ne 0){throw 'Progressive comparison failed'}
