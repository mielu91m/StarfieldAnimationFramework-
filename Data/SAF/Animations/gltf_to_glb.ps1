# Konwertuje walk.gltf na walk.glb (binarny format). Uzycie: .\gltf_to_glb.ps1
$ErrorActionPreference = "Stop"
$scriptDir = $PSScriptRoot
$gltfPath = Join-Path $scriptDir "walk.gltf"
$glbPath = Join-Path $scriptDir "walk.glb"

$json = Get-Content $gltfPath -Raw -Encoding UTF8
$gltf = $json | ConvertFrom-Json

# Wyciagnij i zdekoduj bufor z data:application/octet-stream;base64,...
$uri = $gltf.buffers[0].uri
if ($uri -notmatch "^data:application/octet-stream;base64,(.+)$") {
    Write-Error "Oczekiwano osadzonego bufora base64 w buffers[0].uri"
}
$b64 = $Matches[1]
$binData = [Convert]::FromBase64String($b64)
$binLength = $binData.Length

# W GLB pierwszy bufor to chunk BIN - w JSON tylko byteLength, bez uri
$gltf.buffers[0] = @{ byteLength = $binLength } | ConvertTo-Json -Compress | ConvertFrom-Json
# Usun uri z obiektu (ConvertTo-Json moze go pominac jesli nie ma)
$bufferObj = @{}
$bufferObj["byteLength"] = $binLength
$gltf.buffers = @($bufferObj)

$jsonBytes = [System.Text.Encoding]::UTF8.GetBytes(($gltf | ConvertTo-Json -Depth 20 -Compress))
# Padding JSON do wielokrotnosci 4 (spacja 0x20)
$jsonPad = (4 - ($jsonBytes.Length % 4)) % 4
$jsonChunkData = [byte[]]::new($jsonBytes.Length + $jsonPad)
[Array]::Copy($jsonBytes, $jsonChunkData, $jsonBytes.Length)
for ($i = 0; $i -lt $jsonPad; $i++) { $jsonChunkData[$jsonBytes.Length + $i] = 0x20 }

# Padding BIN do wielokrotnosci 4 (zero)
$binPad = (4 - ($binLength % 4)) % 4
$binChunkData = [byte[]]::new($binLength + $binPad)
[Array]::Copy($binData, $binChunkData, $binLength)

$jsonChunkLen = $jsonChunkData.Length
$binChunkLen = $binChunkData.Length
# Header: magic(4) + version(4) + totalLength(4)
# Chunk0: length(4) + type(4) + data
# Chunk1: length(4) + type(4) + data
$totalLen = 12 + 8 + $jsonChunkLen + 8 + $binChunkLen

$out = [System.IO.MemoryStream]::new()
$bw = [System.IO.BinaryWriter]::new($out)
$bw.Write([uint32]0x46546C67)   # "glTF" magic
$bw.Write([uint32]2)             # version
$bw.Write([uint32]$totalLen)     # total length
$bw.Write([uint32]$jsonChunkLen) # JSON chunk length
$bw.Write([uint32]0x4E4F534A)   # JSON chunk type
$bw.Write($jsonChunkData)
$bw.Write([uint32]$binChunkLen)  # BIN chunk length
$bw.Write([uint32]0x004E4942)    # BIN chunk type
$bw.Write($binChunkData)
$bw.Flush()

[IO.File]::WriteAllBytes($glbPath, $out.ToArray())
Write-Host "Zapisano: $glbPath"
Write-Host "W grze: saf play walk  (lub sciezka do walk.glb)"
