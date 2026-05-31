# Depth of Field Implementation Plan

## Goal

Add an Unreal Engine 5 style Depth of Field pipeline while matching the current render-pass and material batching architecture.

This document is a living planning and progress artifact. Implementation should proceed in small reviewable steps, starting from translucent batching before adding the actual DOF shader and camera model.

## Progress Tracker

Overall progress:

```text
Documentation / UE parity review: Done
Implementation progress: 0 / 7 batches accepted, 6 batches in Review, 1 batch in Progress
Current work: Batch 7 - Editor/debug polish, camera DOF cleanup, and blur quality pass
Next review point: Visual comparison of Gaussian vs Tiled Rotated Poisson Disk blur, physical CineCamera focus distance, debug focus plane, and Before/After DOF translucency separation
```

Status values:

- `Not Started`: no code changes yet.
- `In Progress`: code is being changed.
- `Review`: code compiles locally and is ready for user review.
- `Done`: reviewed or accepted.
- `Blocked`: waiting on a decision or dependency.

Batch status:

| Batch | Scope | Status | Notes |
| --- | --- | --- | --- |
| 0 | UE parity and project pipeline review | Done | Current document reflects UE-style camera, material, pass-order, and viewport-camera rules. |
| 1 | Translucency batching and render-pass skeleton | Review | Debug x64 build passes. Needs user/visual review for unchanged legacy translucency. |
| 2 | Material `Translucency Pass` authoring and asset persistence | Review | `ETranslucencyPass` added. Default and missing legacy data resolve to `AfterDOF`. |
| 3 | Camera DOF settings and frame data path | Review | Base camera owns `PostProcessSettings`; cine camera owns filmback/lens/focus/current settings and writes frame DOF state. |
| 4 | Editor viewport exclusion and runtime camera guards | Review | Runtime reads ActiveCamera DOF; normal editor and asset/ObjViewer preview paths force DOF off. |
| 5 | DOF render resources and pass stub | Review | Viewport now owns full-res CoC and half-res blur ping-pong resources; pass validates resources but remains visually no-op. |
| 6 | Initial DOF shader and composite | Review | Added CoC, half-res blur, and composite shaders. Needs in-editor visual tuning/review. |
| 7 | Editor/debug polish | In Progress | Camera DOF UI cleanup, focus visualization, debug focus plane, and blur method dropdown are implemented. Show flag and material editor visibility polish remain. |

Sync rule:

- Update `Current work` before starting a batch.
- Move the batch to `In Progress` when code changes begin.
- Add a short entry to `Implementation Log` after each meaningful code/doc sync.
- Move the batch to `Review` only after a local compile or a clearly documented reason why compile could not run.

## Implementation Log

