$ErrorActionPreference = "Stop"

function Assert-Equal($Actual, $Expected, [string] $Label) {
    if ($Actual -ne $Expected) {
        throw "$Label`: expected $Expected, got $Actual"
    }
}

function Convert-HexUInt32([string] $Value) {
    return [Convert]::ToUInt32($Value, 16)
}

$root = Split-Path -Parent $PSScriptRoot
$tools = Join-Path $root "hardware/nrf54l15clean/nrf54l15clean/tools"
$temp = Join-Path ([IO.Path]::GetTempPath()) ("nrf54-native-" + [Guid]::NewGuid())
New-Item -ItemType Directory -Path $temp | Out-Null

try {
    [byte[]] $fixture = New-Object byte[] 300
    for ($index = 0; $index -lt $fixture.Length; ++$index) {
        $fixture[$index] = [byte] ($index -band 0xFF)
    }
    $binary = Join-Path $temp "fixture.bin"
    $uf2 = Join-Path $temp "fixture.uf2"
    [IO.File]::WriteAllBytes($binary, $fixture)

    & (Join-Path $tools "uf2/uf2_emit.ps1") `
        -InputPath $binary `
        -OutputPath $uf2 `
        -Family "0xADA54B15" `
        -BaseAddress "0x1000"

    [byte[]] $encoded = [IO.File]::ReadAllBytes($uf2)
    [uint32] $magic0 = Convert-HexUInt32 "0A324655"
    [uint32] $magic1 = Convert-HexUInt32 "9E5D5157"
    [uint32] $familyFlag = Convert-HexUInt32 "00002000"
    [uint32] $familyId = Convert-HexUInt32 "ADA54B15"
    [uint32] $endMagic = Convert-HexUInt32 "0AB16F30"
    Assert-Equal $encoded.Length 1024 "UF2 byte length"
    for ($blockNumber = 0; $blockNumber -lt 2; ++$blockNumber) {
        $offset = $blockNumber * 512
        Assert-Equal ([BitConverter]::ToUInt32($encoded, $offset + 0)) $magic0 "magic0"
        Assert-Equal ([BitConverter]::ToUInt32($encoded, $offset + 4)) $magic1 "magic1"
        Assert-Equal ([BitConverter]::ToUInt32($encoded, $offset + 8)) $familyFlag "family flag"
        Assert-Equal ([BitConverter]::ToUInt32($encoded, $offset + 12)) (0x1000 + 256 * $blockNumber) "target address"
        Assert-Equal ([BitConverter]::ToUInt32($encoded, $offset + 16)) 256 "payload size"
        Assert-Equal ([BitConverter]::ToUInt32($encoded, $offset + 20)) $blockNumber "block number"
        Assert-Equal ([BitConverter]::ToUInt32($encoded, $offset + 24)) 2 "block count"
        Assert-Equal ([BitConverter]::ToUInt32($encoded, $offset + 28)) $familyId "family ID"
        Assert-Equal ([BitConverter]::ToUInt32($encoded, $offset + 508)) $endMagic "end magic"
    }
    for ($index = 0; $index -lt $fixture.Length; ++$index) {
        $block = [Math]::Floor($index / 256)
        $position = $block * 512 + 32 + ($index % 256)
        Assert-Equal $encoded[$position] $fixture[$index] "payload byte $index"
    }
    Write-Host "PASS Windows UF2 emitter"

    $mock = Join-Path $temp "fake_nrf_ocd.cmd"
    $mockLog = Join-Path $temp "nrf_ocd.log"
    $mockMark = Join-Path $temp "first_load_failed"
@'
@echo off
echo %*>>"%MOCK_NRF_LOG%"
if /I "%1"=="list" (
  if /I "%MOCK_NRF_LIST_MODE%"=="mixed" (
    echo   #   Probe/Board                                          Unique ID    Target
    echo --------------------------------------------------------------------------------
    echo   0   Seeed Studio XIAO nrf54 CMSIS-DAP                  761FDE87     nrf54l15
    echo   1   Seeed Studio XIAO nRF54LM20A CMSIS-DAP             3377B9D6     nrf54lm20a
  )
  if /I "%MOCK_NRF_LIST_MODE%"=="two_l15" (
    echo   #   Probe/Board                                          Unique ID    Target
    echo --------------------------------------------------------------------------------
    echo   0   Seeed Studio XIAO nrf54 CMSIS-DAP                  761FDE87     nrf54l15
    echo   1   Seeed Studio XIAO nrf54 CMSIS-DAP                  A1B2C3D4     nrf54l15
  )
  if not "%MOCK_NRF_LIST_EXIT%"=="" exit /b %MOCK_NRF_LIST_EXIT%
  exit /b 0
)
if "%MOCK_NRF_ALWAYS_FAIL%"=="1" exit /b 23
echo %* | findstr /C:" load " >nul
if not errorlevel 1 if not exist "%MOCK_NRF_MARK%" (
  type nul >"%MOCK_NRF_MARK%"
  exit /b 23
)
exit /b 0
'@ | Set-Content -LiteralPath $mock -Encoding Ascii

    $hex = Join-Path $temp "fixture.hex"
    Set-Content -LiteralPath $hex -Value ":00000001FF" -Encoding Ascii
    $env:MOCK_NRF_LOG = $mockLog
    $env:MOCK_NRF_MARK = $mockMark
    $env:MOCK_NRF_ALWAYS_FAIL = "0"
    $env:MOCK_NRF_LIST_MODE = ""
    $env:MOCK_NRF_LIST_EXIT = ""
    & powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass `
        -File (Join-Path $tools "upload_windows.ps1") `
        -HexPath $hex -Port "COM42" -Target "nrf54l" `
        -NrfOcd $mock -RetryDelayMs 0
    Assert-Equal $LASTEXITCODE 0 "native upload wrapper exit"
    $calls = @(Get-Content -LiteralPath $mockLog)
    $loadCalls = @($calls | Where-Object { $_ -match " load " })
    $aliasedCalls = @($calls | Where-Object { $_ -match "-t nrf54l15" })
    Assert-Equal $loadCalls.Count 2 "erase/load attempts"
    Assert-Equal $aliasedCalls.Count 2 "target alias mapping"
    Assert-Equal @($calls | Where-Object {
        $_ -ne "list" -and $_ -notmatch " load "
    }).Count 0 "post-reset command count"
    Write-Host "PASS Windows native upload retry and command-free teardown"

    Remove-Item -LiteralPath $mockMark, $mockLog -Force
    $env:MOCK_NRF_ALWAYS_FAIL = "1"
    & powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass `
        -File (Join-Path $tools "upload_windows.ps1") `
        -HexPath $hex -Port "COM42" -Target "nrf54lm20a" `
        -NrfOcd $mock -RetryDelayMs 0
    $failedExitCode = $LASTEXITCODE
    $global:LASTEXITCODE = 0
    Assert-Equal $failedExitCode 23 "native upload final failure propagation"
    $failedCalls = @(Get-Content -LiteralPath $mockLog)
    $failedLoadCalls = @($failedCalls | Where-Object { $_ -match " load " })
    Assert-Equal $failedLoadCalls.Count 2 "failed upload attempt count"
    Write-Host "PASS Windows native upload failure propagation"

    Remove-Item -LiteralPath $mockMark, $mockLog -Force -ErrorAction SilentlyContinue
    $env:MOCK_NRF_ALWAYS_FAIL = "0"
    $env:MOCK_NRF_LIST_MODE = "mixed"
    & powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass `
        -File (Join-Path $tools "upload_windows.ps1") `
        -HexPath $hex -Port "COM404" -Target "nrf54lm20a" `
        -NrfOcd $mock -RetryDelayMs 0
    Assert-Equal $LASTEXITCODE 0 "target-matched UID upload exit"
    $uidCalls = @(Get-Content -LiteralPath $mockLog)
    $uidTransferCalls = @($uidCalls | Where-Object { $_ -match " load " })
    Assert-Equal $uidTransferCalls.Count 2 "target-matched UID transfer calls"
    Assert-Equal @($uidTransferCalls | Where-Object {
        $_ -match "-u 3377B9D6" -and $_ -notmatch "-p "
    }).Count 2 "target-matched UID selector"
    Write-Host "PASS Windows upload bypasses a stale COM port with the matching UID"

    Remove-Item -LiteralPath $mockMark, $mockLog -Force -ErrorAction SilentlyContinue
    $env:MOCK_NRF_LIST_MODE = "two_l15"
    & powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass `
        -File (Join-Path $tools "upload_windows.ps1") `
        -HexPath $hex -Port "COM42" -Target "nrf54l" `
        -NrfOcd $mock -RetryDelayMs 0
    Assert-Equal $LASTEXITCODE 0 "ambiguous target port fallback exit"
    $portCalls = @(Get-Content -LiteralPath $mockLog)
    $portTransferCalls = @($portCalls | Where-Object { $_ -match " load " })
    Assert-Equal @($portTransferCalls | Where-Object {
        $_ -match "-p COM42" -and $_ -notmatch "-u "
    }).Count 2 "ambiguous target port selector"
    Write-Host "PASS Windows upload uses the port for multiple matching probes"

    Remove-Item -LiteralPath $mockMark, $mockLog -Force -ErrorAction SilentlyContinue
    & powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass `
        -File (Join-Path $tools "upload_windows.ps1") `
        -HexPath $hex -Target "nrf54l15" `
        -NrfOcd $mock -RetryDelayMs 0
    $ambiguousExitCode = $LASTEXITCODE
    $global:LASTEXITCODE = 0
    if ($ambiguousExitCode -eq 0) {
        throw "ambiguous target without a selector unexpectedly succeeded"
    }
    $ambiguousCalls = @(Get-Content -LiteralPath $mockLog)
    Assert-Equal @($ambiguousCalls | Where-Object { $_ -match " load " }).Count 0 `
        "ambiguous target load count"
    Write-Host "PASS Windows upload refuses to guess between matching probes"

    Remove-Item -LiteralPath $mockLog -Force -ErrorAction SilentlyContinue
    & powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass `
        -File (Join-Path $tools "upload_windows.ps1") `
        -HexPath $hex -Target "nrf54l15" -Uid "A1B2C3D4" `
        -NrfOcd $mock -RetryDelayMs 0
    Assert-Equal $LASTEXITCODE 0 "explicit UID upload exit"
    $explicitCalls = @(Get-Content -LiteralPath $mockLog)
    Assert-Equal @($explicitCalls | Where-Object { $_ -eq "list" }).Count 0 `
        "explicit UID enumeration count"
    Assert-Equal @($explicitCalls | Where-Object {
        $_ -match " load " -and $_ -match "-u A1B2C3D4"
    }).Count 2 "explicit UID transfer selector"
    Write-Host "PASS Windows upload honors an explicit UID without enumeration"
}
finally {
    Remove-Item -LiteralPath $temp -Recurse -Force -ErrorAction SilentlyContinue
}
