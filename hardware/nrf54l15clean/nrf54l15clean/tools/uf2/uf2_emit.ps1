param(
    [Parameter(Mandatory = $true)] [string] $InputPath,
    [Parameter(Mandatory = $true)] [string] $OutputPath,
    [Parameter(Mandatory = $true)] [string] $Family,
    [string] $BaseAddress = "0"
)

$ErrorActionPreference = "Stop"

function Convert-ToUInt32([string] $Value) {
    $text = $Value.Trim()
    if ($text.StartsWith("0x", [System.StringComparison]::OrdinalIgnoreCase)) {
        return [Convert]::ToUInt32($text.Substring(2), 16)
    }
    return [Convert]::ToUInt32($text, 10)
}

function Set-UInt32LE([byte[]] $Buffer, [int] $Offset, [uint32] $Value) {
    $bytes = [BitConverter]::GetBytes($Value)
    if (-not [BitConverter]::IsLittleEndian) {
        [Array]::Reverse($bytes)
    }
    [Array]::Copy($bytes, 0, $Buffer, $Offset, 4)
}

if (-not (Test-Path -LiteralPath $InputPath -PathType Leaf)) {
    throw "Input binary not found: $InputPath"
}

[byte[]] $image = [IO.File]::ReadAllBytes($InputPath)
if ($image.Length -eq 0) {
    throw "Input binary is empty: $InputPath"
}

[uint32] $familyId = Convert-ToUInt32 $Family
[uint32] $base = Convert-ToUInt32 $BaseAddress
[int] $chunkSize = 256
[uint32] $blockCount = [uint32] [Math]::Ceiling($image.Length / [double] $chunkSize)
$stream = New-Object IO.MemoryStream

try {
    for ([uint32] $blockNumber = 0; $blockNumber -lt $blockCount; ++$blockNumber) {
        [int] $sourceOffset = [int] $blockNumber * $chunkSize
        [int] $sourceLength = [Math]::Min($chunkSize, $image.Length - $sourceOffset)
        [byte[]] $block = New-Object byte[] 512

        Set-UInt32LE $block 0 0x0A324655
        Set-UInt32LE $block 4 0x9E5D5157
        Set-UInt32LE $block 8 0x00002000
        Set-UInt32LE $block 12 ([uint32] ($base + $sourceOffset))
        Set-UInt32LE $block 16 ([uint32] $chunkSize)
        Set-UInt32LE $block 20 $blockNumber
        Set-UInt32LE $block 24 $blockCount
        Set-UInt32LE $block 28 $familyId
        [Array]::Copy($image, $sourceOffset, $block, 32, $sourceLength)
        Set-UInt32LE $block 508 0x0AB16F30
        $stream.Write($block, 0, $block.Length)
    }

    [IO.File]::WriteAllBytes($OutputPath, $stream.ToArray())
}
finally {
    $stream.Dispose()
}
