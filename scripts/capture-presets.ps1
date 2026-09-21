param([Parameter(Mandatory=$true)][string]$Executable,[Parameter(Mandatory=$true)][string]$Generator,[Parameter(Mandatory=$true)][string]$Compare,[string]$OutputDirectory="evidence/presets")
$ErrorActionPreference="Stop"
$exe=(Resolve-Path $Executable).Path
$generatorExe=(Resolve-Path $Generator).Path
$compareExe=(Resolve-Path $Compare).Path
New-Item -ItemType Directory -Force $OutputDirectory | Out-Null
$out=(Resolve-Path $OutputDirectory).Path
& $generatorExe $out > "$out/contract.log"
if($LASTEXITCODE -ne 0){throw "Versioned preset contract failed"}
Add-Type -AssemblyName System.Drawing

function Convert-Captures {
    foreach($path in Get-ChildItem $out -Filter "*.bmp"){
        $png=[System.IO.Path]::ChangeExtension($path.FullName,"png")
        if(Test-Path $png){continue}
        $image=[System.Drawing.Image]::FromFile($path.FullName)
        try{$image.Save($png,[System.Drawing.Imaging.ImageFormat]::Png)}finally{$image.Dispose()}
    }
}
function Run-Editor([string]$Name,[string[]]$Arguments){
    $p=Start-Process -FilePath $exe -ArgumentList $Arguments -WorkingDirectory $out -PassThru -RedirectStandardOutput "$out/$Name.stdout.log" -RedirectStandardError "$out/$Name.stderr.log"
    try{if(!$p.WaitForExit(240000)){throw "$Name timed out"};if($p.ExitCode -ne 0){throw "$Name failed: $($p.ExitCode)"}}finally{if(!$p.HasExited){Stop-Process -Id $p.Id -Force};Convert-Captures}
}
function Assert-Scene([string]$Recipe,[string]$Metadata){
    $saved=Get-Content "$out/$Recipe" -Raw | ConvertFrom-Json
    $metadataValue=Get-Content "$out/$Metadata/metadata.json" -Raw | ConvertFrom-Json
    if($metadataValue.scene.cloud.kind -ne "frozen"){throw "$Recipe lost frozen authority"}
    if(($saved | ConvertTo-Json -Depth 100 -Compress) -cne ($metadataValue.scene | ConvertTo-Json -Depth 100 -Compress)){throw "$Recipe changed during fresh-process reload/export"}
    if($metadataValue.cached_density -or $metadataValue.sun_tau_cache_resolution -ne 0 -or $metadataValue.empty_space_skipping){throw "$Recipe comparison unexpectedly used an approximation/cache"}
    return $metadataValue
}
function Render-Recipe([string]$Name,[string]$Recipe){
    Run-Editor $Name @("--recipe",$Recipe,"--frames","90","--render-width","192","--view-steps","80","--shadow-steps","8","--cache","0","--sun-cache","0","--capture","$Name.bmp","--export-hdr","$Name-hdr")
    $numeric=Get-Content "$out/$Name.stdout.log" -Raw
    foreach($marker in @("density_reference max_abs_error=","HDR verified","frozen_reload_generation_jobs=0")){if(!$numeric.Contains($marker)){throw "$Name missing numeric verification $marker"}}
    $metadata=Assert-Scene $Recipe "$Name-hdr"
    return $metadata
}
function Compare-Strict([string]$Name,[string]$First,[string]$Second){
    & $compareExe "$out/$First/linear.exr" "$out/$Second/linear.exr" --strict > "$out/$Name.txt"
    if($LASTEXITCODE -ne 0){throw "$Name failed strict GPU HDR/transmittance equality"}
}
function Assert-FinishEqual($First,$Second,[string]$Label){
    if(@($First.layers).Count -ne 3 -or @($Second.layers).Count -ne 3){throw "$Label lost the nonempty three-layer fixture"}
    if(($First | ConvertTo-Json -Depth 100 -Compress) -cne ($Second | ConvertTo-Json -Depth 100 -Compress)){throw "$Label changed finishing IDs, targets, masks, order or values"}
}

