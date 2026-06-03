local mesh = nil

local VEHICLE_TAG = "Vehicle"
local RAGDOLL_IMPULSE_THRESHOLD = 30.0
local RAGDOLL_IMPULSE_MAX = 300.0
local RAGDOLL_IMPULSE_SCALE = 1.0
local FIREWORK_TAG = "Firework"
local FIREWORK_LIFETIME_SECONDS = 30.0
local spawnedFireworks = {}

local function is_vehicle(actor)
    return actor ~= nil and actor:IsValid() and actor:HasTag(VEHICLE_TAG)
end

local function is_firework_trigger(actor)
    return actor ~= nil and actor:IsValid() and actor:HasTag(FIREWORK_TAG)
end

local function spawn_firework_actor()
    if World == nil or World.SpawnFireworkActor == nil then
        print("[VehicleRagdollOnHit] World.SpawnFireworkActor is not registered.")
        return
    end

    local fireworkActor = World.SpawnFireworkActor()
    if fireworkActor == nil then
        return
    end

    table.insert(spawnedFireworks, {
        actor = fireworkActor,
        remaining = FIREWORK_LIFETIME_SECONDS,
    })
end

local function clamp_vector_length(vector, max_length)
    if vector == nil then
        return nil
    end

    local length = vector:Length()
    if length <= 0.0 or max_length <= 0.0 or length <= max_length then
        return vector
    end

    return vector * (max_length / length)
end

local function get_hit_location(hit_result)
    if hit_result ~= nil and hit_result.bHit then
        return hit_result.WorldHitLocation
    end

    return obj.Location
end

function BeginPlay()
    mesh = obj:GetSkeletalMesh()
end

function EndPlay()
end

function OnHit(OtherActor, HitComponent, OtherComp, NormalImpulse, HitResult)
    if mesh == nil then
        mesh = obj:GetSkeletalMesh()
    end

    if mesh == nil or mesh:IsRagdollSimulating() then
        return
    end

    if not is_vehicle(OtherActor) then
        return
    end

    local impulse = NormalImpulse
    if impulse == nil or impulse:Length() < RAGDOLL_IMPULSE_THRESHOLD then
        return
    end

    impulse = clamp_vector_length(impulse, RAGDOLL_IMPULSE_MAX)

    mesh:SetSimulateRagdoll(true)
    mesh:AddImpulseAtLocation(impulse * RAGDOLL_IMPULSE_SCALE, get_hit_location(HitResult))
end

function OnEndHit(OtherActor, OtherComp)
end

function OnOverlap(OtherActor)
    if not is_firework_trigger(OtherActor) then
        return
    end

    spawn_firework_actor()
end

function Tick(dt)
    for i = #spawnedFireworks, 1, -1 do
        local firework = spawnedFireworks[i]
        local actor = firework.actor

        if actor == nil or not actor:IsValid() then
            table.remove(spawnedFireworks, i)
        else
            firework.remaining = firework.remaining - dt
            if firework.remaining <= 0.0 then
                actor:Destroy()
                table.remove(spawnedFireworks, i)
            end
        end
    end
end
