; Wzorzec ze Starfield ContentResources: native global (małe litery), jak Game.psc / InputEnableLayer.psc
; Nie sugerować się NAF.psc – po aktualizacji Starfielda/Papyrusa API się zmieniło.
Scriptname SAFScript extends ScriptObject Native

; Minimal test: cgf "SAFScript.Ping"
Bool Function Ping() native global

Bool Function PlayOnActor(Actor akActor, String animId, Float fSpeed = 1.0, Int animIndex = 0) native global
Bool Function PlayOnPlayer(String animId, Float fSpeed = 1.0, Int animIndex = 0) native global
Bool Function PlayOnActors(Actor[] akActors, String animId, Int animIndex = 0) native global
Bool Function StopAnimation(Actor akActor) native global

String Function GetCurrentAnimation(Actor akActor) native global
Function SetAnimationSpeed(Actor akActor, Float fSpeed) native global
Float Function GetAnimationSpeed(Actor akActor) native global
Function SetGraphControlsPosition(Actor akActor, Bool bLocked) native global
Function SetActorPosition(Actor akActor, Float fX, Float fY, Float fZ) native global
; Zakotwicz aktora w (fX,fY,fZ) – przy następnym Play/StartSequence ta pozycja będzie trzymana. Odblokuj UnlockActorAfterAnimation po scenie.
Function LockActorForAnimation(Actor akActor, Float fX, Float fY, Float fZ, Bool abIsPlayer = false) native global
Function UnlockActorAfterAnimation(Actor akActor, Bool abIsPlayer = false) native global

; Crosshair helper – returns the reference under the crosshair (same as player.crosshairRef)
ObjectReference Function GetCrosshairRef() native global

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
