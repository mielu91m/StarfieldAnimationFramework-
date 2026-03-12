Scriptname SAFAlign extends ScriptObject

Function CopyTransform(Actor aFrom, Actor aTo) Global
    if aFrom == None || aTo == None
        return
    endif

    float ax = aFrom.GetAngleX()
    float ay = aFrom.GetAngleY()
    float az = aFrom.GetAngleZ()

    float px = aFrom.GetPositionX()
    float py = aFrom.GetPositionY()
    float pz = aFrom.GetPositionZ()

    aTo.SetPosition(px, py, pz)
    aTo.SetAngle(ax, ay, az)
EndFunction