$manifest=Get-Content "$out/preset-manifest.json" -Raw | ConvertFrom-Json
if($manifest.preset_version -ne 1 -or $manifest.algorithm_version -ne 1 -or $manifest.fixtures.Count -ne 8){throw "Unexpected preset/version fixture contract"}
foreach($preset in @("cumulonimbus","wide","multiple","anvil")){
    $pair=@($manifest.fixtures | Where-Object {$_.preset -eq $preset})
    if($pair.Count -ne 2 -or @($pair.initial_hash | Select-Object -Unique).Count -ne 1 -or @($pair.wind | Select-Object -Unique).Count -ne 2){throw "$preset calm/wind examples changed the initial source or omitted a variant"}
    if(@($pair.content_hash | Select-Object -Unique).Count -ne 2){throw "$preset wind example did not produce a distinct selected structure"}
}

Run-Editor "input" @("--preset-test","--frames","300","--render-width","192","--view-steps","80","--shadow-steps","8","--cache","0","--sun-cache","0","--capture","input.bmp")
$log=Get-Content "$out/input.stdout.log" -Raw
foreach($marker in @(
    "preset_native=candidate_preview current_document_preserved=true render_revision_distinct=true shared_view=true reset_confirmation=true",
    "preset_native=discard_undo current_preserved=true candidate_restored=true",
    "preset_native=confirmed_reset single_undo=true current_restored=true",
    "preset_native=current_return document_unchanged=true revision_restored=true",
    "preset_native=detail_only generation_jobs=0 content_hash_stable=true",
    "preset_native=cancel previous_current_and_candidate_retained=true",
    "preset_native=stage_after_reset new_reset_intent=false detail_preserved=true layers_preserved=true optics_preserved=true unconfirmed_adoption=true",
    "preset_native=pending_reset identical_inputs=true older_candidate_adopted=true newer_reset_intent_preserved=true",
    "preset_native=adopt single_undo=true redo=true total_generation_jobs=5",
    "preset_native=clone_candidate current_preserved=true fresh_ids=true generation_jobs=0",
    "preset_native=clone_preview render_revision_distinct=true current_preserved=true",
    "preset_native=clone_adopt fresh_ids=true generation_jobs=0 single_undo=true redo=true"
)){if(!$log.Contains($marker)){throw "Missing $marker"}}
foreach($marker in @("finishing_reset_guard","finishing_added","finishing_detail_retained","finishing_stage_retained","finishing_scoped_retained","finishing_adopt_retained","clone_finishing_remapped","clone_finishing_adopt_retained")){
    if(!$log.Contains("preset_native=$marker ")){throw "Missing nonempty finishing lifecycle marker $marker"}
}

$states=@(
    @{frame=100;name="candidate"},@{frame=125;name="current"},@{frame=140;name="detail"},
    @{frame=180;name="scoped"},@{frame=235;name="adopted"},@{frame=255;name="clone"},@{frame=285;name="clone-adopted"}
)
foreach($state in $states){
    $name=$state.name;$frame=$state.frame;$recipe="preset-$name.white.json"
    if(!$log.Contains("preset_capture frame=$frame scene=$name ")){throw "Missing actual preset capture $frame/$name"}
    if(!(Test-Path "$out/preset-$frame.png")){throw "Missing Windows screenshot for $frame/$name"}
    $null=Assert-Scene $recipe "preset-$frame-hdr"
    $null=Render-Recipe "reopened-$name" $recipe
    Compare-Strict "native-reload-$name" "preset-$frame-hdr" "reopened-$name-hdr"
}

