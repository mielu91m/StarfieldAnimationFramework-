# Generuje arm_test.gltf z osadzonym buforem (ta sama animacja co create_arm_test_animation.py).
# Uruchom: .\Scripts\Create-ArmTestAnimation.ps1
$ErrorActionPreference = "Stop"
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Split-Path -Parent $scriptDir
$outDir = Join-Path $repoRoot "Data\SAF\Animations"
if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Path $outDir -Force | Out-Null }

function Write-Float($writer, [float]$v) {
    $writer.Write([System.BitConverter]::GetBytes($v))
}
$times = [float[]](0.0, 1.0, 2.0, 3.0, 4.0)
$qId = [float[]](0.0, 0.0, 0.0, 1.0)
$qArmUp = [float[]](0.0, 0.0, -0.7071068, 0.7071068)

$ms = New-Object System.IO.MemoryStream
$bw = New-Object System.IO.BinaryWriter $ms
foreach ($t in $times) { Write-Float $bw $t }
$joints = @("R_Clavicle","R_Biceps","R_Forearm","R_Arm","L_Clavicle","L_Biceps","L_Forearm","L_Arm")
foreach ($i in 0..7) {
    $isRight = $joints[$i].StartsWith("R_")
    for ($k = 0; $k -lt 5; $k++) {
        if ($k -eq 1 -and $isRight) { foreach ($f in $qArmUp) { Write-Float $bw $f } }
        elseif ($k -eq 3 -and -not $isRight) { foreach ($f in $qArmUp) { Write-Float $bw $f } }
        else { foreach ($f in $qId) { Write-Float $bw $f } }
    }
}
$binary = $ms.ToArray()
$bw.Dispose(); $ms.Dispose()
$base64 = [System.Convert]::ToBase64String($binary)

$timeSize = 20
$quatSize = 80
$quatOffsets = @(20, 100, 180, 260, 340, 420, 500, 580)
$bufferViews = @(
    @{ buffer = 0; byteOffset = 0; byteLength = $timeSize }
)
foreach ($off in $quatOffsets) {
    $bufferViews += @{ buffer = 0; byteOffset = $off; byteLength = $quatSize }
}
$accessors = @(
    @{ bufferView = 0; componentType = 5126; count = 5; type = "SCALAR" }
)
for ($i = 0; $i -lt 8; $i++) {
    $accessors += @{ bufferView = 1 + $i; componentType = 5126; count = 5; type = "VEC4" }
}
$nodes = $joints | ForEach-Object { @{ name = $_; rotation = $qId } }
$channels = @(); $samplers = @()
for ($i = 0; $i -lt 8; $i++) {
    $samplers += @{ input = 0; output = 1 + $i; interpolation = "LINEAR" }
    $channels += @{ sampler = $i; target = @{ node = $i; path = "rotation" } }
}
$doc = @{
    asset = @{ version = "2.0"; generator = "SAF Create-ArmTestAnimation.ps1" }
    scene = 0
    scenes = @(@{ nodes = 0..7 })
    nodes = $nodes
    buffers = @(@{ byteLength = $binary.Length; uri = "data:application/octet-stream;base64,$base64" })
    bufferViews = $bufferViews
    accessors = $accessors
    animations = @(@{ name = "arm_test"; channels = $channels; samplers = $samplers })
} | ConvertTo-Json -Depth 10 -Compress:$false

$gltfPath = Join-Path $outDir "arm_test.gltf"
[System.IO.File]::WriteAllText($gltfPath, $doc, [System.Text.Encoding]::UTF8)
Write-Host "Zapisano: $gltfPath"
Write-Host "W grze: saf play arm_test"
Write-Host "Animacja: 0-1 s prawa reka w gore, 1-2 w dol, 2-3 lewa w gore, 3-4 lewa w dol."
