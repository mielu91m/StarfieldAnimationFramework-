#!/usr/bin/env python3
"""
Tworzy animację diagnostyczną arm_test.glb (lub .gltf + .bin) do testów SAF.
Animacja: na zmianę prawa ręka w górę -> w dół -> lewa ręka w górę -> w dół (4 s).
Dzięki temu w grze widać dokładnie, jak transformacje wpływają na poszczególne kości.

Użycie:
  python create_arm_test_animation.py
  -> tworzy Data/SAF/Animations/arm_test.gltf i arm_test.bin
  W grze: saf play arm_test

Wymaga: pip install pygltflib  (opcjonalnie, do .glb; bez tego tylko .gltf+.bin)
"""

import json
import struct
import base64
import os
from pathlib import Path

# Kości do animowania (nazwy jak w HumanRace.json)
ARM_JOINTS = [
    "R_Clavicle", "R_Biceps", "R_Forearm", "R_Arm",
    "L_Clavicle", "L_Biceps", "L_Forearm", "L_Arm",
]

# Czas keyframe'ów (s): 0=spokój, 1=prawa w górę, 2=prawa w dół, 3=lewa w górę, 4=lewa w dół
TIMES = [0.0, 1.0, 2.0, 3.0, 4.0]
# Quaternion identity (glTF: x,y,z,w)
Q_ID = [0.0, 0.0, 0.0, 1.0]
# "Ręka w górę" – obrót -90° wokół Z (w GLTF Y=up, Z w tył; dla prawej ręki w górę)
# q = [0, 0, sin(-45°), cos(-45°)] = [0, 0, -0.7071068, 0.7071068]
Q_ARM_UP = [0.0, 0.0, -0.7071068, 0.7071068]

def make_quat_keyframes(is_right_arm: bool):
    """5 keyframe'ów: identity, (R/L) up, identity, (L/R) up, identity."""
    q_up = Q_ARM_UP
    frames = []
    for i in range(5):
        if i == 1 and is_right_arm:
            frames.append(q_up)
        elif i == 3 and not is_right_arm:
            frames.append(q_up)
        else:
            frames.append(Q_ID)
    return frames

def build_binary_buffer():
    """Bufor: 5 floatów (time) + dla każdego z 8 jointów 5×4 floatów (quat)."""
    buf = bytearray()
    # Czasy (wspólne dla wszystkich samplers)
    for t in TIMES:
        buf.extend(struct.pack("<f", t))
    # Dla każdego jointu 5 quatów
    for i, name in enumerate(ARM_JOINTS):
        is_right = name.startswith("R_")
        frames = make_quat_keyframes(is_right)
        for q in frames:
            for v in q:
                buf.extend(struct.pack("<f", v))
    return bytes(buf)

def build_gltf(out_dir: Path, use_embedded_buffer: bool = True):
    out_dir.mkdir(parents=True, exist_ok=True)
    binary = build_binary_buffer()

    # Długości w buforze
    time_size = 5 * 4  # 5 floatów
    quat_size = 5 * 4 * 4  # 5 quatów × 4 floaty
    time_offset = 0
    quat_offsets = [time_size + i * quat_size for i in range(len(ARM_JOINTS))]

    if use_embedded_buffer:
        uri = "data:application/octet-stream;base64," + base64.b64encode(binary).decode("ascii")
        buffer_obj = {"byteLength": len(binary), "uri": uri}
    else:
        bin_path = out_dir / "arm_test.bin"
        bin_path.write_bytes(binary)
        buffer_obj = {"byteLength": len(binary), "uri": "arm_test.bin"}

    # Node indices 0..7
    nodes = [{"name": name, "rotation": Q_ID} for name in ARM_JOINTS]

    # BufferView: 1 dla czasu, 8 dla quatów
    buffer_views = [
        {"buffer": 0, "byteOffset": 0, "byteLength": time_size},
    ]
    for off in quat_offsets:
        buffer_views.append({"buffer": 0, "byteOffset": off, "byteLength": quat_size})

    # Accessors
    accessors = [
        {"bufferView": 0, "componentType": 5126, "count": 5, "type": "SCALAR"},  # time
    ]
    for i in range(8):
        accessors.append({"bufferView": 1 + i, "componentType": 5126, "count": 5, "type": "VEC4"})

    # Animation: 8 channeli, 8 samplerów
    channels = []
    samplers = []
    for i in range(8):
        samplers.append({"input": 0, "output": 1 + i, "interpolation": "LINEAR"})
        channels.append({"sampler": i, "target": {"node": i, "path": "rotation"}})

    anim = {"name": "arm_test", "channels": channels, "samplers": samplers}
    doc = {
        "asset": {"version": "2.0", "generator": "SAF create_arm_test_animation.py"},
        "scene": 0,
        "scenes": [{"nodes": list(range(len(ARM_JOINTS)))}],
        "nodes": nodes,
        "buffers": [buffer_obj],
        "bufferViews": buffer_views,
        "accessors": accessors,
        "animations": [anim],
    }

    gltf_path = out_dir / "arm_test.gltf"
    gltf_path.write_text(json.dumps(doc, indent=2), encoding="utf-8")
    print("Zapisano:", gltf_path)
    if not use_embedded_buffer:
        print("Zapisano:", out_dir / "arm_test.bin")
    return gltf_path

def try_export_glb(gltf_path: Path):
    """Eksport do .glb jeśli jest pygltflib."""
    try:
        import pygltflib
        gltf = pygltflib.GLTF2().load(gltf_path)
        glb_path = gltf_path.with_suffix(".glb")
        gltf.save(glb_path)
        print("Zapisano GLB:", glb_path)
    except Exception as e:
        print("GLB (opcjonalnie): pip install pygltflib –", e)

def main():
    script_dir = Path(__file__).resolve().parent
    # Data/SAF/Animations względem repozytorium (Scripts jest obok Data)
    repo_root = script_dir.parent
    out_dir = repo_root / "Data" / "SAF" / "Animations"
    gltf_path = build_gltf(out_dir, use_embedded_buffer=True)
    try_export_glb(gltf_path)
    print("\nW grze: saf play arm_test")
    print("Animacja: 0–1 s prawa ręka w górę, 1–2 w dół, 2–3 lewa w górę, 3–4 lewa w dół.")

if __name__ == "__main__":
    main()
