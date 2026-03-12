Scriptname SAFPingTest extends ScriptObject

; Test czy SAFScript.Ping() (natyw) jest wywoływany. Wywołanie: cgf "SAFPingTest.Test"
Function Test() Global
	Bool ok = SAFScript.Ping()
	If ok
		Debug.Notification("SAFScript.Ping returned true")
	Else
		Debug.Notification("SAFScript.Ping returned false")
	EndIf
EndFunction
