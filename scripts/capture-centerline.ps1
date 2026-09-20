param([Parameter(Mandatory=$true)][string]$Executable,[Parameter(Mandatory=$true)][string]$Compare,[string]$OutputDirectory="evidence/centerline")
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
Run-Editor "input" @("--centerline-test","--frames","210","--capture","centerline-initial.bmp")
$log=Get-Content "$out/input.stdout.log" -Raw
if($log -notmatch "centerline_gizmo=control_x command_equal=true single_undo=true redo=true stable_source=true PASS"){throw "Missing actual point gizmo/command verification"}
if($log -notmatch "centerline_profile_source=true flat_base_preserved=true noise_origin_preserved=true save_reload=true PASS"){throw "Missing profile source/Save/Open verification"}
$scene=Get-Content "$out/centerline-smoke.white.json" -Raw | ConvertFrom-Json
if($scene.schema_version -ne 9 -or $scene.algorithm_version -ne 3 -or $scene.cloud.kind -ne "centerline"){throw "Centerline source version mismatch"}
foreach($view in @("front","side")) {
    $recipe=if($view -eq "front"){"centerline-smoke.white.json"}else{"centerline-side.white.json"}
    foreach($cache in @(0,128,256)) {
        $name="$view-cache-$cache"
        Run-Editor $name @("--recipe",$recipe,"--frames","90","--render-width","160","--view-steps","64","--shadow-steps","8","--cache","$cache","--capture","$name.bmp","--export-hdr","$name-hdr")
        if($cache -ne 0) {
            & $compareExe "$out/$view-cache-0-hdr/linear.exr" "$out/$name-hdr/linear.exr" --report-only > "$out/$name-comparison.txt"
            if($LASTEXITCODE -ne 0){throw "Centerline cache comparison failed"}
        }
    }
}
Run-Editor "density-slice" @("--recipe","centerline-smoke.white.json","--frames","90","--diagnostic","1","--cache","128","--capture","density-slice.bmp","--export-hdr","density-slice-hdr")
Add-Type -AssemblyName System.Drawing
foreach($path in Get-ChildItem $out -Filter "*.bmp") {
    $image=[System.Drawing.Image]::FromFile($path.FullName)
    try{$image.Save([System.IO.Path]::ChangeExtension($path.FullName,"png"),[System.Drawing.Imaging.ImageFormat]::Png)}finally{$image.Dispose()}
}
"Actual Windows centerline: source commands/gizmo, front+side Direct/128/256 cache views and density slice. Raw EXR comparisons separate bake/interpolation error; density GPU validation remains active. Physical device review deferred." | Set-Content "$out/README.txt"
