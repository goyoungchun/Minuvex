param([Parameter(Mandatory=$true)][string]$OutputRoot)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null
$scripts = Join-Path $PSScriptRoot '../../benchmarks'
$checks = 0
function Assert($condition, $message) { if (-not $condition) { throw $message }; $script:checks++ }
function Rejects([scriptblock]$operation) { try { & $operation } catch { $script:checks++; return }; throw 'Invalid report was accepted.' }
# Synthetic contract fixtures only. These files are not benchmark/evaluation results.
$a = [ordered]@{format='MinuvexBenchmark';version=1;engine='Minuvex';engineVersion='fixture';scenario='shared-cubes-v1';objects=10;width=1280;height=720;msaa=1;vsync=$false;shadows=$false;warmupFrames=5;measuredFrames=10;backend='d3d11';gpu='fixture';cadenceMeanMs=2;cadenceP95Ms=3}
$b = [ordered]@{}; foreach ($key in $a.Keys) { $b[$key]=$a[$key] }; $b.engine='Unity';$b.cadenceMeanMs=4
$fa=Join-Path $OutputRoot 'fixture-minuvex.json'; $fb=Join-Path $OutputRoot 'fixture-unity.json'; $result=Join-Path $OutputRoot 'fixture-comparison.json'
$a | ConvertTo-Json | Set-Content $fa -Encoding UTF8
$b | ConvertTo-Json | Set-Content $fb -Encoding UTF8
& (Join-Path $scripts 'Compare.ps1') -Minuvex $fa -Unity $fb -Output $result
Assert ((Get-Content -Raw $result | ConvertFrom-Json).cadenceMeanRatioMinuvexToUnity -eq .5) 'Comparison ratio failed.'
$b.width=1920; $b | ConvertTo-Json | Set-Content $fb -Encoding UTF8
Rejects { & (Join-Path $scripts 'Compare.ps1') -Minuvex $fa -Unity $fb -Output $result }
$b.width=1280;$b.cadenceMeanMs=0; $b | ConvertTo-Json | Set-Content $fb -Encoding UTF8
Rejects { & (Join-Path $scripts 'Compare.ps1') -Minuvex $fa -Unity $fb -Output $result }
$runs = @(
    [ordered]@{engine='Minuvex';runId='fixture-1';taskHash='same-task';model='same-model';modelSettings='same-settings';contextPolicy='public-docs-only';usageSource='synthetic contract fixture';completed=$true;estimated=$false;inputTokens=100;outputTokens=20;documentBytesRead=400;compileErrors=0;repairAttempts=0;buildAttempts=1;elapsedSeconds=10},
    [ordered]@{engine='Unity';runId='fixture-1';taskHash='same-task';model='same-model';modelSettings='same-settings';contextPolicy='public-docs-only';usageSource='synthetic contract fixture';completed=$true;estimated=$false;inputTokens=200;outputTokens=40;documentBytesRead=800;compileErrors=1;repairAttempts=1;buildAttempts=2;elapsedSeconds=20}
)
$usageFile=Join-Path $OutputRoot 'fixture-usage.json';$evaluated=Join-Path $OutputRoot 'fixture-evaluation.json'
$runs | ConvertTo-Json | Set-Content $usageFile -Encoding UTF8
& (Join-Path $scripts 'EvaluateAiUsage.ps1') -InputFile $usageFile -Output $evaluated
$evaluation=Get-Content -Raw $evaluated | ConvertFrom-Json
Assert ($evaluation.engines.Count -eq 2 -and ($evaluation.engines | Where-Object engine -eq 'Minuvex').firstBuildSuccess -eq 1) 'Actual-usage aggregation failed.'
$runs[1].taskHash='different';$runs | ConvertTo-Json | Set-Content $usageFile -Encoding UTF8
Rejects { & (Join-Path $scripts 'EvaluateAiUsage.ps1') -InputFile $usageFile -Output $evaluated }
$runs[1].taskHash='same-task';$runs[1].estimated=$true;$runs | ConvertTo-Json | Set-Content $usageFile -Encoding UTF8
Rejects { & (Join-Path $scripts 'EvaluateAiUsage.ps1') -InputFile $usageFile -Output $evaluated }
$payload=Join-Path $OutputRoot 'fixture-player';New-Item -ItemType Directory -Force -Path $payload | Out-Null
[IO.File]::WriteAllBytes((Join-Path (Resolve-Path $payload) 'player.bin'),[byte[]]@(1,2,3))
$deployment=Join-Path $OutputRoot 'fixture-deployment.json'
& (Join-Path $scripts 'MeasureDeployment.ps1') -Engine Minuvex -Root $payload -Prerequisites 'synthetic fixture' -Output $deployment
Assert ((Get-Content -Raw $deployment | ConvertFrom-Json).totalBytes -eq 3) 'Deployment inventory failed.'
Write-Output "$checks release-tool checks passed (synthetic fixtures only)."