| Date | Entry |
| --- | --- |
| 2026-05-31 | Created UE5-style DOF plan, verified camera/material/pass placement, and added progress tracking. |
| 2026-05-31 | Started Batch 1A: render pass enum and pass registration skeleton. |
| 2026-05-31 | Completed Batch 1A-1D implementation: added split translucency/DOF pass skeleton, routed legacy world `AlphaBlend` materials to `TranslucencyAfterDOF`, and updated translucent sorting. |
| 2026-05-31 | Built `Debug|x64` successfully after sanitizing duplicate `Path`/`PATH` environment variables for MSBuild. Remaining warnings are existing PhysX PDB and FBX float-conversion warnings. |
| 2026-05-31 | Started Batch 2: material `Translucency Pass` authoring and asset persistence. |
| 2026-05-31 | Completed Batch 2 implementation: added `ETranslucencyPass`, material property exposure, `.mat` `TranslucencyPass` load/save compatibility, and property-driven `AlphaBlend` routing to before/after DOF translucency passes. |
| 2026-05-31 | Built `Debug|x64` successfully. Reflection regenerated `Material.generated.h`; remaining warnings are existing FBX float-conversion and PhysX PDB warnings. |
| 2026-05-31 | Added follow-up UI polish note: hide or disable `Translucency Pass` unless the material is effectively translucent. Started Batch 3 camera DOF data path. |
| 2026-05-31 | Completed Batch 3 and Batch 4 implementation: camera/cine camera DOF settings now flow into `FFrameContext`, runtime reads the active game camera, and editor/preview viewport camera paths keep DOF disabled. Built `Debug|x64` successfully; remaining warnings are existing PhysX PDB warnings. |
| 2026-05-31 | Started Batch 5: DOF render resources and pass stub. |
| 2026-05-31 | Completed Batch 5 implementation: added viewport-owned Depth of Field CoC and blur ping-pong resources, exposed them through `FFrameContext`, and kept `DepthOfFieldPass` as a resource-validating no-op until the shader composite lands. Built `Debug|x64` successfully with 0 warnings and 0 errors. |
| 2026-05-31 | Started Batch 6: initial DOF shader and composite. |
| 2026-05-31 | Completed Batch 6 implementation: added initial DOF CoC/downsample/blur/composite shaders, registered shader paths, and wired `FDepthOfFieldPass` to execute between `TranslucencyBeforeDOF` and `TranslucencyAfterDOF`. Verified new HLSL with `fxc` for VS/PS entries and built `Debug|x64` successfully; remaining warnings are existing PhysX PDB link warnings. |
| 2026-06-01 | Started Batch 7 cleanup: removed unused focal-region/transition/override/bokeh UI fields, renamed the radius cap to `DepthOfFieldMaxBlurSize`, wired CineCamera physical focus/focal length/aperture into shader CoC, and added focus visualization/debug focus plane support. Verified DOF HLSL with `fxc` and built `Debug|x64` successfully after closing the running editor executable; remaining warnings are existing PhysX PDB link warnings. |
| 2026-06-01 | Corrected the initial Gaussian DOF blur to use per-pixel CoC as blur radius instead of using only a fixed global blur radius and composite amount. |
| 2026-06-01 | Started DOF blur quality pass: added a renderer-level `DepthOfFieldBlurMethod` option, defaulted it to Tiled Rotated Poisson Disk, added an Editor Debug dropdown for active viewport comparison against Gaussian, and added a stable Poisson gather shader path. |
| 2026-06-01 | Verified the DOF shader set, including `DepthOfFieldPoissonBlur.hlsl`, with `fxc` VS/PS entry compilation and built `Debug|x64` successfully. Remaining warnings are existing FBX float-conversion and PhysX PDB link warnings. |
| 2026-06-01 | Corrected DOF distance units for the engine's meter-based world scale: focus distance and linear scene depth now convert to millimeters with `* 1000`, and default manual focus distance changed from legacy `300` cm-style value to `3.0` meters. |

## Current Render Pass Order

The engine executes render passes in `ERenderPass` enum order.

Current order:

```text
PreDepth
LightCulling
ShadowMap
Opaque
Decal
AdditiveDecal
Fog
AlphaBlend
SelectionMask
EditorLines
PostProcess
FXAA
Bloom
GizmoOuter
GizmoInner
OverlayFont
UI
GammaCorrection
```

`AlphaBlend` currently represents all translucent world drawing.

## Target Render Pass Order

Use UE5-style translucency placement around DOF.

Proposed order:

```text
PreDepth
LightCulling
ShadowMap
Opaque
Decal
AdditiveDecal
Fog
TranslucencyBeforeDOF
DepthOfField
TranslucencyAfterDOF
SelectionMask
EditorLines
PostProcess
FXAA
Bloom
GizmoOuter
GizmoInner
OverlayFont
UI
GammaCorrection
```

Rationale:

- `TranslucencyBeforeDOF` is part of the photographed scene and should be blurred by DOF.
- `TranslucencyAfterDOF` stays sharp and is rendered over the DOF-composited scene.
- Editor selection masks, editor lines, gizmos, overlay text, UI, and gamma correction remain after DOF.
- `DepthOfField` should be a dedicated render pass, not folded into the existing `PostProcess` pass, because it needs multi-step scene color/depth reads and intermediate render targets like `BloomPass`.

## UE5 Naming Preference

Use Unreal-facing names where possible:

