Scriptname SAF extends ScriptObject

Import SAFScript

Struct SequencePhase
    ; If set to -1, will loop infinitely. If set to 0, indicates a play-once/non-looping animation.
    Int numLoops = 0
    ; The amount of time to take to blend from the previous phase to this phase, in seconds. Does not affect phase timing, only visuals.
    Float transitionTime = 1.0
    ; The path to the animation file, starting from the Starfield/Data/SAF folder (analogicznie do NAF).
    String filePath
EndStruct

; NAF-compatible API: fTransitionSeconds controls blend time in NAF.
; W SAF na razie używamy domyślnego czasu przejścia, a prędkość animacji = 1.0 (normalna).
Function PlayAnimation(Actor akTarget, String asAnim, Float fTransitionSeconds = 1.0) Global
    SAFScript.PlayOnActor(akTarget, asAnim, 1.0)
EndFunction

Function PlayAnimationOnce(Actor akTarget, String asAnim, Float fTransitionSeconds = 1.0) Global
    SAFScript.PlayOnActor(akTarget, asAnim, 1.0)
EndFunction

Bool Function StopAnimation(Actor akTarget, Float fTransitionSeconds = 1.0) Global
    return SAFScript.StopAnimation(akTarget)
EndFunction

Function SyncAnimations(Actor[] akTargets) Global
    SAFScript.SyncGraphs(akTargets)
EndFunction

Function StopSyncing(Actor akTarget) Global
    SAFScript.StopSyncing(akTarget)
EndFunction

Function StartSequence(Actor akTarget, SequencePhase[] sPhases, Bool bLoop) Global
    If sPhases.Length == 0
        return
    EndIf
    String[] paths = new String[sPhases.Length]
    Int i = 0
    While i < sPhases.Length
        paths[i] = sPhases[i].filePath
        i += 1
    EndWhile
    SAFScript.StartSequence(akTarget, paths, bLoop)
EndFunction

Bool Function AdvanceSequence(Actor akTarget, Bool bSmooth) Global
    return SAFScript.AdvanceSequence(akTarget, bSmooth)
EndFunction

Bool Function SetSequencePhase(Actor akTarget, Int iPhase) Global
    return SAFScript.SetSequencePhase(akTarget, iPhase)
EndFunction

Int Function GetSequencePhase(Actor akTarget) Global
    return SAFScript.GetSequencePhase(akTarget)
EndFunction

Function SetPositionLocked(Actor akTarget, Bool bLocked) Global
    SAFScript.SetGraphControlsPosition(akTarget, bLocked)
EndFunction

; Zablokuj aktora (pozycja + blokada ruchu/AI w pluginie). Przed odtworzeniem wywołaj tę funkcję, po scenie UnlockActorAfterAnimationRestrained.
Function LockActorForAnimationRestrained(Actor akActor, Float fX, Float fY, Float fZ, Bool abIsPlayer = false) Global
    If akActor == None
        return
    EndIf
    SAFScript.LockActorForAnimation(akActor, fX, fY, fZ, abIsPlayer)
EndFunction

; Odblokuj aktora po scenie.
Function UnlockActorAfterAnimationRestrained(Actor akActor, Bool abIsPlayer = false) Global
    If akActor == None
        return
    EndIf
    SAFScript.UnlockActorAfterAnimation(akActor, abIsPlayer)
EndFunction

; Główna funkcja: odtwarzaj animację z blokadą pozycji (blokada w pluginie). Po zakończeniu sceny wywołaj UnlockActorAfterAnimationRestrained w RegisterForSequenceEnd.
Function PlayOnActorLocked(Actor akActor, String asAnim, Float fSpeed = 1.0, Int animIndex = 0) Global
    If akActor == None
        return
    EndIf
    Float x = akActor.GetPositionX()
    Float y = akActor.GetPositionY()
    Float z = akActor.GetPositionZ()
    Bool isPlayer = (akActor == Game.GetPlayer())
    LockActorForAnimationRestrained(akActor, x, y, z, isPlayer)
    SAFScript.PlayOnActor(akActor, asAnim, fSpeed, animIndex)
