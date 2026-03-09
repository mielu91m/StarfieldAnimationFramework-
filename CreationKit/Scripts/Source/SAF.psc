Scriptname SAF extends ScriptObject

Struct SequencePhase
    Int numLoops = 0
    Float transitionTime = 1.0
    String filePath
EndStruct

Function PlayAnimation(Actor akTarget, String asAnim, Float fSpeed = 1.0) Global
    SAFScript.PlayOnActor(akTarget, asAnim, fSpeed)
EndFunction

Function PlayAnimationOnce(Actor akTarget, String asAnim, Float fSpeed = 1.0) Global
    SAFScript.PlayOnActor(akTarget, asAnim, fSpeed)
EndFunction

Function PlayOnTargetOrPlayer(String asAnim, Float fSpeed = 1.0) Global
    Actor target = Game.GetCrosshairRef()
    If target == None
        target = Game.GetPlayer()
    EndIf
    If target != None
        SAFScript.PlayOnActor(target, asAnim, fSpeed)
    EndIf
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

Bool Function SetAnimationSpeed(Actor akTarget, Float fSpeed) Global
    SAFScript.SetAnimationSpeed(akTarget, fSpeed)
    return true
EndFunction

Float Function GetAnimationSpeed(Actor akTarget) Global
    return SAFScript.GetAnimationSpeed(akTarget)
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
