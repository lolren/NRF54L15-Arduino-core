param(
    [Parameter(Mandatory = $true)][string]$BuildPath,
    [Parameter(Mandatory = $true)][ValidateSet("nrf54l15", "nrf54lm20b")][string]$Target,
    [Parameter(Mandatory = $true)][string]$PlatformPath
)

$ErrorActionPreference = "Stop"
$targets = @("nrf54l15", "nrf54lm20b")
$marker = Join-Path $BuildPath ".nrf54-clean-build-target"
New-Item -ItemType Directory -Force -Path $BuildPath | Out-Null

$reason = $null
$resolvedPlatform = [System.IO.Path]::GetFullPath($PlatformPath)
if (Test-Path $marker) {
    $markerLines = @(Get-Content $marker)
    $previous = if ($markerLines.Count -gt 0) { $markerLines[0].Trim() } else { "" }
    $previousPlatform = if ($markerLines.Count -gt 1) { $markerLines[1].Trim() } else { "" }
    if (($targets -contains $previous) -and ($previous -ne $Target)) {
        $reason = "target changed from $previous to $Target"
    } elseif (($previous.Length -gt 0) -and ($previousPlatform -ne $resolvedPlatform)) {
        $reason = "platform installation changed"
    }
} elseif ($null -ne (Get-ChildItem -Path $BuildPath -Filter "*.d" -File -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1)) {
    $reason = "existing cache predates target tracking"
}

if ($null -eq $reason) {
    $other = $targets | Where-Object { $_ -ne $Target } | Select-Object -First 1
    $pattern = "[\\/]cores[\\/]$other[\\/]"
    $dependency = Get-ChildItem -Path $BuildPath -Filter "*.d" -File -Recurse -ErrorAction SilentlyContinue |
        Select-String -Pattern $pattern -List | Select-Object -First 1
    if ($null -ne $dependency) {
        $reason = "dependencies contain $other objects"
    }
}

if ($null -ne $reason) {
    foreach ($directory in @("core", "libraries")) {
        Remove-Item -Recurse -Force -ErrorAction SilentlyContinue (Join-Path $BuildPath $directory)
    }

    $sketch = Join-Path $BuildPath "sketch"
    if (Test-Path $sketch) {
        Get-ChildItem -Path $sketch -File -Recurse -Include "*.o", "*.d", "*.a" |
            Remove-Item -Force -ErrorAction SilentlyContinue
    }

    Get-ChildItem -Path $BuildPath -File -ErrorAction SilentlyContinue |
        Where-Object {
            $_.Extension -in @(".elf", ".map", ".hex", ".bin", ".uf2", ".a") -or
            $_.Name -in @("includes.cache", "libraries.cache", "compile_commands.json")
        } | Remove-Item -Force -ErrorAction SilentlyContinue

    Write-Host "nRF54 cleared stale Arduino objects: $reason"
}

Set-Content -Encoding UTF8 -Path $marker -Value @($Target, $resolvedPlatform)