EndFunction

; Jak PlayOnActorLocked, dla gracza.
Function PlayOnPlayerLocked(String asAnim, Float fSpeed = 1.0, Int animIndex = 0) Global
    Actor player = Game.GetPlayer()
    Float x = player.GetPositionX()
    Float y = player.GetPositionY()
    Float z = player.GetPositionZ()
    LockActorForAnimationRestrained(player, x, y, z, true)
    SAFScript.PlayOnPlayer(asAnim, fSpeed, animIndex)
EndFunction

; ─────────────────────────────────────────────────────────────
; Scena dla dwóch aktorów z blokadą pozycji/ruchu/AI
; actor1 zostaje w swoim miejscu, actor2 jest przeniesiony do actor1
; i oba są „zakotwiczone” w pluginie na czas sceny.
; Po scenie wywołaj UnlockActorAfterAnimationRestrained na obu.
; ─────────────────────────────────────────────────────────────
Function PlaySceneLocked(Actor akActor1, Actor akActor2, String asAnim1, String asAnim2, Float fSpeed = 1.0) Global
    If akActor1 == None || akActor2 == None
        return
    EndIf

    ; Prosty wrapper: całą logikę pozycji/rotacji/ruchu obsługuje plugin.
    SAFScript.PlayScene(akActor1, akActor2, asAnim1, asAnim2, fSpeed)
EndFunction

; Wygodny wrapper dla player + NPC (player jako actor1).
Function PlaySceneLockedPlayerNPC(Actor akNPC, String asPlayerAnim, String asNPCAnim, Float fSpeed = 1.0) Global
    Actor player = Game.GetPlayer()
    PlaySceneLocked(player, akNPC, asPlayerAnim, asNPCAnim, fSpeed)
EndFunction

; ─────────────────────────────────────────────────────────────
; PlaySceneSeparate – ZALECANE dla moda Gerge Ebanex i podobnych
;
; Rozwiązuje DWA problemy naraz:
;   1. Kapsuła havok: SetPosition(true) odświeża ją w bieżącej pozycji
;   2. Pozycja actor2: PrepareActorsForScene przesuwa actor2 na pozycję actor1
;
; Użycie (zastąp 2x PlayOnActor):
;   SAF.PlaySceneSeparate(akActor1, akActor2, "anim1.glb", "anim2.glb")
; ─────────────────────────────────────────────────────────────
Function PlaySceneSeparateWrapper(Actor akActor1, Actor akActor2, String asAnim1, String asAnim2, Float fSpeed = 1.0) Global
    If akActor1 == None || akActor2 == None
        return
    EndIf
    SAFScript.PlaySceneSeparate(akActor1, akActor2, asAnim1, asAnim2, fSpeed)
EndFunction

; Wrapper dla player + NPC.
Function PlaySceneSeparatePlayerNPC(Actor akNPC, String asPlayerAnim, String asNPCAnim, Float fSpeed = 1.0) Global
    Actor player = Game.GetPlayer()
    SAFScript.PlaySceneSeparate(player, akNPC, asPlayerAnim, asNPCAnim, fSpeed)
EndFunction

