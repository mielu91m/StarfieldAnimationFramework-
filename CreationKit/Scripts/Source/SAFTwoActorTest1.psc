Scriptname SAFTwoActorTest1 extends ScriptObject

Import SAFScript

; Wywołanie z konsoli:
;   cgf "SAFTwoActorTest.Play" <refIdNpc>
;   cgf "SAFTwoActorTest.Stop" <refIdNpc>

Function Play(Actor akNpc) Global
    Actor player = Game.GetPlayer()
    if !player || !akNpc
        Debug.Notification("SAFTwoActorTest: missing player/NPC")
        return
    EndIf

    float x = player.GetPositionX()
    float y = player.GetPositionY()
    float z = player.GetPositionZ()

    player.SetPosition(x, y, z)
    akNpc.SetPosition(x, y, z)

    ; Zablokuj ruch/AI na czas animacji (Ebanex-style):
    ; - player: brak wejścia od gracza
    ; - NPC: brak pakietów AI / ruchu
    player.SetPlayerControls(false)
    akNpc.SetRestrained(true)

    ; Używaj tej samej ścieżki, która działa w konsoli (log pokazał GE\C\cw.glb)
    Bool ok1 = SAFScript.PlayOnActor(player, "GE\\s\\sw", 1.0, 0)
    Bool ok2 = SAFScript.PlayOnActor(akNpc,  "GE\\s\\sm", 1.0, 0)

    if ok1
        Debug.Notification("SAFTwoActorTest1: player sw OK")
    else
        Debug.Notification("SAFTwoActorTest1: player sw FAILED")
    EndIf

    if ok2
        Debug.Notification("SAFTwoActorTest1: npc sm OK")
    else
        Debug.Notification("SAFTwoActorTest1: npc sm FAILED")
    EndIf

    if !ok1 || !ok2
        return
    EndIf

    SAFScript.SetActorPosition(player, x, y, z)
    SAFScript.SetActorPosition(akNpc,  x, y, z)
    SAFScript.SetGraphControlsPosition(player, True)
    SAFScript.SetGraphControlsPosition(akNpc,  True)

    Actor[] pair = new Actor[2]
    pair[0] = player
    pair[1] = akNpc
    SAFScript.SyncGraphs(pair)
EndFunction

Function Stop(Actor akNpc) Global
    Actor player = Game.GetPlayer()
    if player
        player.SetPlayerControls(true)
        SAFScript.SetGraphControlsPosition(player, False)
        SAFScript.StopAnimation(player)
    EndIf
    if akNpc
        akNpc.SetRestrained(false)
        SAFScript.SetGraphControlsPosition(akNpc, False)
        SAFScript.StopAnimation(akNpc)
    EndIf
EndFunction