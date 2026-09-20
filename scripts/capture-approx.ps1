param([Parameter(Mandatory=$true)][string]$Executable,[Parameter(Mandatory=$true)][string]$Compare)
$ErrorActionPreference="Stop"
Add-Type -AssemblyName System.Drawing
$exe=(Resolve-Path $Executable).Path
$compareExe=(Resolve-Path $Compare).Path
New-Item -ItemType Directory -Force evidence/approximation | Out-Null
$out=(Resolve-Path evidence/approximation).Path
foreach($enabled in @($false,$true)){
    $name=if($enabled){'enabled'}else{'disabled'}
    $scene=Get-Content "$PSScriptRoot/../tests/fixtures/phase/isotropic-forward.white.json" -Raw | ConvertFrom-Json
    $scene.schema_version=4
    $scene | Add-Member -NotePropertyName preview_approx -NotePropertyValue @{enabled=$enabled;strength=0.5}
    $scene | ConvertTo-Json -Depth 32 | Set-Content -Encoding utf8 "$out/$name.white.json"
    $arguments=@('--recipe',"$name.white.json",'--cache','128','--frames','100','--capture',"$name.bmp",'--export-hdr',"$name-hdr")
    $process=Start-Process $exe -ArgumentList $arguments -WorkingDirectory $out -PassThru -RedirectStandardOutput "$out/$name.stdout.log" -RedirectStandardError "$out/$name.stderr.log"
    try{
        if(!$process.WaitForExit(240000)){throw "Approximation timeout: $name"}
        if($process.ExitCode -ne 0){throw "Approximation failed: $name"}
        $metadata=Get-Content "$out/$name-hdr/metadata.json" -Raw | ConvertFrom-Json
        if($metadata.scene.preview_approx.enabled -ne $enabled -or $metadata.scene.preview_approx.strength -ne 0.5){throw "Approximation metadata mismatch: $name"}
        if(-not $metadata.cached_density -or ($metadata.actual_density_extent -join ',') -ne '128,128,128'){throw "Approximation comparison did not use the frozen 128 grid: $name"}
        $image=[System.Drawing.Image]::FromFile("$out/$name.bmp")
        try{$image.Save("$out/$name.png",[System.Drawing.Imaging.ImageFormat]::Png)}finally{$image.Dispose()}
    }finally{if(!$process.HasExited){Stop-Process -Id $process.Id -Force}}
}
# The default comparator enforces unchanged primary T. --report would disable
# that gate and could conceal an approximation that modifies extinction.
$difference=& $compareExe "$out/disabled-hdr/linear.exr" "$out/enabled-hdr/linear.exr"
if($LASTEXITCODE -ne 0){throw 'Approximation comparison failed'}
$difference | Out-File "$out/difference.txt"
if($difference -notmatch 'linear_RGB_max=([0-9.eE+-]+)' -or [double]::Parse($Matches[1],[System.Globalization.CultureInfo]::InvariantCulture) -le 1e-7){throw 'Approximation toggle produced no measurable radiance change'}
