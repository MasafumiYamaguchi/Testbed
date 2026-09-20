param([Parameter(Mandatory=$true)][string]$Executable,[Parameter(Mandatory=$true)][string]$Check)
$ErrorActionPreference="Stop"
Add-Type -AssemblyName System.Drawing
$exe=(Resolve-Path $Executable).Path
$checkExe=(Resolve-Path $Check).Path
New-Item -ItemType Directory -Force evidence/diagnostics | Out-Null
$out=(Resolve-Path evidence/diagnostics).Path
foreach($fixture in @('cut','empty')){
    Copy-Item "$PSScriptRoot/../tests/fixtures/sun/$fixture.white.json" $out
    $modes=if($fixture -eq 'cut'){@(1,2,3,4,5,6,7,8,9,10,11)}else{@(1,2,3,6,7,11)}
    foreach($mode in $modes){
        $stem="$fixture-mode-$mode"
        $arguments=@('--recipe',"$fixture.white.json",'--cache','128','--empty-skip','--sun-cache','32','--diagnostic',"$mode",'--frames','90','--capture',"$stem.bmp",'--export-hdr',"$stem-hdr")
        $process=Start-Process $exe -ArgumentList $arguments -WorkingDirectory $out -PassThru -RedirectStandardOutput "$out/$stem.stdout.log" -RedirectStandardError "$out/$stem.stderr.log"
        try{
            if(!$process.WaitForExit(240000)){throw "Diagnostic timeout: $stem"}
            if($process.ExitCode -ne 0){throw "Diagnostic failed: $stem"}
            $image=[System.Drawing.Image]::FromFile("$out/$stem.bmp")
            try{$image.Save("$out/$stem.png",[System.Drawing.Imaging.ImageFormat]::Png)}finally{$image.Dispose()}
            if($fixture -eq 'empty'){
                & $checkExe "$out/$stem-hdr/linear.exr" $(if($mode -eq 3){'1'}else{'0'}) | Add-Content "$out/analytic-checks.txt"
                if($LASTEXITCODE -ne 0){throw "Empty diagnostic mismatch: $stem"}
            }
        }finally{if(!$process.HasExited){Stop-Process -Id $process.Id -Force}}
    }
}
