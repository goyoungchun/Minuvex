param([Parameter(Mandatory=$true)][string]$InputFile,[Parameter(Mandatory=$true)][string]$Output)
$ErrorActionPreference = 'Stop'
$runs = @(Get-Content -Raw -LiteralPath $InputFile | ConvertFrom-Json)
if ($runs.Count -lt 2) { throw 'Supply at least one actual run per engine.' }
$first = $runs[0]; $keys = @{}
foreach ($run in $runs) {
    foreach ($field in @('engine','runId','taskHash','model','modelSettings','contextPolicy','usageSource')) {
        if ($run.$field -isnot [string] -or [string]::IsNullOrWhiteSpace($run.$field)) { throw "Missing $field" }
    }
    if (($run.engine -cne 'Minuvex' -and $run.engine -cne 'Unity') -or $run.completed -isnot [bool] -or $run.estimated -isnot [bool] -or $run.estimated) { throw 'Use actual provider token counts and explicit completion status.' }
    foreach ($field in @('taskHash','model','modelSettings','contextPolicy')) {
        if ($run.$field -cne $first.$field) { throw "Experiments must use the same $field" }
    }
    $key = $run.engine + ':' + $run.runId
    if ($keys.ContainsKey($key)) { throw 'Duplicate run ID.' }; $keys[$key]=$true
    foreach ($field in @('inputTokens','outputTokens','documentBytesRead','compileErrors','repairAttempts','buildAttempts','elapsedSeconds')) {
        $n=$run.$field
        if ($null -eq $n -or $n -isnot [ValueType] -or $n -is [bool] -or [double]::IsNaN([double]$n) -or [double]::IsInfinity([double]$n) -or [double]$n -lt 0) { throw "Invalid $field" }
        if ($field -ne 'elapsedSeconds' -and [math]::Truncate([double]$n) -ne [double]$n) { throw "Integer required: $field" }
    }
    if ($run.completed -and $run.buildAttempts -lt 1) { throw 'A completed integration must include a build attempt.' }
}
$groups = @($runs | Group-Object engine)
if ($groups.Count -ne 2) { throw 'Both engines are required; missing runs are not zero.' }
$minuvexIds=@($runs | Where-Object engine -ceq 'Minuvex' | ForEach-Object runId)
$unityIds=@($runs | Where-Object engine -ceq 'Unity' | ForEach-Object runId)
if (Compare-Object $minuvexIds $unityIds -CaseSensitive) { throw 'Pair each runId across both engines; unmatched experiments cannot be compared.' }
$summary = foreach ($group in $groups) {
    $items=@($group.Group)
    [ordered]@{engine=$group.Name;runs=$items.Count
        inputTokens=($items | Measure-Object inputTokens -Sum).Sum
        outputTokens=($items | Measure-Object outputTokens -Sum).Sum
        totalTokens=(($items | Measure-Object inputTokens -Sum).Sum + ($items | Measure-Object outputTokens -Sum).Sum)
        documentBytesRead=($items | Measure-Object documentBytesRead -Sum).Sum
        compileErrors=($items | Measure-Object compileErrors -Sum).Sum
        repairAttempts=($items | Measure-Object repairAttempts -Sum).Sum
        elapsedSeconds=($items | Measure-Object elapsedSeconds -Sum).Sum
        completed=@($items | Where-Object completed).Count
        completionRate=(@($items | Where-Object completed).Count / [double]$items.Count)
        firstBuildSuccess=@($items | Where-Object { $_.completed -and $_.buildAttempts -eq 1 -and $_.compileErrors -eq 0 }).Count
        firstBuildSuccessRate=(@($items | Where-Object { $_.completed -and $_.buildAttempts -eq 1 -and $_.compileErrors -eq 0 }).Count / [double]$items.Count)}
}
[ordered]@{format='MinuvexAiEvaluation';version=1;taskHash=$first.taskHash;model=$first.model
    modelSettings=$first.modelSettings;contextPolicy=$first.contextPolicy;engines=@($summary)
    note='Aggregates actual supplied experiments; document bytes are not token estimates.'
} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $Output -Encoding UTF8
