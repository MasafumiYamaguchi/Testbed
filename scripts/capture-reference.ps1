param([Parameter(Mandatory=$true)][string]$Executable,[string]$OutputDirectory="evidence/reference")
$ErrorActionPreference="Stop"
$exe=(Resolve-Path $Executable).Path
New-Item -ItemType Directory -Force $OutputDirectory | Out-Null
$out=(Resolve-Path $OutputDirectory).Path
function Assert-ReferenceBundle([string]$Directory,[uint64]$Seed,[int]$Samples,[int]$Width,[int]$Height,[bool]$Final){
    $meta=Get-Content (Join-Path $Directory "metadata.json") -Raw | ConvertFrom-Json
    if([bool]$meta.reference.complete -ne $Final -or -not $meta.reference.all_attempted_paths_complete){throw "Reference completion metadata mismatch: $Directory"}
    if($meta.samples_per_pixel -ne $Samples -or $meta.reference.completed_sample_planes -ne $Samples){throw "Reference sample metadata mismatch: $Directory"}
    if([string]$meta.reference.seed -ne [string]$Seed -or [string]$meta.jitter_seed -ne [string]$Seed){throw "Reference seed metadata mismatch: $Directory"}
    if($meta.width -ne $Width -or $meta.height -ne $Height -or $meta.reference.attempted_paths -ne ($Width*$Height*$Samples)){throw "Reference path count mismatch: $Directory"}
    if(($meta.actual_density_extent -join ',') -ne '32,32,32' -or ($meta.reference.grid_extent -join ',') -ne '32,32,32'){throw "Reference grid provenance mismatch: $Directory"}
    if(-not $meta.reference.pixel_jitter -or $meta.reference.event_limited_paths -ne 0 -or $meta.reference.bounce_limited_paths -ne 0 -or $meta.reference.numerical_failures -ne 0){throw "Reference invalid paths or sampling metadata: $Directory"}
    foreach($file in @('linear.exr','display.ppm')){if((Get-Item (Join-Path $Directory $file)).Length -le 32){throw "Reference image missing or empty: $Directory/$file"}}
}
$cases=@(
    @{Name="cloud-single";Fixture="cloud";Mode="single";Samples=128},
    @{Name="cloud-multiple";Fixture="cloud";Mode="multiple";Samples=128},
    @{Name="empty";Fixture="empty";Mode="multiple";Samples=32},
    @{Name="absorption";Fixture="absorption";Mode="multiple";Samples=64},
    @{Name="internal";Fixture="internal";Mode="multiple";Samples=64},
    @{Name="albedo-one";Fixture="albedo-one";Mode="multiple";Samples=64},
    @{Name="thick";Fixture="thick";Mode="multiple";Samples=128}
)
foreach($case in $cases){
    $destination=Join-Path $out $case.Name
    & $exe --output $destination --fixture $case.Fixture --mode $case.Mode --width 32 --height 18 --grid 32 --samples $case.Samples --seed 42 --bounce-limit 256 --checkpoint-every 32 > (Join-Path $out ($case.Name+".csv")) 2> (Join-Path $out ($case.Name+".stderr.log"))
    if($LASTEXITCODE -ne 0){throw "Reference $($case.Name) failed or partial: exit $LASTEXITCODE"}
    for($samples=32;$samples -le $case.Samples;$samples+=32){
        $final=$samples -eq $case.Samples
        $bundle=if($final){$destination}else{"$destination-spp-$samples"}
        Assert-ReferenceBundle -Directory $bundle -Seed 42 -Samples $samples -Width 32 -Height 18 -Final $final
    }
}
# Held-out independent seeds and increasing sample planes use a smaller image.
foreach($seed in @(17,99991,4294967313)){
    $name="cloud-seed-$seed"
    & $exe --output (Join-Path $out $name) --fixture cloud --mode multiple --width 16 --height 9 --grid 32 --samples 256 --seed $seed --checkpoint-every 64 > (Join-Path $out ($name+".csv")) 2> (Join-Path $out ($name+".stderr.log"))
    if($LASTEXITCODE -ne 0){throw "Reference seed $seed failed or partial"}
    for($samples=64;$samples -le 256;$samples+=64){
        $final=$samples -eq 256
        $bundle=Join-Path $out $(if($final){$name}else{"$name-spp-$samples"})
        Assert-ReferenceBundle -Directory $bundle -Seed $seed -Samples $samples -Width 16 -Height 9 -Final $final
    }
}
"CPU reference snapshots: 7 fixed fixtures + 3 independent seeds. RGB linear EXR, display PPM, full scene/settings/variance/limits in metadata.json; physical GPU validation deferred." | Set-Content (Join-Path $out "README.txt")
