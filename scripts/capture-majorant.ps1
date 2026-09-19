param([Parameter(Mandatory=$true)][string]$Executable,[Parameter(Mandatory=$true)][string]$Compare)
$ErrorActionPreference="Stop"
Add-Type -AssemblyName System.Drawing
$exe=(Resolve-Path $Executable).Path
$compareExe=(Resolve-Path $Compare).Path
New-Item -ItemType Directory -Force evidence/majorant | Out-Null
$out=(Resolve-Path evidence/majorant).Path
foreach($source in Get-ChildItem "$PSScriptRoot/../tests/fixtures/sun/*.white.json"){
    Copy-Item $source.FullName $out
    $name=$source.Name.Replace('.white.json','')
    foreach($resolution in @(0,1)){
        $stem="$name-skip-$resolution"
        $arguments=@('--recipe',$source.Name,'--cache','128','--frames','90','--capture',"$stem.bmp",'--export-hdr',"$stem-hdr")
        if($resolution){$arguments+='--empty-skip'}
        $process=Start-Process $exe -ArgumentList $arguments -WorkingDirectory $out -PassThru -RedirectStandardOutput "$out/$stem.stdout.log" -RedirectStandardError "$out/$stem.stderr.log"
        try {
            if(!$process.WaitForExit(240000)){throw "Sun case timed out: $stem"}
            if($process.ExitCode -ne 0){throw "Sun case failed: $stem"}
            $image=[System.Drawing.Image]::FromFile("$out/$stem.bmp")
            try{$image.Save("$out/$stem.png",[System.Drawing.Imaging.ImageFormat]::Png)}finally{$image.Dispose()}
            if($resolution -ne 0){
                $difference=& $compareExe "$out/$name-skip-0-hdr/linear.exr" "$out/$stem-hdr/linear.exr" --strict
                if($LASTEXITCODE -ne 0){throw "HDR comparison failed: $stem"}
                "$stem $difference" | Add-Content "$out/comparison.txt"
            }
        }finally{if(!$process.HasExited){Stop-Process -Id $process.Id -Force}}
    }
}
