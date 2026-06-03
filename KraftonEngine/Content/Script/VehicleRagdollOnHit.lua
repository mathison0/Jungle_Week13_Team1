local mesh = nil

local VEHICLE_TAG = "Vehicle"
local RAGDOLL_IMPULSE_THRESHOLD = 30.0
local RAGDOLL_IMPULSE_MAX = 300.0
local RAGDOLL_IMPULSE_SCALE = 1.0

local function is_vehicle(actor)
    return actor ~= nil and actor:IsValid() and actor:HasTag(VEHICLE_TAG)
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

function Tick(dt)
end
