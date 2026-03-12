Scriptname SimpleTest extends ScriptObject

; Test czy cgf w ogóle wywołuje Papyrus. Wywołanie: cgf "SimpleTest.Ping"
Function Ping() Global
	Debug.Notification("SimpleTest.Ping OK")
EndFunction
