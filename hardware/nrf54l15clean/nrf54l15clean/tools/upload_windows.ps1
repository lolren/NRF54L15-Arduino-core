param(
    [Parameter(Mandatory = $true)] [string] $HexPath,
    [string] $Port = "",
    [Parameter(Mandatory = $true)] [string] $Target,
    [string] $Uid = "",
    [string] $NrfOcd = "",
    [int] $Attempts = 2,
    [int] $RetryDelayMs = 1000
)

$ErrorActionPreference = "Stop"

function Test-UsableSelectorValue([string] $Value) {
    if ([string]::IsNullOrWhiteSpace($Value)) {
        return $false
    }
    return $Value.Trim() -notmatch '^\{.*\}$'
}

function Find-TargetProbeUids([string] $Executable, [string] $TargetName) {
    [string[]] $output = @(& $Executable list 2>&1)
    $listExitCode = $LASTEXITCODE
    if ($listExitCode -ne 0) {
        Write-Warning "nrf_ocd list failed with exit code $listExitCode"
        return @()
    }

    [string[]] $matches = @()
    foreach ($line in $output) {
        $match = [regex]::Match(
            [string] $line,
            '^\s*\d+\s+.+?\s+([0-9A-Fa-f]{8,64})\s+(nrf54l15|nrf54lm20a)\s*$'
        )
        if ($match.Success -and
            $match.Groups[2].Value.ToLowerInvariant() -eq $TargetName) {
            $matches += $match.Groups[1].Value
        }
    }
    return @($matches | Select-Object -Unique)
}

if (-not (Test-Path -LiteralPath $HexPath -PathType Leaf)) {
    throw "HEX image not found: $HexPath"
}
if ([string]::IsNullOrWhiteSpace($NrfOcd)) {
    $NrfOcd = Join-Path $PSScriptRoot "nrf_ocd.exe"
}
if (-not (Test-Path -LiteralPath $NrfOcd -PathType Leaf)) {
    throw "Bundled nrf_ocd executable not found: $NrfOcd"
}
if ([string]::IsNullOrWhiteSpace($Target)) {
    throw "Target must be nrf54l15 or nrf54lm20a"
}
$Target = $Target.Trim().ToLowerInvariant()
if ($Target -eq "nrf54l") {
    $Target = "nrf54l15"
}
if ($Target -ne "nrf54l15" -and $Target -ne "nrf54lm20a") {
    throw "Unsupported nrf_ocd target: $Target"
}

$selectorArgs = @()
if (Test-UsableSelectorValue $Uid) {
    $Uid = $Uid.Trim()
    $selectorArgs = @("-u", $Uid)
    Write-Host "Selecting CMSIS-DAP probe by configured UID $Uid"
} else {
    [string[]] $matchingUids = @(Find-TargetProbeUids $NrfOcd $Target)
    if ($matchingUids.Count -eq 1) {
        $selectorArgs = @("-u", $matchingUids[0])
        Write-Host "Selecting the detected $Target CMSIS-DAP probe $($matchingUids[0])"
    } elseif (Test-UsableSelectorValue $Port) {
        $Port = $Port.Trim()
        $selectorArgs = @("-p", $Port)
        if ($matchingUids.Count -gt 1) {
            Write-Host (
                "Multiple $Target probes detected; using Arduino port $Port " +
                "to select the intended board"
            )
        } else {
            Write-Host "Using Arduino port $Port to select the CMSIS-DAP probe"
        }
    } elseif ($matchingUids.Count -gt 1) {
        throw (
            "Multiple $Target CMSIS-DAP probes are connected ($($matchingUids -join ', ')). " +
            "Select a board port or configure its upload UID; refusing to guess."
        )
    } else {
        throw (
            "No $Target CMSIS-DAP probe or usable Arduino port was found. " +
            "Reconnect the board and check 'nrf_ocd list'."
        )
    }
}

$Attempts = [Math]::Max(1, $Attempts)
$RetryDelayMs = [Math]::Max(0, $RetryDelayMs)
$flashArgs = @($selectorArgs + @(
    "-t", $Target,
    "-e", "chip",
    "-R",
    "--no-verify",
    "load", $HexPath
))

$flashExitCode = 1
for ($attempt = 1; $attempt -le $Attempts; ++$attempt) {
    Write-Host "nrf_ocd upload attempt $attempt/$Attempts"
    & $NrfOcd @flashArgs
    $flashExitCode = $LASTEXITCODE
    if ($flashExitCode -eq 0) {
        break
    }
    if ($attempt -lt $Attempts) {
        Write-Warning (
            "nrf_ocd failed with exit code $flashExitCode; retrying the " +
            "protected-target erase/load sequence"
        )
        Start-Sleep -Milliseconds $RetryDelayMs
    }
}

if ($flashExitCode -ne 0) {
    exit $flashExitCode
}

Write-Host "Upload complete"
exit 0