- `DepthOfField`
- `TranslucencyBeforeDOF`
- `TranslucencyAfterDOF`
- `FPostProcessSettings`
- `FDepthOfFieldSettings`
- `FCameraFilmbackSettings`
- `FCameraFocusSettings`
- `DepthOfFieldFstop`
- `DepthOfFieldScale`
- `DepthOfFieldMaxBlurSize`
- `bVisualizeFocusDistance`
- `bDrawDebugFocusPlane`
- `ManualFocusDistance`
- `CurrentAperture`
- `CurrentFocalLength`
- `CurrentFocusDistance`
- `CurrentHorizontalFOV`
- `Current Camera Settings`

Keep `AlphaBlend` as a legacy compatibility alias at first. Existing `AlphaBlend` materials should behave like `TranslucencyAfterDOF` by default, matching UE's safe default of keeping legacy translucency sharp.

## UE5 Parity Notes

Verified UE5 behavior to mirror:

- `UCameraComponent` exposes camera projection and `PostProcessSettings`.
- UE's `FPostProcessSettings` owns many raw DOF override properties. This engine exposes only the DOF controls that are currently implemented: enable, scale, max blur size, and focus visualization.
- `UCineCameraComponent` inherits from `UCameraComponent`, but adds physical/cinematic camera data.
- `UCineCameraComponent` should not be authored through the inherited base `FOV` field. Its render `FOV` is derived from `Filmback.SensorHeight` and `CurrentFocalLength`; the editor details panel should hide the base `FOV` row for cine cameras.
- UE exposes `LensSettings` with min/max focal length and min/max F-stop to model lens presets, zoom lenses, and lens limits. This project intentionally does not implement that lens constraint layer; expose the current values directly instead.
- This project exposes `Filmback`, `FocusSettings`, `CurrentFocalLength`, `CurrentAperture`, `CurrentFocusDistance`, and `CurrentHorizontalFOV` under the editor category `Current Camera Settings`.
- `CurrentFocalLength` and `CurrentAperture` are editable current cine camera values.
- `CurrentFocusDistance` is derived/read-only from focus settings.
- `CurrentHorizontalFOV` is derived/read-only from focal length and filmback.
- Default CineCamera framing should match the default base Camera framing in this engine. With the engine's 60 degree vertical FOV and default 36.0 x 20.25mm filmback, the matching default focal length is about 17.54mm.
- First implementation supports `Manual` and `Disable` focus methods. Tracking focus and smooth focus changes are deferred because they need target binding/interpolation behavior that is not yet present.
- Diaphragm blade count and true aperture-shaped bokeh are deferred. The current shader is a blur-radius based DOF implementation, so the editor should call the radius cap `Max Blur Size` instead of `Max Bokeh Size`.
- UE material translucency pass values include `Before DOF`, `After DOF`, and `After Motion Blur`. This engine should implement `Before DOF` and `After DOF` first because the current pipeline does not have motion blur yet.
- UE's default translucent material behavior is `After DOF`.

## Phase 1: Translucency Batching

Objective:

Create the render-pass slots needed to draw translucent objects before or after DOF without implementing the DOF effect yet.

Batch split:

### Batch 1A: Render Pass Skeleton

Status: `Done`

Goal:

Add the enum-level and class-level slots so the renderer can execute UE-style translucency placement.

Tasks:

1. Add `TranslucencyBeforeDOF`, `DepthOfField`, and `TranslucencyAfterDOF` to `ERenderPass` in the target order.
2. Update `GetRenderPassName()` and `RenderPassMap`.
3. Add render-pass classes:
   - `FTranslucencyBeforeDOFPass`
   - `FDepthOfFieldPass`
   - `FTranslucencyAfterDOFPass`
4. Register the passes through the existing render pass registry.
5. Make `FDepthOfFieldPass` a safe no-op until camera settings and resources exist.

Done when:

- Render pass registry creates the new passes in enum order.
- The engine compiles with no visual behavior change expected.

### Batch 1B: Render State Defaults

Status: `Done`

Goal:

Make the new translucency passes behave like the current `AlphaBlend` pass at the render-state level.

Tasks:

1. Give `TranslucencyBeforeDOF` and `TranslucencyAfterDOF` these defaults:
   - `DepthReadOnly`
   - `AlphaBlend`
   - `SolidBackCull`
   - `TRIANGLELIST`
