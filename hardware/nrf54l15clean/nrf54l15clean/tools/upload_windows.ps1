param(
    [Parameter(Mandatory = $true)] [string] $HexPath,
    [Parameter(Mandatory = $true)] [string] $Port,
    [Parameter(Mandatory = $true)] [string] $Target,
    [string] $NrfOcd = "",
    [int] $Attempts = 2,
    [int] $RetryDelayMs = 1000
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $HexPath -PathType Leaf)) {
    throw "HEX image not found: $HexPath"
}
if ([string]::IsNullOrWhiteSpace($NrfOcd)) {
    $NrfOcd = Join-Path $PSScriptRoot "nrf_ocd.exe"
}
if (-not (Test-Path -LiteralPath $NrfOcd -PathType Leaf)) {
    throw "Bundled nrf_ocd executable not found: $NrfOcd"
}
if ($Target -eq "nrf54l") {
    $Target = "nrf54l15"
}

$Attempts = [Math]::Max(1, $Attempts)
$RetryDelayMs = [Math]::Max(0, $RetryDelayMs)
$flashArgs = @(
    "-p", $Port,
    "-t", $Target,
    "-e", "chip",
    "-R",
    "--no-verify",
    "load", $HexPath
)

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

# nrf_ocd reset leaves C_DEBUGEN asserted on some CMSIS-DAP firmware. Make the
# last debug access clear DHCSR so subsequent System OFF calls use real hardware
# behavior instead of debugger emulation. Bytes are little-endian 0xA05F0000.
& $NrfOcd "-p" $Port "-t" $Target "write" "0xE000EDF0" "00005FA0"
$detachExitCode = $LASTEXITCODE
if ($detachExitCode -ne 0) {
    Write-Warning (
        "Firmware was written successfully, but the best-effort debugger " +
        "detach returned exit code $detachExitCode"
    )
}

Write-Host "Upload complete"
exit 0
