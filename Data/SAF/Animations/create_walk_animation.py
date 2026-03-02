#!/usr/bin/env python3
"""
Generuje walk.gltf - animację chodzenia dla CAŁEGO ciała (nogi, kręgosłup, ręce, głowa).
Dzięki temu w grze widać jednocześnie ruch nóg/tułowia (które zwykle są poprawne)
i rąk (gdzie szukamy właściwych osi). Porównanie pozwala znaleźć prawidłową rotację kończyn.

Użycie: python create_walk_animation.py
        (uruchom z folderu Data/SAF/Animations lub podaj ścieżkę)
"""
import struct
import json
import base64
import math
import os

# Jointy z HumanRace.json - nazwy MUSZĄ się zgadzać
WALK_JOINTS = [
    "C_Spine", "C_Spine2", "C_Chest",   # tułów
    "L_Thigh", "R_Thigh", "L_Calf", "R_Calf",  # nogi
    "L_Clavicle", "L_Biceps", "R_Clavicle", "R_Biceps",  # ręce
    "C_Head",  # głowa
]

NUM_JOINTS = len(WALK_JOINTS)
NUM_FRAMES = 5
# Czas w sekundach: 0, 0.25, 0.5, 0.75, 1.0 (pętla 1 s)
TIMES = [0.0, 0.25, 0.5, 0.75, 1.0]

def quat_identity():
    return [0.0, 0.0, 0.0, 1.0]

def quat_rot_x(angle_rad):
    """Obrót wokół osi X (w GLTF: xyzw)."""
    half = angle_rad / 2
    return [math.sin(half), 0.0, 0.0, math.cos(half)]

def quat_rot_y(angle_rad):
    """Obrót wokół osi Y (xyzw)."""
    half = angle_rad / 2
    return [0.0, math.sin(half), 0.0, math.cos(half)]

def build_walk_keyframes():
    """Klatki: 0=L_leg forward, 0.5=R_leg forward; ręce przeciwlegle do nóg."""
    angle_leg = 0.4   # radiany – wyraźny krok
    angle_arm = 0.35  # wymach ręki
    angle_spine = 0.08  # lekki przechył tułowia
    
    # Dla każdego jointa: lista 5 kwaternionów [x,y,z,w] na klatkę
    keyframes = {j: [quat_identity()] * NUM_FRAMES for j in WALK_JOINTS}
    
    for frame in range(NUM_FRAMES):
        t = frame / (NUM_FRAMES - 1)  # 0, 0.25, 0.5, 0.75, 1.0
        # Faza: przy 0 i 1 – L noga do przodu; przy 0.5 – R noga do przodu
        phase = math.sin(t * 2 * math.pi)
        
        # Tułów – lekki przechył do przodu/tyłu
        keyframes["C_Spine"][frame] = quat_rot_x(angle_spine * phase)
        keyframes["C_Spine2"][frame] = quat_rot_x(angle_spine * 0.5 * phase)
        keyframes["C_Chest"][frame] = quat_identity()
        
        # Nogi: L_Thigh forward gdy phase>0, R_Thigh forward gdy phase<0
        keyframes["L_Thigh"][frame] = quat_rot_x(angle_leg * (1 if phase > 0 else -0.3))
        keyframes["R_Thigh"][frame] = quat_rot_x(-angle_leg * (1 if phase < 0 else -0.3))
        keyframes["L_Calf"][frame] = quat_rot_x(0.15 if phase > 0 else -0.05)
        keyframes["R_Calf"][frame] = quat_rot_x(-0.15 if phase < 0 else 0.05)
        
        # Ręce: przeciwnie do nóg (L arm forward gdy R leg forward)
        keyframes["L_Clavicle"][frame] = quat_rot_x(-angle_arm * (1 if phase < 0 else -0.3))
        keyframes["R_Clavicle"][frame] = quat_rot_x(angle_arm * (1 if phase > 0 else -0.3))
        keyframes["L_Biceps"][frame] = quat_rot_x(-angle_arm * 0.8 * (1 if phase < 0 else -0.3))
        keyframes["R_Biceps"][frame] = quat_rot_x(angle_arm * 0.8 * (1 if phase > 0 else -0.3))
        
        # Głowa – prawie bez ruchu
        keyframes["C_Head"][frame] = quat_identity()
    
    return keyframes

def build_binary_buffer(keyframes):
    """Bufor: 5 floatów (czas) + dla każdego jointa 5× quat (5×4 floaty)."""
    buf = bytearray()
    # Czasy
    for t in TIMES:
        buf += struct.pack("<f", t)
    # Dla każdego jointa 5 kwaternionów (xyzw = 4 floaty)
    for j in WALK_JOINTS:
        for q in keyframes[j]:
            for v in q:
                buf += struct.pack("<f", v)
    return buf

def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    out_path = os.path.join(script_dir, "walk.gltf")
    
    keyframes = build_walk_keyframes()
    buf = build_binary_buffer(keyframes)
    b64 = base64.b64encode(buf).decode("ascii")
    
    # Layout: bufferView 0 = times (20 bytes), bufferView 1..N = po 80 bytes (5 quats) na joint
    time_len = 5 * 4  # 20
    quat_block_len = 5 * 4 * 4  # 80
    
    buffer_views = [{"buffer": 0, "byteOffset": 0, "byteLength": time_len}]
    offset = time_len
    for _ in WALK_JOINTS:
        buffer_views.append({"buffer": 0, "byteOffset": offset, "byteLength": quat_block_len})
        offset += quat_block_len
    
    accessors = [
        {"type": "SCALAR", "bufferView": 0, "count": 5, "componentType": 5126}
    ]
    for i in range(1, 1 + NUM_JOINTS):
        accessors.append({"type": "VEC4", "bufferView": i, "count": 5, "componentType": 5126})
    
    nodes = [{"name": j, "rotation": [0, 0, 0, 1]} for j in WALK_JOINTS]
    scene_nodes = list(range(NUM_JOINTS))
    
    channels = [{"target": {"node": i, "path": "rotation"}, "sampler": i} for i in range(NUM_JOINTS)]
    samplers = [{"input": 0, "interpolation": "LINEAR", "output": i + 1} for i in range(NUM_JOINTS)]
    
    gltf = {
        "asset": {"version": "2.0", "generator": "SAF create_walk_animation.py"},
        "scene": 0,
        "scenes": [{"nodes": scene_nodes}],
        "nodes": nodes,
        "bufferViews": buffer_views,
        "accessors": accessors,
        "buffers": [{"uri": "data:application/octet-stream;base64," + b64, "byteLength": len(buf)}],
        "animations": [{
            "name": "walk",
            "channels": channels,
            "samplers": samplers
        }]
    }
    
    with open(out_path, "w", encoding="utf-8") as f:
        json.dump(gltf, f, indent=2, ensure_ascii=False)
    
    print("Zapisano:", out_path)
    print("Jointy:", NUM_JOINTS, "| Klatki:", NUM_FRAMES, "| Czas:", TIMES[-1], "s")
    print("W grze: saf play walk")

if __name__ == "__main__":
    main()