2. Update material manager default state resolution.
3. Keep `AlphaBlend` compatibility for old assets and helper draws.

Done when:

- Materials routed to either new translucency pass receive the same draw state as current translucent rendering.

### Batch 1C: Command Routing

Status: `Done`

Goal:

Route legacy world translucent material commands to `TranslucencyAfterDOF` by default.

Tasks:

1. Add a small resolver near draw command building:
   - old `AlphaBlend` material route -> `TranslucencyAfterDOF`
   - explicit `TranslucencyBeforeDOF` -> before DOF
   - explicit `TranslucencyAfterDOF` -> after DOF
2. Use the resolver in mesh command building.
3. Use the resolver in particle command building.
4. Keep non-world helper paths deliberate:
   - world text currently uses `AlphaBlend`
   - screen text uses `OverlayFont`
   - editor/debug lines remain after DOF

Done when:

- World mesh/particle translucency no longer depends on the old single `AlphaBlend` render pass for its effective DOF placement.

### Batch 1D: Translucent Sorting

Status: `Done`

Goal:

Preserve current back-to-front translucent sorting after splitting the pass.

Tasks:

1. Update `FDrawCommandList::Sort()` so these passes share translucent sort behavior:
   - `AlphaBlend`
   - `TranslucencyBeforeDOF`
   - `TranslucencyAfterDOF`
2. Keep pass order primary, then translucent priority/depth within each pass.

Done when:

- Before/after DOF translucent commands both sort like current `AlphaBlend`.

### Batch 1E: Batching Validation

Status: `Review`

Goal:

Verify the batching split is behavior-preserving before adding DOF visuals.

Tasks:

1. Compile.
2. Run a simple scene with existing translucent materials.
3. Confirm no expected visual change except internal pass routing.
4. Update this document:
   - move Batch 1 to `Review` or `Done`
   - update `Current work`
   - add an `Implementation Log` entry

Done when:

- The user can review the split before camera/material authoring continues.

Tasks:

1. Add `TranslucencyBeforeDOF`, `DepthOfField`, and `TranslucencyAfterDOF` to `ERenderPass` in the target order.
2. Add names and string mappings for the new render passes.
3. Add render-pass classes:
   - `FTranslucencyBeforeDOFPass`
   - `FTranslucencyAfterDOFPass`
   - `FDepthOfFieldPass` as a stub that returns false until camera settings and resources are ready.
4. Give both translucency passes the same default render state as current `AlphaBlend`:
   - `DepthReadOnly`
   - `AlphaBlend`
   - `SolidBackCull`
   - `TRIANGLELIST`
5. Keep `AlphaBlend` load compatibility, but route world translucent material commands to `TranslucencyAfterDOF` by default.
6. Update translucent sorting checks so `TranslucencyBeforeDOF`, `TranslucencyAfterDOF`, and legacy `AlphaBlend` all sort back-to-front.
7. Keep world text and other legacy `AlphaBlend` helper paths sharp unless they explicitly opt into `BeforeDOF`. Existing text render proxies currently use `AlphaBlend`, so this needs a deliberate routing decision during implementation.

Review point:

At the end of this phase, nothing visually changes except the pass names and route. Existing translucent materials should still render after the future DOF pass.

## Phase 2: Material Authoring Model

Objective:

Avoid making artists choose render-pass internals directly for translucent DOF behavior.

Recommended model:

```cpp
UENUM()
enum class ETranslucencyPass
{
    BeforeDOF,
    AfterDOF
};
```

Tasks:

1. Add a material property named `Translucency Pass` using UE-style values:
   - `Before DOF`
   - `After DOF`
2. Default `Translucency Pass` to `AfterDOF`.
3. Show this property only when the material is translucent. In the current project, that means materials whose effective blend/render route is the old `AlphaBlend` translucent path.
4. Keep `RenderPass` as the low-level engine route for now, but avoid asking artists to edit `RenderPass` directly for DOF placement.
5. During command building, resolve translucent material settings into:
   - `TranslucencyBeforeDOF`
   - `TranslucencyAfterDOF`
