Scriptname MySAFExample extends ScriptObject

Function PlayAnimOnActor(Actor akActor, String animId)
    SAFScript.PlayOnActor(akActor, animId)
EndFunction

Function PlayAnimOnPlayer(String animId)
    SAFScript.PlayOnPlayer(animId)
EndFunction