$detailBase=Get-Content "$out/preset-candidate.white.json" -Raw | ConvertFrom-Json
$finishBase=Get-Content "$out/preset-finishing-base.white.json" -Raw | ConvertFrom-Json
$detail=Get-Content "$out/preset-detail.white.json" -Raw | ConvertFrom-Json
if($detailBase.cloud.source.content_hash -cne $detail.cloud.source.content_hash -or ($detailBase.cloud.source.selection | ConvertTo-Json -Depth 100 -Compress) -cne ($detail.cloud.source.selection | ConvertTo-Json -Depth 100 -Compress)){throw "Detail-only regeneration changed structure/selection"}
Assert-FinishEqual $finishBase.cloud.finishing $detail.cloud.finishing "Detail regeneration"
$stage=Render-Recipe "reopened-stage-preserved" "preset-stage-preserved.white.json"
Assert-FinishEqual $finishBase.cloud.finishing $stage.scene.cloud.finishing "Stage regeneration"
if(($stage.scene.cloud.source.optics | ConvertTo-Json -Depth 100 -Compress) -cne ($detail.cloud.source.optics | ConvertTo-Json -Depth 100 -Compress)){throw "Stage-only regeneration reset current optics"}
for($i=0;$i -lt $detail.cloud.source.fields.Count;$i++){
    $before=$detail.cloud.source.fields[$i];$after=$stage.scene.cloud.source.fields[$i]
    if($before.development_id -cne $after.development_id -or $before.recipe.detail_seed -cne $after.recipe.detail_seed -or ($before.layers | ConvertTo-Json -Depth 100 -Compress) -cne ($after.layers | ConvertTo-Json -Depth 100 -Compress) -or ($before.recipe.noise | ConvertTo-Json -Depth 100 -Compress) -cne ($after.recipe.noise | ConvertTo-Json -Depth 100 -Compress)){throw "Stage-only regeneration reset fixed-field detail/layers"}
}
$adopted=Get-Content "$out/preset-adopted.white.json" -Raw | ConvertFrom-Json
$scoped=Get-Content "$out/preset-scoped.white.json" -Raw | ConvertFrom-Json
$clone=Get-Content "$out/preset-clone.white.json" -Raw | ConvertFrom-Json
Assert-FinishEqual $finishBase.cloud.finishing $scoped.cloud.finishing "Scoped candidate"
Assert-FinishEqual $finishBase.cloud.finishing $adopted.cloud.finishing "Scoped adoption"
if($adopted.cloud.source.id -eq $clone.cloud.source.id -or $adopted.cloud.source.content_hash -eq $clone.cloud.source.content_hash){throw "Whole-cloud clone reused the original identity"}
$targetMap=@{};$targetMap[$adopted.cloud.source.id]=$clone.cloud.source.id
for($i=0;$i -lt $adopted.cloud.source.fields.Count;$i++){$targetMap[$adopted.cloud.source.fields[$i].development_id]=$clone.cloud.source.fields[$i].development_id}
if(@($clone.cloud.finishing.layers).Count -ne 3 -or @($clone.cloud.finishing.layers.id | Select-Object -Unique).Count -ne 3){throw "Clone lost unique finishing layers"}
$oldLayerIds=@($adopted.cloud.finishing.layers.id)
for($i=0;$i -lt $adopted.cloud.finishing.layers.Count;$i++){
    $before=$adopted.cloud.finishing.layers[$i];$after=$clone.cloud.finishing.layers[$i]
    if($oldLayerIds -contains $after.id -or !$targetMap.ContainsKey($before.target.id)){throw "Clone reused a layer identity or lost an internal target mapping"}
    $expected=$before | ConvertTo-Json -Depth 100 -Compress | ConvertFrom-Json
    $expected.id=$after.id;$expected.target.id=$targetMap[$before.target.id]
    if(($expected | ConvertTo-Json -Depth 100 -Compress) -cne ($after | ConvertTo-Json -Depth 100 -Compress)){throw "Clone changed finishing values/masks/order instead of only remapping IDs"}
}
$cloneAdopted=Get-Content "$out/preset-clone-adopted.white.json" -Raw | ConvertFrom-Json
Assert-FinishEqual $clone.cloud.finishing $cloneAdopted.cloud.finishing "Clone adoption"
Compare-Strict "adopted-clone-same-appearance" "reopened-adopted-hdr" "reopened-clone-hdr"
Compare-Strict "clone-adoption-same-appearance" "reopened-clone-hdr" "reopened-clone-adopted-hdr"

foreach($fixture in $manifest.fixtures){
    $metadata=Render-Recipe $fixture.name $fixture.recipe
    if($metadata.scene.cloud.source.content_hash -cne $fixture.content_hash -or $metadata.scene.cloud.source.payload_hash -cne $fixture.payload_hash){throw "$($fixture.name) lost its evaluated field identity"}
}
foreach($preset in @("cumulonimbus","wide","multiple","anvil")){
    & $compareExe "$out/$preset-calm-hdr/linear.exr" "$out/$preset-wind-hdr/linear.exr" --report-only > "$out/$preset-wind-difference.txt"
    if($LASTEXITCODE -ne 0){throw "$preset calm/wind visual comparison failed"}
}
"Native preset/seed/clone lifecycle with actual Current/Candidate rendering, shared view, explicit reset confirmation, discard/Undo, cancellation and five total growth jobs. Seven native HDR checkpoints are strictly compared to separate-process reloads with exact Scene metadata and zero growth. A nonempty three-layer finish stack survives detail/stage/scoped generation and adoption; resetting its identity is rejected, and clone remaps layer/target IDs while preserving masks/order/values and rendered appearance. Eight versioned calm/wind fixtures share each initial source and undergo numeric density/HDR verification. PNGs are actual app screenshots; physical RTX performance and naturalness still need separate review." | Set-Content "$out/README.txt"