6. Default all legacy translucent materials to `AfterDOF`.
7. Save/load the new material property in `.mat` files.
8. Treat missing `TranslucencyPass` data as `AfterDOF` when loading old assets.

Recommended command-building rule:

```cpp
ERenderPass ResolveMaterialRenderPass(const UMaterialInterface* Material)
{
    const ERenderPass Pass = Material ? Material->GetRenderPass() : ERenderPass::Opaque;

    if (Pass != ERenderPass::AlphaBlend)
    {
        return Pass;
    }

    return Material->GetTranslucencyPass() == ETranslucencyPass::BeforeDOF
        ? ERenderPass::TranslucencyBeforeDOF
        : ERenderPass::TranslucencyAfterDOF;
}
```

Implementation note:

The project currently stores `RenderPass`, `BlendState`, `DepthStencilState`, and `RasterizerState` directly on `UMaterial`. For UE-like authoring, `Translucency Pass` should be an additional material property, not a replacement for low-level render state.

Review point:

After this phase, material UI and asset data can express the DOF translucency decision without exposing too much render-pipeline detail.

## Phase 3: Camera DOF Data Path

Objective:

Add UE-style camera DOF settings and pass the resolved values through the existing frame context.

Recommended ownership:

- `UCameraComponent` owns base projection settings and camera post-process settings.
- `UCameraComponent::PostProcessSettings` owns raw render-facing DOF controls.
- `UCineCameraComponent` owns cinematic camera inputs: filmback, lens, focus, current focal length, current aperture, and derived current focus/FOV values.
- `UCineCameraComponent` resolves its cinematic inputs into the same frame-level DOF state consumed by the renderer.

This differs from putting all physical camera values directly on `UCameraComponent`. That approach is simple, but UE5 keeps the cine-specific physical camera model on `UCineCameraComponent`, while the base camera component exposes post-process overrides.

Recommended structures:

```cpp
struct FDepthOfFieldSettings
{
    bool bEnableDepthOfField = false;

    float DepthOfFieldScale = 1.0f;
    float DepthOfFieldMaxBlurSize = 12.0f;
    bool bVisualizeFocusDistance = false;
};

struct FPostProcessSettings
{
    FDepthOfFieldSettings DepthOfField;
};

struct FCameraFilmbackSettings
{
    float SensorWidth = 36.0f;
    float SensorHeight = 20.25f;
};

enum class ECameraFocusMethod
{
    Manual,
    Disable
};

struct FCameraFocusSettings
{
    ECameraFocusMethod FocusMethod = ECameraFocusMethod::Manual;
    float ManualFocusDistance = 3.0f;
    bool bDrawDebugFocusPlane = false;
};

struct FCameraDepthOfFieldState
{
    bool bEnabled = false;

    float DepthOfFieldFstop = 5.6f;
    float DepthOfFieldScale = 1.0f;
    float DepthOfFieldMaxBlurSize = 12.0f;
    bool bVisualizeFocusDistance = false;
    bool bDrawDebugFocusPlane = false;

    float SensorWidth = 36.0f;
    float SensorHeight = 20.25f;
    float CurrentAperture = 5.6f;
    float CurrentFocalLength = 17.54f;
    float CurrentFocusDistance = 3.0f;
    float CurrentHorizontalFOV = 0.0f;
};
```

Data flow:

```text
UCameraComponent / UCineCameraComponent
-> GameRenderPipeline::BuildFrame or EditorRenderPipeline::BuildFrame
-> FFrameContext::CameraDepthOfField
-> FDepthOfFieldPass
-> DepthOfField.hlsl constant buffer
```

Review point:

Renderer code should not directly inspect camera components. It should only consume `FFrameContext::CameraDepthOfField`.

Editor viewport exclusion:

DOF must not be applied to the normal editor viewport camera.

Current project state:

- `FViewportCameraTransform` is a plain editor struct, not a `UCameraComponent`.
- `FEditorViewportClient::GetCameraView()` converts that transform directly into `FMinimalViewInfo`.
- Asset preview viewport clients also use editor/preview camera transforms and `FMinimalViewInfo`.
- `FEditorRenderPipeline::RenderViewport()` starts with the editor viewport POV and only switches to the game camera POV in PIE possessed mode when the current viewport is the game viewport.

