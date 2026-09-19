param(
    [Parameter(Mandatory=$true)][string]$Executable,
    [string]$OutputDirectory="evidence/gate",
    [ValidateSet(160,640)][int]$InternalWidth=160,
    [int]$ViewSteps=64,
    [int]$ShadowSteps=8,
    [switch]$ExportHdr,
    [string]$RecipeDirectory="$PSScriptRoot/../tests/fixtures/gate"
)
$ErrorActionPreference="Stop"
Add-Type -AssemblyName System.Drawing
$exe=(Resolve-Path $Executable).Path
$recipes=(Resolve-Path $RecipeDirectory).Path
New-Item -ItemType Directory -Force $OutputDirectory | Out-Null
$out=(Resolve-Path $OutputDirectory).Path
$primary=@("tall","wide","fusion","flat-base","cut","detail-17","detail-18")
foreach($source in Get-ChildItem "$recipes/*.white.json") {
    $name=$source.Name.Replace(".white.json","")
    Copy-Item $source.FullName "$out/$($source.Name)"
    $modes=if($primary -contains $name){@(0,128,256)}else{@(0)}
    foreach($grid in $modes){
        $stem="$name-cache-$grid"
        $arguments=@("--recipe",$source.Name,"--cache","$grid","--render-width","$InternalWidth","--view-steps","$ViewSteps","--shadow-steps","$ShadowSteps","--frames","90","--capture","$stem.bmp")
        if($ExportHdr){$arguments+=@("--export-hdr","$stem-hdr")}
        $process=Start-Process -FilePath $exe -ArgumentList $arguments -WorkingDirectory $out -PassThru -RedirectStandardOutput "$out/$stem.stdout.log" -RedirectStandardError "$out/$stem.stderr.log"
        try {
            if(!$process.WaitForExit(180000)){throw "Fixed gate case timed out: $stem"}
            if($process.ExitCode -ne 0){throw "Fixed gate case failed: $stem ($($process.ExitCode))"}
            $log=Get-Content "$out/$stem.stdout.log" -Raw
            $height=[int]($InternalWidth*9/16)
            if($log -notmatch "fixed_capture width=$InternalWidth height=$height .*pending=0"){throw "Wrong dimensions or unfinished bake: $stem"}
            $img=[System.Drawing.Image]::FromFile("$out/$stem.bmp")
            try{$img.Save("$out/$stem.png",[System.Drawing.Imaging.ImageFormat]::Png)}finally{$img.Dispose()}
        } finally {if(!$process.HasExited){Stop-Process -Id $process.Id -Force}}
    }
}
Get-ChildItem "$out/*.white.json" | Get-FileHash -Algorithm SHA256 | Select-Object Hash,@{Name="Recipe";Expression={Split-Path $_.Path -Leaf}} | Export-Csv "$out/recipe-sha256.csv" -NoTypeInformation
Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion,AdapterRAM | Format-List | Out-File "$out/adapters.txt"
"internal_width=$InternalWidth`nview_steps=$ViewSteps`nshadow_steps=$ShadowSteps`nframes=90`ncapture_frame=60`nwarmup_frames=60`ntimed_dirty_frames=see_each_log`nperformance_gate=not-established`ngpu_timestamp=unavailable`nactual_gpu_residency=unavailable" | Out-File "$out/protocol.txt"
