# Konwertuje arm_test.gltf (z osadzonym base64) na arm_test.glb
# GLB: header 12B + chunk JSON (length, type 0x4E4F534A, data) + chunk BIN (length, type 0x004E4942, data)
$ErrorActionPreference = "Stop"
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Split-Path -Parent $scriptDir
$animDir = Join-Path $repoRoot "Data\SAF\Animations"
$gltfPath = Join-Path $animDir "arm_test.gltf"
$glbPath = Join-Path $animDir "arm_test.glb"

$json = Get-Content $gltfPath -Raw -Encoding UTF8
$obj = $json | ConvertFrom-Json
$uri = $obj.buffers[0].uri
if ($uri -notmatch "^data:application/octet-stream;base64,(.+)$") { throw "Expected base64 buffer URI" }
$binBytes = [System.Convert]::FromBase64String($matches[1])
$obj.buffers[0].PSObject.Properties.Remove("uri")
$obj.buffers[0] | Add-Member -NotePropertyName "byteLength" -NotePropertyValue $binBytes.Length -Force
$jsonForGlb = $obj | ConvertTo-Json -Depth 10 -Compress
$jsonBytes = [System.Text.Encoding]::UTF8.GetBytes($jsonForGlb)
$jsonPadding = (4 - ($jsonBytes.Length % 4)) % 4
$binPadding = (4 - ($binBytes.Length % 4)) % 4
$jsonChunkLen = $jsonBytes.Length + $jsonPadding
$binChunkLen = $binBytes.Length + $binPadding
$totalLen = 12 + 8 + $jsonChunkLen + 8 + $binChunkLen

$ms = New-Object System.IO.MemoryStream
$bw = New-Object System.IO.BinaryWriter $ms
# Header: magic "glTF" (0x46546C67), version 2, total length
$bw.Write([uint32]0x46546C67)
$bw.Write([uint32]2)
$bw.Write([uint32]$totalLen)
# JSON chunk
$bw.Write([uint32]$jsonChunkLen)
$bw.Write([uint32]0x4E4F534A)
$bw.Write($jsonBytes)
for ($i = 0; $i -lt $jsonPadding; $i++) { $bw.Write([byte]0x20) }
# BIN chunk
$bw.Write([uint32]$binChunkLen)
$bw.Write([uint32]0x004E4942)
$bw.Write($binBytes)
for ($i = 0; $i -lt $binPadding; $i++) { $bw.Write([byte]0) }
$glbBytes = $ms.ToArray()
$bw.Dispose(); $ms.Dispose()
[System.IO.File]::WriteAllBytes($glbPath, $glbBytes)
Write-Host "Zapisano: $glbPath"
