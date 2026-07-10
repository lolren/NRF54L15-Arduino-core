$ErrorActionPreference = "Stop"

function Assert-Equal($Actual, $Expected, [string] $Label) {
    if ($Actual -ne $Expected) {
        throw "$Label`: expected $Expected, got $Actual"
    }
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
    Assert-Equal $encoded.Length 1024 "UF2 byte length"
    for ($blockNumber = 0; $blockNumber -lt 2; ++$blockNumber) {
        $offset = $blockNumber * 512
        Assert-Equal ([BitConverter]::ToUInt32($encoded, $offset + 0)) 0x0A324655 "magic0"
        Assert-Equal ([BitConverter]::ToUInt32($encoded, $offset + 4)) 0x9E5D5157 "magic1"
        Assert-Equal ([BitConverter]::ToUInt32($encoded, $offset + 8)) 0x00002000 "family flag"
        Assert-Equal ([BitConverter]::ToUInt32($encoded, $offset + 12)) (0x1000 + 256 * $blockNumber) "target address"
        Assert-Equal ([BitConverter]::ToUInt32($encoded, $offset + 16)) 256 "payload size"
        Assert-Equal ([BitConverter]::ToUInt32($encoded, $offset + 20)) $blockNumber "block number"
        Assert-Equal ([BitConverter]::ToUInt32($encoded, $offset + 24)) 2 "block count"
        Assert-Equal ([BitConverter]::ToUInt32($encoded, $offset + 28)) 0xADA54B15 "family ID"
        Assert-Equal ([BitConverter]::ToUInt32($encoded, $offset + 508)) 0x0AB16F30 "end magic"
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
if "%MOCK_NRF_ALWAYS_FAIL%"=="1" exit /b 23
echo %* | findstr /C:"write 0xE000EDF0 00005FA0" >nul
if not errorlevel 1 if "%MOCK_NRF_DETACH_FAIL%"=="1" exit /b 29
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
    $env:MOCK_NRF_DETACH_FAIL = "0"
    & powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass `
        -File (Join-Path $tools "upload_windows.ps1") `
        -HexPath $hex -Port "COM42" -Target "nrf54l" `
        -NrfOcd $mock -RetryDelayMs 0
    Assert-Equal $LASTEXITCODE 0 "native upload wrapper exit"
    $calls = @(Get-Content -LiteralPath $mockLog)
    $loadCalls = @($calls | Where-Object { $_ -match " load " })
    $detachCalls = @($calls | Where-Object { $_ -match "write 0xE000EDF0 00005FA0" })
    $aliasedCalls = @($calls | Where-Object { $_ -match "-t nrf54l15" })
    Assert-Equal $loadCalls.Count 2 "erase/load attempts"
    Assert-Equal $detachCalls.Count 1 "debug detach"
    Assert-Equal $aliasedCalls.Count 3 "target alias mapping"
    Write-Host "PASS Windows native upload retry and detach"

    Remove-Item -LiteralPath $mockLog -Force
    $env:MOCK_NRF_DETACH_FAIL = "1"
    & powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass `
        -File (Join-Path $tools "upload_windows.ps1") `
        -HexPath $hex -Port "COM42" -Target "nrf54lm20a" `
        -NrfOcd $mock -RetryDelayMs 0
    Assert-Equal $LASTEXITCODE 0 "successful flash with detach warning"
    Write-Host "PASS Windows detach warning preserves upload success"

    Remove-Item -LiteralPath $mockMark, $mockLog -Force
    $env:MOCK_NRF_ALWAYS_FAIL = "1"
    $env:MOCK_NRF_DETACH_FAIL = "0"
    & powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass `
        -File (Join-Path $tools "upload_windows.ps1") `
        -HexPath $hex -Port "COM42" -Target "nrf54lm20a" `
        -NrfOcd $mock -RetryDelayMs 0
    Assert-Equal $LASTEXITCODE 23 "native upload final failure propagation"
    $failedCalls = @(Get-Content -LiteralPath $mockLog)
    Assert-Equal $failedCalls.Count 2 "failed upload attempt count"
    Write-Host "PASS Windows native upload failure propagation"
}
finally {
    Remove-Item -LiteralPath $temp -Recurse -Force -ErrorAction SilentlyContinue
}
