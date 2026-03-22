# Physics (SAF)

Moduł fizyki wzorowany na **NAF** (`NAF-Common/project/src/Physics`), dostosowany do **CommonLibSF** i **Starfield** (bez REL / Fallout 4).

## Zawartość

| Plik | Opis |
|------|------|
| **ModelSpaceSystem** | Stały krok czasowy (1/60 s), przyspieszenie roota w model space, współczynnik interpolacji. |
| **DynamicProperty** | Właściwość dynamiczna (current, previous, velocity) + integracja liniowa i kątowa. |
| **Spring** | Sprężyna (stiffness, damping), siły liniowe i momenty kątowe. |
| **Constraint** | Ograniczenia: pudełko liniowe, sfera liniowa, stożek kątowy (z opcjonalną sprężyną miękką). |
| **Body** | Ciało z pozycją i rotacją w model space, sprężyny, ograniczenia, aktualizacja krokowa. |

## Zależności

- **ozz-animation** (SimdFloat4, SimdQuaternion, Float4x4, ToAffine, Clamp, NLerp, itd.) – już używane w SAF.
- **Util::Ozz::ToNormalizedQuaternion** – w `Util/OzzUtil.h` (Float4x4 → SimdQuaternion).

## Użycie

Moduł jest kompilowany z pluginem. Można go użyć w węzłach proceduralnych (np. spring-bone, secondary motion) lub w przyszłym rozszerzeniu animacji. NAF używa tych klas w proceduralnych węzłach (np. `PSpringBoneNode`).

## Różnice względem NAF

- Brak kodu z `TARGET_GAME_F4` (Havok, character controller).
- Wszystkie typy ozz::math bez zmian; kompatybilne z tą samą wersją ozz co reszta SAF.
