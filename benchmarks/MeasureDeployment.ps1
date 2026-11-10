param(
    [Parameter(Mandatory=$true)][ValidateSet('Minuvex','Unity')][string]$Engine,
    [Parameter(Mandatory=$true)][string]$Root,
    [Parameter(Mandatory=$true)][string]$Prerequisites,
    [Parameter(Mandatory=$true)][string]$Output
)
$ErrorActionPreference = 'Stop'
$directory = Get-Item -LiteralPath $Root
if (-not $directory.PSIsContainer -or ($directory.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Root must be a complete, runnable player deployment directory without links.' }
$payloadPrefix = $directory.FullName.TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar
if ([IO.Path]::GetFullPath($Output).StartsWith($payloadPrefix, [StringComparison]::OrdinalIgnoreCase)) { throw 'Write the report outside the measured payload.' }
# Reject links instead of silently counting files outside the chosen payload.
$entries = @(Get-ChildItem -LiteralPath $directory.FullName -Recurse -Force)
if (@($entries | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }).Count) { throw 'Deployment links/reparse points are not supported.' }
$files = @($entries | Where-Object { -not $_.PSIsContainer })
if ($files.Count -eq 0) { throw 'Empty deployment.' }
$inventory = @($files | ForEach-Object { [ordered]@{path=$_.FullName.Substring($directory.FullName.TrimEnd('\','/').Length + 1);bytes=$_.Length} })
[ordered]@{format='MinuvexDeployment';version=1;engine=$Engine;prerequisites=$Prerequisites
    totalBytes=($files | Measure-Object Length -Sum).Sum;fileCount=$files.Count;files=$inventory
    note='Uncompressed complete player payload; no SDK-vs-player ranking. Prerequisite runtimes are listed separately.'
} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $Output -Encoding UTF8