; ─────────────────────────────────────────────────────────────
; PlaySceneWithApproach – aktor 2 podchodzi do aktora 1 przed sceną
;
; Schemat:
;   1. Aktor 2 dostaje polecenie podejścia do aktora 1 (MoveTo z offsetem)
;   2. Pętla czeka aż aktor 2 znajdzie się w zasięgu fStopDistance
;   3. Po dotarciu: PrepareActorsForScene → blokada AI → rotacja → animacja
;
; Parametry:
;   fStopDistance  – odległość (j.g.) przy której uznajemy że aktor dotarł (domyślnie 50)
;   fTimeout       – maks. czas oczekiwania w sekundach (domyślnie 10)
;   fApproachOffset – offset X przy teleporcie (żeby NPC nie stanął w dokładnie tym samym miejscu)
; ─────────────────────────────────────────────────────────────
Function PlaySceneWithApproach(Actor akActor1, Actor akActor2, String asAnim1, String asAnim2,         Float fSpeed = 1.0, Float fStopDistance = 50.0, Float fTimeout = 10.0, Float fApproachOffset = 40.0) Global

    If akActor1 == None || akActor2 == None
        Return
    EndIf

    ; Krok 1: Upewnij się że AI aktora 2 jest odblokowane przed ruchem.
    ; (kMovementBlocked mógłby być ustawiony z poprzedniej sceny)
    SAFScript.UnlockActorAfterAnimation(akActor2, False)

    ; Krok 2: Oblicz punkt docelowy – pozycja aktora 1 z małym offsetem bocznym
    ; żeby aktor 2 stanął obok, nie w tym samym miejscu.
    Float destX = akActor1.GetPositionX() + fApproachOffset
    Float destY = akActor1.GetPositionY()
    Float destZ = akActor1.GetPositionZ()

    ; Krok 3: Przesuń aktora 2 w stronę aktora 1.
    ; MoveTo teleportuje – używamy go jako "wstępne ustawienie" blisko celu,
    ; a potem czekamy na faktyczne dotarcie przez AI.
    ; Jeśli chcesz żeby NPC szedł (nie teleportował), zamień MoveTo na
    ; odpowiedni pakiet AI w swoim skrypcie questowym.
    akActor2.MoveTo(akActor1, fApproachOffset, 0.0, 0.0, True)

    ; Krok 4: Poczekaj aż aktor 2 znajdzie się w zasięgu (max fTimeout sekund).
    Float elapsed = 0.0
    Float step    = 0.1
    While elapsed < fTimeout
        If akActor2.GetDistance(akActor1) <= fStopDistance
            elapsed = fTimeout + 1.0  ; wyjdź z pętli
        Else
            Utility.Wait(step)
            elapsed += step
        EndIf
    EndWhile

    ; Krok 5: Reszta schematu bez zmian – blokada AI, rotacja, animacja.
    PlaySceneLocked(akActor1, akActor2, asAnim1, asAnim2, fSpeed)
EndFunction

; Wygodny wrapper PlaySceneWithApproach dla player + NPC.
Function PlaySceneWithApproachPlayerNPC(Actor akNPC, String asPlayerAnim, String asNPCAnim,         Float fSpeed = 1.0, Float fStopDistance = 50.0, Float fTimeout = 10.0) Global
    Actor player = Game.GetPlayer()
    PlaySceneWithApproach(player, akNPC, asPlayerAnim, asNPCAnim, fSpeed, fStopDistance, fTimeout, 40.0)
EndFunction

Function SetActorPosition(Actor akTarget, Float fX, Float fY, Float fZ) Global
    SAFScript.SetActorPosition(akTarget, fX, fY, fZ)
EndFunction

; W NAF 100.0 to normalna prędkość. W SAF 1.0 to normalna prędkość.
Bool Function SetAnimationSpeed(Actor akTarget, Float fSpeed) Global
    SAFScript.SetAnimationSpeed(akTarget, fSpeed / 100.0)
    return true
EndFunction

Float Function GetAnimationSpeed(Actor akTarget) Global
    return SAFScript.GetAnimationSpeed(akTarget) * 100.0
EndFunction

String Function GetCurrentAnimation(Actor akTarget) Global
    return SAFScript.GetCurrentAnimation(akTarget)
EndFunction

Bool Function SetBlendGraphVariable(Actor akTarget, String asName, Float fValue) Global
    return SAFScript.SetBlendGraphVariable(akTarget, asName, fValue)
EndFunction

