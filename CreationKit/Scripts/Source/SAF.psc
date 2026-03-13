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
Actor Function PickActorFromCrosshair(Actor[] sceneActors, Float maxAngle = 20.0, Float maxDist = 3000.0) Global
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
Actor Function PickPairActorFromCrosshair(Actor firstActor, Actor secondActor, Float maxAngle = 20.0, Float maxDist = 3000.0) Global
    Actor[] arr = new Actor[2]
    arr[0] = firstActor
    arr[1] = secondActor
    return PickActorFromCrosshair(arr, maxAngle, maxDist)
EndFunction