Recommended guard:

```cpp
const bool bAllowCameraPostProcess = bShouldUseGameCamera;
```

Use this guard when resolving `FPostProcessSettings`, cine camera DOF, and future camera-driven post effects in editor rendering.

Rules:

- `FGameRenderPipeline::BuildFrame()` may resolve DOF from the active player camera.
- `FEditorRenderPipeline::BuildFrame()` may resolve DOF only when rendering the PIE possessed game viewport.
- Normal editor perspective/orthographic viewports force `Frame.CameraDepthOfField.bEnabled = false`.
- Asset preview viewports force `Frame.CameraDepthOfField.bEnabled = false` unless a preview tool explicitly opts into camera post-process later.
- Orthographic views should disable DOF even when the settings exist.

Recommended editor exposure:

- `UCameraComponent`
  - Camera/projection settings.
  - `PostProcessSettings`.
  - `PostProcessBlendWeight` if the project wants to match UE camera blending later.
  - `PostProcessSettings.DepthOfField` implemented render-facing controls:
    - `Enable Depth of Field`
    - `DepthOfFieldScale`
    - `DepthOfFieldMaxBlurSize`
    - `bVisualizeFocusDistance`
- `UCineCameraComponent`
  - `Current Camera Settings`
    - `CurrentFocalLength`
    - `CurrentAperture`
    - `CurrentFocusDistance` as read-only/derived from focus settings
    - `CurrentHorizontalFOV` as read-only/derived from filmback and focal length
    - `Filmback`
    - `FocusSettings`

Component ownership summary:

```text
UCameraComponent
  Projection/FOV/Ortho
  PostProcessSettings
    Enable Depth of Field
    DepthOfFieldScale
    DepthOfFieldMaxBlurSize
    Visualize Focus Distance
  PostProcessBlendWeight

UCineCameraComponent
  Filmback
  FocusSettings
    FocusMethod: Manual or Disable
    ManualFocusDistance
    DrawDebugFocusPlane
  CurrentFocalLength
  CurrentAperture
  CurrentFocusDistance
  CurrentHorizontalFOV
```

Do not put filmback, lens presets, or focus method directly on the base `UCameraComponent`. The base camera can still drive DOF through `PostProcessSettings`; the cine camera adds physical inputs and resolves them into the same renderer-facing state. For cine cameras, projection FOV is output data derived from sensor size and focal length, not the authoring input.

Current Camera Settings details:

- `CurrentFocalLength`
- `CurrentAperture`
- `CurrentFocusDistance` derived from focus settings
- `CurrentHorizontalFOV` derived from filmback/focal length
- `Filmback`
- `FocusSettings`

Deferred UE parity:

- `Tracking` focus method.
- Smooth focus interpolation.
- Diaphragm blade count and blade-shaped bokeh.
- UE-style lens presets and min/max lens limits.
- Post-process DOF focal-region and transition-region controls. These are not meaningful until the shader implements that model.

Legacy note:

The existing project already has `UCineCameraComponent` letterbox settings. DOF should follow the same active-camera extraction pattern in game runtime, but the editor pipeline needs the `bAllowCameraPostProcess` guard above so normal viewport cameras do not inherit game camera DOF.

## Phase 4: DOF Render Resources

Objective:

Provide intermediate textures for DOF without overloading bloom resources.

Recommended resources:

- Full-resolution scene color copy: existing `SceneColorCopyTexture`.
- Depth copy: existing `DepthCopyTexture`.
- Half/quarter-resolution DOF blur ping-pong textures:
  - `DepthOfFieldRTVA`
  - `DepthOfFieldSRVA`
  - `DepthOfFieldRTVB`
  - `DepthOfFieldSRVB`

Tasks:

1. Add DOF render targets to `FViewport`.
2. Populate them into `FFrameContext`.
3. Release them with viewport resources.
4. Add system texture slot if needed, or reuse pass-local binding slots carefully.

Review point:

DOF should not reuse bloom textures unless the pipeline guarantees no overlap and the naming remains clear. Separate resources are easier to debug.