Float Function GetBlendGraphVariable(Actor akTarget, String asName) Global
    return SAFScript.GetBlendGraphVariable(akTarget, asName)
EndFunction

Function RegisterForPhaseBegin(ScriptObject sScript, String sFunctionName) Global
    SAFScript.RegisterForPhaseBegin(sScript, sFunctionName)
EndFunction

Function RegisterForSequenceEnd(ScriptObject sScript, String sFunctionName) Global
    SAFScript.RegisterForSequenceEnd(sScript, sFunctionName)
EndFunction

Function UnregisterForPhaseBegin(ScriptObject sScript) Global
    SAFScript.UnregisterForPhaseBegin(sScript)
EndFunction

Function UnregisterForSequenceEnd(ScriptObject sScript) Global
    SAFScript.UnregisterForSequenceEnd(sScript)
EndFunction

; Wybiera aktora ze sceny najbliżej celownika (z tablicy scenicznych aktorów).
; Najpierw próbuje użyć SAFScript.GetCrosshairRef, a jeśli ref nie należy do sceny,
; bierze aktora o najmniejszym kącie względem kierunku patrzenia gracza.
Actor Function PickActorFromCrosshair(Actor[] sceneActors, Float maxAngle = 20.0, Float maxDist = 500.0) Global
    If sceneActors == None || sceneActors.Length == 0
        return None
    EndIf

    Actor player = Game.GetPlayer()

    ; 1) Próba z natywnym crosshairRef
    ObjectReference ref = SAFScript.GetCrosshairRef()
    Actor hitActor = ref as Actor
    If hitActor
        Int i = 0
        While i < sceneActors.Length
            If sceneActors[i] == hitActor
                return hitActor
            EndIf
            i += 1
        EndWhile
    EndIf

    ; 2) Fallback: wybierz aktora o najmniejszym kącie względem celownika
    Actor best = None
    Float bestScore = maxAngle

    Int j = 0
    While j < sceneActors.Length
        Actor a = sceneActors[j]
        If a
            Float dist = player.GetDistance(a)
            If dist <= maxDist
                Float ang = Math.Abs(player.GetHeadingAngle(a)) ; 0 = idealnie w celowniku
                If ang < bestScore
                    bestScore = ang
                    best = a
                EndIf
            EndIf
        EndIf
        j += 1
    EndWhile

    return best
EndFunction

; Dla dwóch aktorów: zwraca tego, który jest bliżej celownika (spośród pary scenicznej).
Actor Function PickPairActorFromCrosshair(Actor firstActor, Actor secondActor, Float maxAngle = 20.0, Float maxDist = 500.0) Global
    Actor[] arr = new Actor[2]
    arr[0] = firstActor
    arr[1] = secondActor
    return PickActorFromCrosshair(arr, maxAngle, maxDist)
EndFunction

