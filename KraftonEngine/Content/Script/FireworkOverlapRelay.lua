local TARGET_TAG = "Yui"
local activeTargets = {}

local function is_target(actor)
    return actor ~= nil and actor:IsValid() and actor:HasTag(TARGET_TAG)
end

local function dispatch_to_target(target_actor)
    if World == nil or World.DispatchOverlapToActorScript == nil then
        print("[FireworkOverlapRelay] World.DispatchOverlapToActorScript is not registered.")
        return
    end

    World.DispatchOverlapToActorScript(target_actor, obj)
end

function BeginPlay()
end

function EndPlay()
    activeTargets = {}
end

function OnOverlap(OtherActor)
    if not is_target(OtherActor) then
        return
    end

    local uuid = OtherActor.UUID
    if uuid ~= nil and activeTargets[uuid] then
        return
    end

    if uuid ~= nil then
        activeTargets[uuid] = true
    end

    dispatch_to_target(OtherActor)
end

function OnEndOverlap(OtherActor)
    if OtherActor == nil then
        return
    end

    local uuid = OtherActor.UUID
    if uuid ~= nil then
        activeTargets[uuid] = nil
    end
end

function Tick(dt)
end
