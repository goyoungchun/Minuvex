param(
    [Parameter(Mandatory=$true)][string]$Minuvex,
    [Parameter(Mandatory=$true)][string]$Unity,
    [Parameter(Mandatory=$true)][string]$Output
)
$ErrorActionPreference = 'Stop'
$a = Get-Content -Raw -LiteralPath $Minuvex | ConvertFrom-Json
$b = Get-Content -Raw -LiteralPath $Unity | ConvertFrom-Json
foreach ($report in @($a,$b)) {
    if ($report.format -ne 'MinuvexBenchmark' -or $report.version -ne 1) { throw 'Unsupported benchmark report.' }
    foreach ($field in @('cadenceMeanMs','cadenceP95Ms')) {
        if ($null -eq $report.$field -or $report.$field -is [string] -or -not ([double]$report.$field -gt 0) -or [double]::IsInfinity([double]$report.$field)) { throw "Invalid $field" }
    }
}
if ($a.engine -ne 'Minuvex' -or $b.engine -ne 'Unity') { throw 'Expected actual Minuvex and Unity reports.' }
foreach ($field in @('scenario','objects','width','height','msaa','vsync','shadows','warmupFrames','measuredFrames','backend','gpu')) {
    if ($null -eq $a.$field -or $null -eq $b.$field -or $a.$field -cne $b.$field) { throw "Incompatible comparison field: $field" }
}
$result = [ordered]@{
    format='MinuvexComparison';version=1;scenario=$a.scenario
    minuvexVersion=$a.engineVersion;unityVersion=$b.engineVersion
    workload=[ordered]@{objects=$a.objects;width=$a.width;height=$a.height;backend=$a.backend;gpu=$a.gpu}
    cadenceMeanRatioMinuvexToUnity=([double]$a.cadenceMeanMs / [double]$b.cadenceMeanMs)
    cadenceP95RatioMinuvexToUnity=([double]$a.cadenceP95Ms / [double]$b.cadenceP95Ms)
    notes=@('Ratios describe this run only, not an engine-wide advantage.','Submission CPU time and latest delayed GPU time have different scopes; they are not ranked.','Procedural scene; model loading is unmeasured. Run on the same idle machine and repeat before drawing conclusions.')
}
$result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $Output -Encoding UTF8
