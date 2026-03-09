Scriptname SAFScript extends ScriptObject Native

Bool Function PlayOnActor(Actor akActor, String animId, Float fSpeed = 1.0, Int animIndex = 0) Native Global
Bool Function PlayOnPlayer(String animId, Float fSpeed = 1.0, Int animIndex = 0) Native Global
Bool Function StopAnimation(Actor akActor) Native Global

String Function GetCurrentAnimation(Actor akActor) Native Global
Function SetAnimationSpeed(Actor akActor, Float fSpeed) Native Global
Float Function GetAnimationSpeed(Actor akActor) Native Global
Function SetGraphControlsPosition(Actor akActor, Bool bLocked) Native Global
Function SetActorPosition(Actor akActor, Float fX, Float fY, Float fZ) Native Global

Int Function GetSequencePhase(Actor akActor) Native Global
Bool Function SetSequencePhase(Actor akActor, Int iPhase) Native Global
Bool Function AdvanceSequence(Actor akActor, Bool bSmooth) Native Global
Function StartSequence(Actor akActor, String[] asPaths, Bool bLoop) Native Global

Function SyncGraphs(Actor[] akTargets) Native Global
Function StopSyncing(Actor akTarget) Native Global

Bool Function SetBlendGraphVariable(Actor akActor, String asName, Float fValue) Native Global
Float Function GetBlendGraphVariable(Actor akActor, String asName) Native Global

; Event registration (NAF-style: pass script that will receive the event)
Function RegisterForPhaseBegin(ScriptObject akScript, String asFunctionName) Native Global
Function RegisterForSequenceEnd(ScriptObject akScript, String asFunctionName) Native Global
Function UnregisterForPhaseBegin(ScriptObject akScript) Native Global
Function UnregisterForSequenceEnd(ScriptObject akScript) Native Global
