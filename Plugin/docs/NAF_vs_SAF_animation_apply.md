# Porównanie: NAF vs SAF – aplikacja animacji do kości

## Jak robi to NAF (Native Animation Framework)

1. **Mapowanie kości**  
   Dla każdego jointa szkieletu (`skeleton->data->joint_names()`) NAF szuka węzła w grze po **dokładnej nazwie**:  
   `a_rootNode->GetObjectByName(name)`.  
   Wskaźniki do `n->local` (NiTransform) są trzymane w `transforms[]` jako `Float4x4*` (reinterpret_cast).

2. **Źródło danych**  
   Generator zwraca **lokalne** macierze w formacie SoA (ozz).  
   `UnpackSoaTransforms(a_output, loadedData->lastOutput)` zamienia je na AOS `Float4x4` (nadal **lokalne**).

3. **Zapis do gry**  
   W `PushAnimationOutput`:  
   `*dest[i] = source[i]`  
   czyli **nadpisanie `node->local` wyjściem animacji** (lokalne macierze z generatora).  
   **Brak** mnożenia przez „game_base”, **brak** jawnej konwersji Y-up→Z-up w tej ścieżce.

4. **Rest pose**  
   W `UpdateRestPose` dla kości z `controlledByGame` NAF **czyta** aktualny stan z gry (`*transforms[i]`) i na tej podstawie buduje rest pose do blendów.  
   Czyli: animacja jest w jednym układzie (prawdopodobnie ten sam co szkielet OZZ), a NAF **nadpisuje** lokalne macierze węzłów wyjściem generatora.

5. **Brak osobnej konwersji osi w apply**  
   W ścieżce „generator → lastOutput → node” NAF nie stosuje osobnego kroku Y-up→Z-up; ewentualna konwersja byłaby w imporcie (retarget) lub w definicji szkieletu.

---

## Jak robi to SAF (Starfield Animation Framework)

1. **Mapowanie kości**  
   Podobnie: joint map (nazwy → węzły), z aliasami i opcją `SwapArmsInJointMap`.  
   Zapis jest w **lokalne** pole rotacji (offset w NiAVObject), nie przez `Float4x4*` na cały `local`.

2. **Źródło danych**  
   Dla każdego jointa:  
   `delta = inv(ozz_rest) * ozz_anim` (lokalna delta w przestrzeni GLTF/Y-up).  
   Potem konwersja Y-up → Z-up (Creation Engine) i mnożenie:  
   `result = game_base * delta` (albo `delta * game_base` zależnie od opcji).  
   **game_base** = rotacja „z gry” zrobiona raz przy budowaniu joint map.

3. **Zapis do gry**  
   Zapis **tylko rotacji** (3×3) do `node + offset`.  
   Tułów/łydki/ramię mają dodatkowe korekty (ArmsCorrectDirection, SpineLegsAxisFix, CalfAxisFix itd.).

4. **Różnica**  
   SAF **miesza** rotację gry z deltą animacji i stosuje konwersję osi oraz wiele poprawek.  
   NAF **nadpisuje** `local` wyjściem generatora bez game_base i bez tych poprawek.

---

## Wniosek

- W NAF **cała** informacja o układzie współrzędnych i „poprawności” kości jest w:  
  szkielet OZZ, importer GLTF (ew. retarget) i generator.  
  Apply do sceny to tylko: unpack local SoA → copy do `node->local`.

- W SAF apply jest dużo bardziej złożone (game_base, Y-up→Z-up, osobne fixy na ręce/tułów/nogi), co może dawać „kręcenie” lub „składanie” gdy któraś z tych korekt jest zła.

---

## Propozycja: tryb „jak NAF” w SAF

Żeby zbliżyć zachowanie do NAF:

- **ApplyAnimRotationOnly=1** (w .ini)  
  W SAF przy tej opcji zapisujemy **tylko** rotację z animacji (z opcjonalną konwersją Y-up→Z-up), **bez** mnożenia przez game_base i **bez** korekt Arms/Spine/Calf.  
  To najbliższe „nadpisz local wynikiem generatora” jak w NAF.

- Dla pełnej zgodności z NAF należałoby dodatkowo:  
  - pisać cały `NiTransform` (rotate + translate),  
  - ewentualnie zmienić sposób konwersji Y-up→Z-up (np. tylko przy imporcie, nie przy apply).

Na testy: włączyć **ApplyAnimRotationOnly=1**, resztę korekt wyłączyć (ArmsCorrectDirection=0, SpineLegsAxisFix=0, CalfAxisFix=0, ArmsAlongTorsoFix=0 itd.) i sprawdzić, czy tułów/głowa/nogi przestają się kręcić (kosztem możliwego błędnego ustawienia rąk, które w NAF mogą być poprawiane w innym miejscu).
