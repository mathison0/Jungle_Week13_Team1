local cine = nil
local camera = nil
local time = 0.0

local startPosition = nil
local point1 = nil
local point2 = nil
local point3 = nil
local fireworkParticles = {}
local fireworksActivated = false

local START_WAIT = 8.0
local MOVE_TO_POINT1 = 5.0
local MOVE_TO_POINT2 = 5.0
local MOVE_TO_POINT3 = 8.0
local LETTERBOX_IN = 1.5

local START_FOCUS_DISTANCE = 6.0
local POINT1_FOCUS_DISTANCE = 2.0
local START_FOCAL_LENGTH = 24.0
local POINT2_FOCAL_LENGTH = 16.0
local POINT3_FOCAL_LENGTH = 8.0
local START_APERTURE = 2.0
local POINT2_APERTURE = 1.0
local POINT3_APERTURE = 5.0

local function clamp01(value)
    if value < 0.0 then
        return 0.0
    end
    if value > 1.0 then
        return 1.0
    end
    return value
end

local function smoothstep(value)
    local x = clamp01(value)
    return x * x * (3.0 - 2.0 * x)
end

local function lerp(a, b, alpha)
    return a + (b - a) * alpha
end

local function lerp_vector(a, b, alpha)
    return Vector.Lerp(a, b, alpha)
end

local function find_marker(name)
    local actor = World.FindActorByName(name)
    if actor == nil then
        actor = World.FindFirstActorByTag(name)
    end
    return actor
end

local function marker_location(actor, fallback)
    if actor == nil then
        return fallback
    end
    return actor.Location
end

local function apply_camera_values(focalLength, aperture, focusDistance)
    if cine == nil then
        return
    end

    cine:SetCurrentFocalLength(focalLength)
    cine:SetCurrentAperture(aperture)
    cine:SetManualFocusDistance(focusDistance)
end

local function collect_firework_particles()
    fireworkParticles = {}

    local fireworkActors = World.FindActorsByTag("Firework")
    for _, actor in ipairs(fireworkActors) do
        local particle = actor:GetParticleSystem()
        if particle ~= nil then
            table.insert(fireworkParticles, particle)
        end
    end
end

local function deactivate_fireworks()
    for _, particle in ipairs(fireworkParticles) do
        particle:Deactivate()
    end
    fireworksActivated = false
end

local function activate_fireworks()
    if fireworksActivated then
        return
    end

    for _, particle in ipairs(fireworkParticles) do
        particle:ResetSystem()
        particle:Activate()
    end
    fireworksActivated = true
end

local function apply_timeline()
    if cine == nil or startPosition == nil then
        return
    end

    local p1 = marker_location(point1, startPosition)
    local p2 = marker_location(point2, p1)
    local p3 = marker_location(point3, p2)

    local t0 = START_WAIT
    local t1 = t0 + MOVE_TO_POINT1
    local t2 = t1 + MOVE_TO_POINT2
    local t3 = t2 + MOVE_TO_POINT3

    cine:SetLetterboxAmount(smoothstep(time / LETTERBOX_IN))
    if time >= t0 then
        activate_fireworks()
    end

    if time < t0 then
        obj.Location = startPosition
        apply_camera_values(START_FOCAL_LENGTH, START_APERTURE, START_FOCUS_DISTANCE)
        return
    end

    if time < t1 then
        local alpha = smoothstep((time - t0) / MOVE_TO_POINT1)
        obj.Location = lerp_vector(startPosition, p1, alpha)
        apply_camera_values(
            START_FOCAL_LENGTH,
            START_APERTURE,
            lerp(START_FOCUS_DISTANCE, POINT1_FOCUS_DISTANCE, alpha))
        return
    end

    if time < t2 then
        local alpha = smoothstep((time - t1) / MOVE_TO_POINT2)
        obj.Location = lerp_vector(p1, p2, alpha)
        apply_camera_values(
            lerp(START_FOCAL_LENGTH, POINT2_FOCAL_LENGTH, alpha),
            lerp(START_APERTURE, POINT2_APERTURE, alpha),
            POINT1_FOCUS_DISTANCE)
        return
    end

    if time < t3 then
        local alpha = smoothstep((time - t2) / MOVE_TO_POINT3)
        obj.Location = lerp_vector(p2, p3, alpha)
        apply_camera_values(
            lerp(POINT2_FOCAL_LENGTH, POINT3_FOCAL_LENGTH, alpha),
            lerp(POINT2_APERTURE, POINT3_APERTURE, alpha),
            POINT1_FOCUS_DISTANCE)
        return
    end

    obj.Location = p3
    apply_camera_values(POINT3_FOCAL_LENGTH, POINT3_APERTURE, POINT1_FOCUS_DISTANCE)
end

function BeginPlay()
    cine = obj:GetCineCamera()
    camera = obj:GetCamera()
    if cine == nil then
        print("[DisneyCameraDOF] Cine camera component not found.")
        return
    end

    time = 0.0
    fireworksActivated = false
    startPosition = obj.Location
    point1 = find_marker("Point1")
    point2 = find_marker("Point2")
    point3 = find_marker("Point3")
    collect_firework_particles()
    deactivate_fireworks()

    if point1 == nil or point2 == nil or point3 == nil then
        print("[DisneyCameraDOF] Point1/Point2/Point3 marker is missing. Using available fallback positions.")
    end

    cine:SetDepthOfFieldEnabled(true)
    cine:SetPostProcessBlendWeight(1.0)
    cine:SetDepthOfFieldVisualizeFocusDistance(false)
    cine:SetDrawDebugFocusPlane(false)
    cine:SetDepthOfFieldScale(1.4)
    cine:SetDepthOfFieldMaxBlurSize(16.0)
    cine:SetLetterboxEnabled(true)
    cine:SetLetterboxAmount(0.0)
    cine:SetLetterboxThickness(0.12)

    AudioManager.Load("DisneyOpening", "DisneyOpening.mp3", false)
    AudioManager.PlayBGM("DisneyOpening", 0.85)
    CameraManager.PossessCamera(camera)
    apply_timeline()
end

function Tick(dt)
    time = time + dt
    apply_timeline()
end

function EndPlay()
    AudioManager.StopBGM()
end
