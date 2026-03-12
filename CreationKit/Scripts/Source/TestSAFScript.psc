Scriptname TestSAFScript extends ScriptObject
Import SAF

Function Test(Actor akActor) Global
    Debug.Notification("TestSAF: calling SAF.PlayAnimation")
    SAF.PlayAnimation(akActor, "bw", 1.0)
EndFunction