## Phase 5: DOF Shader and Pass

Objective:

Implement the first usable DOF pass.

Recommended initial algorithm:

1. Copy current scene color into `SceneColorCopyTexture`.
2. Copy current depth into `DepthCopyTexture`.
3. Generate Circle of Confusion from reversed-Z depth.
4. Downsample and blur scene color with a per-pixel radius derived from CoC.
5. Composite sharp scene color and blurred color back into `ViewportRenderTexture`.

Important depth rule:

The engine uses reversed-Z. Depth is `1` near and `0` far. DOF shader depth linearization must match existing `SceneDepth.hlsl`.

Review point:

Start with a stable Gaussian/dual-filter blur before attempting UE-like cinematic bokeh. Correct ordering and data path matter more than bokeh shape in the first implementation.

Physical DOF parameter path:

The current implementation feeds CineCamera physical values into the DOF pass and shader constants.

```cpp
struct FDepthOfFieldShaderParameters
{
    float FocusDistanceMM;
    float FocalLengthMM;
    float FStop;

    float SensorWidthMM;
    float SensorHeightMM;

    float DepthOfFieldScale;
    float DepthOfFieldMaxBlurSize;
    float RenderTargetHeight;

    float VisualizeFocusDistance;
    float DrawDebugFocusPlane;
};
```

Recommended calculations:

```cpp
VerticalFOV = 2.0f * atan(SensorHeightMM / (2.0f * FocalLengthMM));
ApertureDiameterMM = FocalLengthMM / FStop;
```

For physical circle of confusion, compute signed CoC in millimeters and convert it into a pixel radius:

```text
WorldDistanceMeters * 1000 -> DistanceMM
SignedCoCMM -> SignedCoCPixels -> clamped blur radius
```

Project unit rule:

- The engine world unit is meters.
- Camera focus distances are authored in meters.
- Physical DOF shader math converts world-space depth and focus distance to millimeters with `* 1000`.

`DepthOfFieldMaxBlurSize` is a screen-space clamp for the current blur implementation. Normalized signed CoC is converted back into a pixel radius with this value before blurring. It is intentionally named as blur size instead of bokeh size because the shader does not yet simulate blade-shaped aperture highlights.

Current blur methods:

- `Gaussian`: legacy two-pass separable blur. It is useful as a baseline comparison, but directional/horizontal-vertical sampling can expose patterned blur on large radii.
- `TiledRotatedPoissonDisk`: default method. It performs a stable half-resolution aperture gather with a deterministic 4x4 tiled rotation, so it reduces directional banding without per-frame random jitter.

The blur method is stored in `FViewportRenderOptions`, not camera settings. This keeps it as a renderer/editor debug quality switch while camera and cine camera components continue to own physical DOF authoring values.

## Phase 6: Editor and Debug Controls

Objective:

Make the feature testable.

Tasks:

1. Add show flag:
   - `bDepthOfField`
2. Add editor toolbar/property controls.
3. Add debug view option if useful:
   - focus distance visualization
   - debug focus plane overlay
4. Add material editor option for `Translucency Pass`.
5. Polish material editor visibility:
   - show or enable `Translucency Pass` only when the material is effectively translucent
   - keep default `AfterDOF` visible in asset data even when the UI hides the control

Review point:

The first debugging UI should expose enough values to test focus distance, blur strength, and Before/After DOF separation.

## Suggested Implementation Order

1. Batch 1A: render pass enum and pass registration skeleton.
2. Batch 1B: render state defaults for split translucency passes.
3. Batch 1C: mesh/particle command routing.
4. Batch 1D: translucent sorting for both DOF translucency passes.
5. Batch 1E: compile and behavior-preserving validation.
6. Batch 2: material `Translucency Pass` authoring and `.mat` persistence.
7. Batch 3: camera DOF settings and frame data path.
8. Batch 4: editor viewport exclusion and runtime camera guards.
9. Batch 5: DOF render resources and pass stub.
10. Batch 6: first usable DOF shader and composite.
11. Batch 7: editor/debug polish.

Each batch should compile independently whenever possible. If a batch cannot compile independently, the reason must be recorded in `Implementation Log`.
