param(
    [Parameter(Mandatory = $true)]
    [string]$InputJson
)

$ErrorActionPreference = 'Stop'

function Convert-HexBytes {
    param(
        [string]$Value,
        [int]$ExpectedBytes,
        [string]$FieldName
    )

    if ([string]::IsNullOrWhiteSpace($Value)) {
        throw "Missing '$FieldName' in the input JSON."
    }
    $hex = $Value -replace '[\s:-]', ''
    if ($hex.Length -ne $ExpectedBytes * 2 -or $hex -notmatch '^[0-9a-fA-F]+$') {
        throw "'$FieldName' must contain exactly $ExpectedBytes hexadecimal bytes."
    }
    $bytes = for ($i = 0; $i -lt $hex.Length; $i += 2) {
        '0x' + $hex.Substring($i, 2).ToUpperInvariant()
    }
    return $bytes -join ', '
}

$sourcePath = (Resolve-Path -LiteralPath $InputJson).Path
$targetPath = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\main\secrets.c'))
if (Test-Path -LiteralPath $targetPath) {
    throw "Refusing to overwrite existing local secrets.c: $targetPath"
}

$record = Get-Content -LiteralPath $sourcePath -Raw | ConvertFrom-Json
$mac = Convert-HexBytes -Value $record.bluetooth -ExpectedBytes 6 -FieldName 'bluetooth'
$deviceKey = Convert-HexBytes -Value $record.device -ExpectedBytes 16 -FieldName 'device'
$blob = Convert-HexBytes -Value $record.blob -ExpectedBytes 256 -FieldName 'blob'

$source = @(
    '#include "secrets.h"'
    ''
    "uint8_t MAC[6] = {$mac};"
    "uint8_t DEVICE_KEY[16] = {$deviceKey};"
    "uint8_t BLOB[256] = {$blob};"
    ''
) -join [Environment]::NewLine

[IO.File]::WriteAllText($targetPath, $source, [Text.UTF8Encoding]::new($false))
Write-Host "Created local secrets.c (values omitted): $targetPath"