; ─────────────────────────────────────────────────────────────────────────────
; PlayOnActorAtFurniture
;
; Rozwiązuje problem kapsuły havok zostającej w starym miejscu NPC.
;
; Schemat:
;   1. MoveTo → teleportuj aktora na pozycję mebla (silnik przesuwa kapsułę havok)
;   2. Utility.Wait(0.1) → jedna klatka żeby silnik zaktualizował kapsułę
;   3. LockActorForAnimation → zapamiętaj NOWĄ pozycję (na meblu) jako kotwicę
;   4. PlayOnActor → odtwórz animację
;
; Parametry:
;   akActor    – aktor do animowania (NPC lub gracz)
;   akFurniture – mebel / ObjectReference na którym ma grać animacja
;   asAnim     – ścieżka do animacji SAF
;   fSpeed     – prędkość animacji (domyślnie 1.0)
;   fOffsetX/Y/Z – opcjonalny offset względem centrum mebla (domyślnie 0)
;   animIndex  – indeks animacji (domyślnie 0)
; ─────────────────────────────────────────────────────────────────────────────
Function PlayOnActorAtFurniture(Actor akActor, ObjectReference akFurniture, String asAnim, \
        Float fSpeed = 1.0, Float fOffsetX = 0.0, Float fOffsetY = 0.0, Float fOffsetZ = 0.0, \
        Int animIndex = 0) Global
    If akActor == None || akFurniture == None
        return
    EndIf

    ; Krok 1: Teleportuj aktora na pozycję mebla.
    ; MoveTo przesuwa też kapsułę havok (silnik obsługuje to wewnętrznie).
    ; abMatchRotation=True – aktor staje twarzą w kierunku mebla.
    akActor.MoveTo(akFurniture, fOffsetX, fOffsetY, fOffsetZ, True)

    ; Krok 2: Poczekaj jedną klatkę – silnik musi przetworzyć nową pozycję
    ; i zaktualizować bhkCharacterController zanim zablokujemy pozycję.
    Utility.Wait(0.1)

    ; Krok 3: Pobierz aktualną pozycję (już na meblu) i zablokuj ją jako kotwicę SAF.
    Float x = akActor.GetPositionX()
    Float y = akActor.GetPositionY()
    Float z = akActor.GetPositionZ()
    Bool isPlayer = (akActor == Game.GetPlayer())
    SAFScript.LockActorForAnimation(akActor, x, y, z, isPlayer)

    ; Krok 4: Odtwórz animację.
    SAFScript.PlayOnActor(akActor, asAnim, fSpeed, animIndex)
EndFunction

; ─────────────────────────────────────────────────────────────────────────────
; PlaySceneAtFurniture
;
; Jak PlaySceneLocked ale oboje aktorów są teleportowani na mebel przed sceną,
; żeby kapsuły havok były w prawidłowym miejscu.
;
; Parametry:
;   akActor1/2   – aktorzy sceny
;   akFurniture  – mebel / ObjectReference
;   asAnim1/2    – animacje dla actor1 i actor2
;   fSpeed       – prędkość
;   fOffsetX2/Y2/Z2 – offset aktora2 względem mebla (żeby nie stali w tym samym miejscu)
; ─────────────────────────────────────────────────────────────────────────────
Function PlaySceneAtFurniture(Actor akActor1, Actor akActor2, ObjectReference akFurniture, \
        String asAnim1, String asAnim2, Float fSpeed = 1.0, \
        Float fOffsetX2 = 40.0, Float fOffsetY2 = 0.0, Float fOffsetZ2 = 0.0) Global
    If akActor1 == None || akActor2 == None || akFurniture == None
        return
    EndIf

    ; Krok 1: Teleportuj obu aktorów na mebel.
    ; Actor1 na centrum mebla, actor2 z offsetem bocznym.
    akActor1.MoveTo(akFurniture, 0.0, 0.0, 0.0, True)
    akActor2.MoveTo(akFurniture, fOffsetX2, fOffsetY2, fOffsetZ2, True)

    ; Krok 2: Poczekaj żeby silnik zaktualizował kapsuły havok obu aktorów.
    Utility.Wait(0.1)

    ; Krok 3: Przekaż do PlayScene – plugin sam obsłuży blokadę AI/ruchu
    ; i rotację aktorów (PrepareActorsForScene w GraphManager).
    SAFScript.PlayScene(akActor1, akActor2, asAnim1, asAnim2, fSpeed)
EndFunction

; Wygodny wrapper PlaySceneAtFurniture dla player + NPC.
Function PlaySceneAtFurniturePlayerNPC(Actor akNPC, ObjectReference akFurniture, \
        String asPlayerAnim, String asNPCAnim, Float fSpeed = 1.0) Global
    Actor player = Game.GetPlayer()
    PlaySceneAtFurniture(player, akNPC, akFurniture, asPlayerAnim, asNPCAnim, fSpeed, 40.0, 0.0, 0.0)
EndFunction
