param([Parameter(Mandatory=$true)][string]$Executable,[string]$OutputDirectory="evidence/prefab")
$ErrorActionPreference="Stop"
$exe=(Resolve-Path $Executable).Path
New-Item -ItemType Directory -Force $OutputDirectory | Out-Null
$out=(Resolve-Path $OutputDirectory).Path
$process=Start-Process -FilePath $exe -ArgumentList @("--prefab-test","--frames","260","--render-width","160","--view-steps","64","--shadow-steps","8","--capture","prefab-initial.bmp") -WorkingDirectory $out -PassThru -RedirectStandardOutput "$out/prefab.stdout.log" -RedirectStandardError "$out/prefab.stderr.log"
try {
    if(!$process.WaitForExit(180000)){throw "Prefab input smoke timed out"}
    if($process.ExitCode -ne 0){throw "Prefab input smoke failed: $($process.ExitCode)"}
    $log=Get-Content "$out/prefab.stdout.log" -Raw
    foreach($name in @("height","width","cloud_base")) {
        if($log -notmatch "prefab_gizmo=$name .*command_equal=true single_undo=true redo=true PASS"){throw "Missing $name gizmo/command verification"}
    }
    if($log -notmatch "prefab_source_preservation=true source_only_save_reload=true conversion_undo=true PASS"){throw "Missing prefab source preservation verification"}
    $scene=Get-Content "$out/prefab-smoke.white.json" -Raw | ConvertFrom-Json
    if($scene.schema_version -ne 10 -or $scene.cloud.kind -ne "cumulonimbus"){throw "Prefab was not saved as a versioned source"}
    if($scene.cloud.PSObject.Properties.Name -contains "cells"){throw "Prefab save contains a duplicate generated Recipe"}
    Add-Type -AssemblyName System.Drawing
    foreach($name in @("prefab-initial","prefab-116","prefab-156","prefab-196","prefab-225")) {
        $image=[System.Drawing.Image]::FromFile("$out/$name.bmp")
        try {$image.Save("$out/$name.png",[System.Drawing.Imaging.ImageFormat]::Png)}finally{$image.Dispose()}
    }
    "Actual Windows editor captures: creation, height/width/base injected ImGuizmo input, preserved local edit and source-only persistence. Same typed commands and one-step Undo/Redo checked in app. Physical GPU review deferred." | Set-Content "$out/README.txt"
}finally {
    if(!$process.HasExited){Stop-Process -Id $process.Id -Force}
}
