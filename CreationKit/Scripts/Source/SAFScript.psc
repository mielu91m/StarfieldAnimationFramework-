; Wzorzec ze Starfield ContentResources: native global (małe litery), jak Game.psc / InputEnableLayer.psc
; Nie sugerować się NAF.psc – po aktualizacji Starfielda/Papyrusa API się zmieniło.
Scriptname SAFScript extends ScriptObject Native

; Minimal test: cgf "SAFScript.Ping"
Bool Function Ping() native global

Bool Function PlayOnActor(Actor akActor, String animId, Float fSpeed = 1.0, Int animIndex = 0) native global
Bool Function PlayAnimationOnce(Actor akActor, String asAnimId, Float fTransitionTime = 1.0) native global
; Dwie osoby: aktor 2 dostaje pozycję aktora 1, potem obie animacje startują (jak saf playscene).
Bool Function PlayScene(Actor akActor1, Actor akActor2, String asAnim1, String asAnim2, Float fSpeed = 1.0) native global
; PlaySceneSeparate – jak PlayScene ale z pełną synchronizacją:
; 1) kapsuła havok obu aktorów odświeżona w bieżącej pozycji
; 2) actor2 przesunięty na pozycję actor1 (PrepareActorsForScene)
; 3) pozycja zakotwiczona w SAF dla obu
; 4) każdy aktor dostaje swoją animację + SyncGraphs
; Używaj zamiast 2x PlayOnActor gdy chcesz scenę z dwoma aktorami.
Bool Function PlaySceneSeparate(Actor akActor1, Actor akActor2, String asAnim1, String asAnim2, Float fSpeed = 1.0) native global
Bool Function PlayOnPlayer(String animId, Float fSpeed = 1.0, Int animIndex = 0) native global
Bool Function PlayOnActors(Actor[] akActors, String animId, Int animIndex = 0) native global
Bool Function StopAnimation(Actor akActor) native global

String Function GetCurrentAnimation(Actor akActor) native global
Function SetAnimationSpeed(Actor akActor, Float fSpeed) native global
Float Function GetAnimationSpeed(Actor akActor) native global
Function SetGraphControlsPosition(Actor akActor, Bool bLocked) native global
Function SetActorPosition(Actor akActor, Float fX, Float fY, Float fZ) native global
Function MatchActorTransform(Actor akTarget, Actor akSource) native global
; Zakotwicz aktora w (fX,fY,fZ) – przy następnym Play/StartSequence ta pozycja będzie trzymana. Odblokuj UnlockActorAfterAnimation po scenie.
; Przy meblach używaj SAF.PlayOnActorLocked / PlayOnPlayerLocked (blokada + odtwarzanie), w RegisterForSequenceEnd wywołaj SAF.UnlockActorAfterAnimationRestrained.
Function LockActorForAnimation(Actor akActor, Float fX, Float fY, Float fZ, Bool abIsPlayer = false) native global
Function UnlockActorAfterAnimation(Actor akActor, Bool abIsPlayer = false) native global

; Crosshair helper – zwraca referencję pod celownikiem (walidowana).
ObjectReference Function GetCrosshairRef() native global

; Zwraca aktora pod celownikiem (crosshairRef + fallback stożkowy). Preferuj tę funkcję.
Actor Function GetCrosshairActor() native global

; Szuka aktora w stożku widzenia przez ProcessLists (niezależnie od crosshairRef).
; maxAngleDeg: połowa kąta stożka (0 = domyślnie 15°), maxDist: zasięg (0 = domyślnie 2000).
Actor Function FindActorNearCrosshair(Float maxAngleDeg = 15.0, Float maxDist = 2000.0) native global

; Dodaje aktora wskazanego celownikiem do bufora selekcji (max 8).
; Zwraca indeks aktora w buforze (0-based) lub -1 jeśli brak aktora lub bufor pełny.
Int Function AddActorToSelectionBuffer(Float maxAngleDeg = 15.0, Float maxDist = 2000.0) native global

; Zwraca aktualny bufor selekcji jako tablicę.
Actor[] Function GetSelectionBuffer() native global

; Zwraca rozmiar bufora selekcji.
Int Function GetSelectionBufferSize() native global

; Czyści bufor selekcji.
Function ClearSelectionBuffer() native global

; Bezpośrednio dodaje konkretnego aktora do bufora (bez celowania).
; Użyj gdy masz już aktora z własnej logiki (np. z trafienia, z GetLinkedRef).
; Zwraca indeks (0-based) lub -1 jeśli bufor pełny / aktor None.
Int Function SelectActor(Actor akActor) native global

Int Function GetSequencePhase(Actor akActor) native global
Bool Function SetSequencePhase(Actor akActor, Int iPhase) native global
Bool Function AdvanceSequence(Actor akActor, Bool bSmooth) native global
Function StartSequence(Actor akActor, String[] asPaths, Bool bLoop) native global

Function SyncGraphs(Actor[] akTargets) native global
Function StopSyncing(Actor akTarget) native global

Bool Function SetBlendGraphVariable(Actor akActor, String asName, Float fValue) native global
Float Function GetBlendGraphVariable(Actor akActor, String asName) native global

; Event registration (script object + function name)
Function RegisterForPhaseBegin(ScriptObject akScript, String asFunctionName) native global
Function RegisterForSequenceEnd(ScriptObject akScript, String asFunctionName) native global
Function UnregisterForPhaseBegin(ScriptObject akScript) native global
Function UnregisterForSequenceEnd(ScriptObject akScript) native global
