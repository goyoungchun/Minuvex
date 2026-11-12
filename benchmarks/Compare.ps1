param(
    [Parameter(Mandatory=$true)][string]$Minuvex,
    [Parameter(Mandatory=$true)][string]$Unity,
    [Parameter(Mandatory=$true)][string]$Output
)
$ErrorActionPreference = 'Stop'
$a = Get-Content -Raw -LiteralPath $Minuvex | ConvertFrom-Json
$b = Get-Content -Raw -LiteralPath $Unity | ConvertFrom-Json
function RequireNumber($value, [double]$minimum, [double]$maximum, [bool]$integer, [string]$field) {
    if ($null -eq $value -or $value -isnot [ValueType] -or $value -is [bool]) { throw "Invalid numeric field: $field" }
    $number = [double]$value
    if ([double]::IsNaN($number) -or [double]::IsInfinity($number) -or $number -lt $minimum -or $number -gt $maximum -or ($integer -and [math]::Truncate($number) -ne $number)) { throw "Invalid numeric field: $field" }
}
foreach ($report in @($a,$b)) {
    if ($report.format -isnot [string] -or $report.format -cne 'MinuvexBenchmark') { throw 'Unsupported benchmark report.' }
    RequireNumber $report.version 2 2 $true 'version'
    foreach ($field in @('engine','engineVersion','scenario','backend','gpu')) {
        if ($report.$field -isnot [string] -or [string]::IsNullOrWhiteSpace($report.$field)) { throw "Missing $field" }
    }
    foreach ($field in @('objects','width','height','warmupFrames','measuredFrames')) { RequireNumber $report.$field 1 100000 $true $field }
    RequireNumber $report.msaa 1 8 $true 'msaa'
    if ($report.msaa -notin @(1,2,4,8) -or $report.vsync -isnot [bool] -or $report.shadows -isnot [bool]) { throw 'Invalid sample count or boolean setting.' }
    foreach ($field in @('lod','instancing','frustumCulling','postProcessing')) { if ($report.$field -isnot [bool]) { throw "Missing boolean setting: $field" } }
    if ($report.compression -isnot [string] -or $report.compression -cne 'none' -or $report.scenario -cne 'shared-cubes-v2-unbatched' -or $report.lod -or $report.instancing -or $report.postProcessing -or -not $report.frustumCulling) { throw 'Use the explicit v2 unbatched scenario settings.' }
    foreach ($field in @('cadenceMeanMs','cadenceP95Ms')) {
        RequireNumber $report.$field ([double]::Epsilon) ([double]::MaxValue) $false $field
    }
}
if ($a.engine -ne 'Minuvex' -or $b.engine -ne 'Unity') { throw 'Expected actual Minuvex and Unity reports.' }
foreach ($field in @('scenario','objects','width','height','msaa','vsync','shadows','warmupFrames','measuredFrames','backend','gpu','lod','instancing','frustumCulling','postProcessing','compression')) {
    if ($null -eq $a.$field -or $null -eq $b.$field -or $a.$field -cne $b.$field) { throw "Incompatible comparison field: $field" }
}
$result = [ordered]@{
    format='MinuvexComparison';version=2;scenario=$a.scenario
    minuvexVersion=$a.engineVersion;unityVersion=$b.engineVersion
    workload=[ordered]@{objects=$a.objects;width=$a.width;height=$a.height;backend=$a.backend;gpu=$a.gpu}
    cadenceMeanRatioMinuvexToUnity=([double]$a.cadenceMeanMs / [double]$b.cadenceMeanMs)
    cadenceP95RatioMinuvexToUnity=([double]$a.cadenceP95Ms / [double]$b.cadenceP95Ms)
    notes=@('Ratios describe this run only, not an engine-wide advantage.','Submission CPU time and latest delayed GPU time have different scopes; they are not ranked.','Procedural scene; model loading is unmeasured. Run on the same idle machine and repeat before drawing conclusions.')
}
$result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $Output -Encoding UTF8
