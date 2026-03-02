# Generuje walk.gltf - animacja chodzenia (caly model). Uzycie: .\create_walk_animation.ps1
$ErrorActionPreference = "Stop"
$joints = @("C_Spine","C_Spine2","C_Chest","L_Thigh","R_Thigh","L_Calf","R_Calf","L_Clavicle","L_Biceps","R_Clavicle","R_Biceps","C_Head")
$times = @(0.0, 0.25, 0.5, 0.75, 1.0)
$numFrames = 5

function Add-Float { param([System.Collections.Generic.List[byte]]$list, [float]$v)
    $list.AddRange([BitConverter]::GetBytes($v))
}
function Add-Quat { param($list, [float]$x,[float]$y,[float]$z,[float]$w)
    Add-Float $list $x; Add-Float $list $y; Add-Float $list $z; Add-Float $list $w
}
function QuatRotX { param([double]$angle)
    $h = $angle/2.0; return @([Math]::Sin($h), 0.0, 0.0, [Math]::Cos($h))
}
function QuatId { return @(0.0, 0.0, 0.0, 1.0) }

$buf = [System.Collections.Generic.List[byte]]::new()
foreach ($t in $times) { Add-Float $buf $t }

# Bufor: dla kazdego jointa 5 kolejnych kwaternionow (klatki 0..4)
$angleLeg = 0.4; $angleArm = 0.35; $angleSpine = 0.08
$jointData = @{}
foreach ($j in $joints) { $jointData[$j] = @(0..4 | ForEach-Object { QuatId }) }

foreach ($frame in 0..4) {
    $t = $frame / 4.0
    $phase = [Math]::Sin($t * 2 * [Math]::PI)
    $phasePos = if ($phase -gt 0.3) { 1 } elseif ($phase -lt -0.3) { -1 } else { 0 }
    $sx = $angleSpine * $phase
    $jointData["C_Spine"][$frame] = QuatRotX $sx
    $jointData["C_Spine2"][$frame] = QuatRotX ($sx * 0.5)
    $lThigh = if ($phasePos -eq 1) { $angleLeg } else { -0.12 }; $rThigh = if ($phasePos -eq -1) { -$angleLeg } else { 0.12 }
    $jointData["L_Thigh"][$frame] = QuatRotX $lThigh
    $jointData["R_Thigh"][$frame] = QuatRotX $rThigh
    $lCalf = if ($phasePos -eq 1) { 0.15 } else { -0.05 }; $rCalf = if ($phasePos -eq -1) { -0.15 } else { 0.05 }
    $jointData["L_Calf"][$frame] = QuatRotX $lCalf
    $jointData["R_Calf"][$frame] = QuatRotX $rCalf
    $lArm = if ($phasePos -eq -1) { -$angleArm } else { 0.1 }; $rArm = if ($phasePos -eq 1) { $angleArm } else { -0.1 }
    $jointData["L_Clavicle"][$frame] = QuatRotX $lArm
    $jointData["L_Biceps"][$frame] = QuatRotX ($lArm * 0.8)
    $jointData["R_Clavicle"][$frame] = QuatRotX $rArm
    $jointData["R_Biceps"][$frame] = QuatRotX ($rArm * 0.8)
}

foreach ($j in $joints) {
    foreach ($q in $jointData[$j]) { Add-Quat $buf $q[0] $q[1] $q[2] $q[3] }
}
# C_Chest, C_Head zostaly jako identity w jointData

$b64 = [Convert]::ToBase64String($buf)
$byteOffset = 20
$bufferViews = @(@{ buffer = 0; byteOffset = 0; byteLength = 20 })
for ($i = 0; $i -lt $joints.Length; $i++) {
    $bufferViews += @{ buffer = 0; byteOffset = $byteOffset; byteLength = 80 }
    $byteOffset += 80
}
$accessors = @(@{ type = "SCALAR"; bufferView = 0; count = 5; componentType = 5126 })
for ($i = 1; $i -le $joints.Length; $i++) {
    $accessors += @{ type = "VEC4"; bufferView = $i; count = 5; componentType = 5126 }
}
$nodes = $joints | ForEach-Object { @{ name = $_; rotation = @(0,0,0,1) } }
$channels = 0..($joints.Length-1) | ForEach-Object { @{ target = @{ node = $_; path = "rotation" }; sampler = $_ } }
$samplers = 0..($joints.Length-1) | ForEach-Object { @{ input = 0; interpolation = "LINEAR"; output = $_ + 1 } }

$gltf = @{
    asset = @{ version = "2.0"; generator = "SAF create_walk_animation.ps1" }
    scene = 0
    scenes = @(@{ nodes = 0..($joints.Length-1) })
    nodes = $nodes
    bufferViews = $bufferViews
    accessors = $accessors
    buffers = @(@{ uri = "data:application/octet-stream;base64,$b64"; byteLength = $buf.Count })
    animations = @(@{ name = "walk"; channels = $channels; samplers = $samplers })
}

$json = $gltf | ConvertTo-Json -Depth 10
$outPath = Join-Path $PSScriptRoot "walk.gltf"
[IO.File]::WriteAllText($outPath, $json, [Text.Encoding]::UTF8)
Write-Host "Zapisano: $outPath"
Write-Host "W grze: saf play walk"
