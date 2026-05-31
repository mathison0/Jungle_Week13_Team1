#include "DepthOfFieldPass.h"
#include "RenderPassRegistry.h"

#include "Render/Device/D3DDevice.h"
#include "Render/Resource/RenderResources.h"
#include "Render/Shader/ShaderManager.h"
#include "Render/Types/RenderConstants.h"
#include "Render/Types/FrameContext.h"
#include "Render/Command/DrawCommandList.h"

REGISTER_RENDER_PASS(FDepthOfFieldPass)

namespace
{
	struct FDepthOfFieldConstants
	{
		FVector2 SceneTexelSize;
		FVector2 BlurTexelSize;
		float FocusDistanceMM = 3000.0f;
		float FocalLengthMM = 50.0f;
		float FStop = 5.6f;
		float SensorHeightMM = 20.25f;
		float NearClip = 0.1f;
		float FarClip = 1000.0f;
		float RenderTargetHeight = 1.0f;
		float DepthOfFieldScale = 1.0f;
		float DepthOfFieldMaxBlurSize = 12.0f;
		float VisualizeFocusDistance = 0.0f;
		float DrawDebugFocusPlane = 0.0f;
		float Pad0 = 0.0f;
		FVector2 BlurDirection;
		FVector2 Pad1;
	};

	void DrawFullscreenTriangle(ID3D11DeviceContext* DC)
	{
		DC->IASetInputLayout(nullptr);
		DC->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
		DC->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		DC->Draw(3, 0);
	}

	constexpr float WorldUnitsToMillimeters = 1000.0f;
}

FDepthOfFieldPass::FDepthOfFieldPass()
{
	PassType = ERenderPass::DepthOfField;
	RenderState = { EDepthStencilState::NoDepth, EBlendState::Opaque,
	                ERasterizerState::SolidNoCull, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, false };
}

bool FDepthOfFieldPass::BeginPass(const FPassContext& Ctx)
{
	const FFrameContext& Frame = Ctx.Frame;
	if (!Frame.CameraDepthOfField.bEnabled)
	{
		return false;
	}

	if (Frame.CameraDepthOfField.DepthOfFieldMaxBlurSize <= 0.0f
		&& !Frame.CameraDepthOfField.bVisualizeFocusDistance
		&& !Frame.CameraDepthOfField.bDrawDebugFocusPlane)
	{
		return false;
	}

	const bool bHasInputResources =
		Frame.ViewportRenderTexture
		&& Frame.SceneColorCopyTexture
		&& Frame.SceneColorCopySRV
		&& Frame.DepthCopyTexture
		&& Frame.DepthCopySRV;

	const bool bHasDepthOfFieldResources =
		Frame.DepthOfFieldCoCRTV
		&& Frame.DepthOfFieldCoCSRV
		&& Frame.DepthOfFieldBlurRTVA
		&& Frame.DepthOfFieldBlurSRVA
		&& Frame.DepthOfFieldBlurRTVB
		&& Frame.DepthOfFieldBlurSRVB;

	if (!bHasInputResources || !bHasDepthOfFieldResources)
	{
		return false;
	}

	return true;
}

void FDepthOfFieldPass::Execute(const FPassContext& Ctx)
{
	const FFrameContext& Frame = Ctx.Frame;
	ID3D11DeviceContext* DC = Ctx.Device.GetDeviceContext();
	if (!DC)
	{
		return;
	}

	FShader* CoCShader = FShaderManager::Get().GetOrCreate(EShaderPath::DepthOfFieldCoC);
	FShader* DownsampleShader = FShaderManager::Get().GetOrCreate(EShaderPath::DepthOfFieldDownsample);
	FShader* BlurShader = FShaderManager::Get().GetOrCreate(EShaderPath::DepthOfFieldBlur);
	FShader* PoissonBlurShader = FShaderManager::Get().GetOrCreate(EShaderPath::DepthOfFieldPoissonBlur);
	FShader* CompositeShader = FShaderManager::Get().GetOrCreate(EShaderPath::DepthOfFieldComposite);
	if (!CoCShader || !DownsampleShader || !BlurShader || !PoissonBlurShader || !CompositeShader)
	{
		return;
	}

	if (!DepthOfFieldCB.GetBuffer())
	{
		DepthOfFieldCB.Create(Ctx.Device.GetDevice(), sizeof(FDepthOfFieldConstants), "DepthOfFieldCB");
	}

	Ctx.Resources.SetDepthStencilState(Ctx.Device, EDepthStencilState::NoDepth);
	Ctx.Resources.SetBlendState(Ctx.Device, EBlendState::Opaque);
	Ctx.Resources.SetRasterizerState(Ctx.Device, ERasterizerState::SolidNoCull);

	const float InvSceneWidth = Frame.ViewportWidth > 0.0f ? 1.0f / Frame.ViewportWidth : 0.0f;
	const float InvSceneHeight = Frame.ViewportHeight > 0.0f ? 1.0f / Frame.ViewportHeight : 0.0f;
	const float InvBlurWidth = Frame.DepthOfFieldBlurWidth > 0.0f ? 1.0f / Frame.DepthOfFieldBlurWidth : 0.0f;
	const float InvBlurHeight = Frame.DepthOfFieldBlurHeight > 0.0f ? 1.0f / Frame.DepthOfFieldBlurHeight : 0.0f;

	FDepthOfFieldConstants DOFData = {};
	DOFData.SceneTexelSize = FVector2(InvSceneWidth, InvSceneHeight);
	DOFData.BlurTexelSize = FVector2(InvBlurWidth, InvBlurHeight);
	DOFData.FocusDistanceMM = Frame.CameraDepthOfField.CurrentFocusDistance * WorldUnitsToMillimeters;
	DOFData.FocalLengthMM = Frame.CameraDepthOfField.CurrentFocalLength;
	DOFData.FStop = Frame.CameraDepthOfField.DepthOfFieldFstop;
	DOFData.SensorHeightMM = Frame.CameraDepthOfField.SensorHeight;
	DOFData.NearClip = Frame.NearClip;
	DOFData.FarClip = Frame.FarClip;
	DOFData.RenderTargetHeight = Frame.ViewportHeight;
	DOFData.DepthOfFieldScale = Frame.CameraDepthOfField.DepthOfFieldScale;
	DOFData.DepthOfFieldMaxBlurSize = Frame.CameraDepthOfField.DepthOfFieldMaxBlurSize;
	DOFData.VisualizeFocusDistance = Frame.CameraDepthOfField.bVisualizeFocusDistance ? 1.0f : 0.0f;
	DOFData.DrawDebugFocusPlane = Frame.CameraDepthOfField.bDrawDebugFocusPlane ? 1.0f : 0.0f;
	DepthOfFieldCB.Update(DC, &DOFData, sizeof(DOFData));

	ID3D11Buffer* DOFCBRaw = DepthOfFieldCB.GetBuffer();
	DC->VSSetConstantBuffers(ECBSlot::PerShader0, 1, &DOFCBRaw);
	DC->PSSetConstantBuffers(ECBSlot::PerShader0, 1, &DOFCBRaw);

	ID3D11ShaderResourceView* NullSRV = nullptr;
	ID3D11ShaderResourceView* NullSystemSRVs[13] = {};

	D3D11_VIEWPORT SceneViewport = {};
	SceneViewport.Width = Frame.ViewportWidth;
	SceneViewport.Height = Frame.ViewportHeight;
	SceneViewport.MinDepth = 0.0f;
	SceneViewport.MaxDepth = 1.0f;

	D3D11_VIEWPORT BlurViewport = {};
	BlurViewport.Width = Frame.DepthOfFieldBlurWidth;
	BlurViewport.Height = Frame.DepthOfFieldBlurHeight;
	BlurViewport.MinDepth = 0.0f;
	BlurViewport.MaxDepth = 1.0f;

	DC->CopyResource(Frame.SceneColorCopyTexture, Frame.ViewportRenderTexture);

	// 1. Circle of Confusion: SceneDepth -> full-res CoC.
	DC->RSSetViewports(1, &SceneViewport);
	DC->OMSetRenderTargets(1, &Frame.DepthOfFieldCoCRTV, nullptr);
	DC->PSSetShaderResources(ESystemTexSlot::SceneDepth, 1, &Frame.DepthCopySRV);
	CoCShader->Bind(DC);
	DrawFullscreenTriangle(DC);
	DC->PSSetShaderResources(ESystemTexSlot::SceneDepth, 1, &NullSRV);

	// 2. Downsample scene color into half-res DOF buffer A.
	DC->RSSetViewports(1, &BlurViewport);
	DC->OMSetRenderTargets(1, &Frame.DepthOfFieldBlurRTVA, nullptr);
	DC->PSSetShaderResources(ESystemTexSlot::SceneColor, 1, &Frame.SceneColorCopySRV);
	DC->PSSetShaderResources(ESystemTexSlot::DepthOfFieldCoC, 1, &Frame.DepthOfFieldCoCSRV);
	DownsampleShader->Bind(DC);
	DrawFullscreenTriangle(DC);
	DC->PSSetShaderResources(ESystemTexSlot::SceneColor, 1, &NullSRV);
	DC->PSSetShaderResources(ESystemTexSlot::DepthOfFieldCoC, 1, &NullSRV);

	ID3D11ShaderResourceView* FinalBlurSRV = Frame.DepthOfFieldBlurSRVA;
	if (Frame.RenderOptions.DepthOfFieldBlurMethod == EDepthOfFieldBlurMethod::Gaussian)
	{
		// 3A. Separable Gaussian blur: DOF A -> DOF B -> DOF A.
		DOFData.BlurDirection = FVector2(1.0f, 0.0f);
		DepthOfFieldCB.Update(DC, &DOFData, sizeof(DOFData));
		DC->OMSetRenderTargets(1, &Frame.DepthOfFieldBlurRTVB, nullptr);
		DC->PSSetShaderResources(ESystemTexSlot::DepthOfFieldBlur, 1, &Frame.DepthOfFieldBlurSRVA);
		BlurShader->Bind(DC);
		DrawFullscreenTriangle(DC);
		DC->PSSetShaderResources(ESystemTexSlot::DepthOfFieldBlur, 1, &NullSRV);

		DOFData.BlurDirection = FVector2(0.0f, 1.0f);
		DepthOfFieldCB.Update(DC, &DOFData, sizeof(DOFData));
		DC->OMSetRenderTargets(1, &Frame.DepthOfFieldBlurRTVA, nullptr);
		DC->PSSetShaderResources(ESystemTexSlot::DepthOfFieldBlur, 1, &Frame.DepthOfFieldBlurSRVB);
		BlurShader->Bind(DC);
		DrawFullscreenTriangle(DC);
		DC->PSSetShaderResources(ESystemTexSlot::DepthOfFieldBlur, 1, &NullSRV);

		FinalBlurSRV = Frame.DepthOfFieldBlurSRVA;
	}
	else
	{
		// 3B. Stable aperture gather: DOF A -> DOF B.
		DOFData.BlurDirection = FVector2(0.0f, 0.0f);
		DepthOfFieldCB.Update(DC, &DOFData, sizeof(DOFData));
		DC->OMSetRenderTargets(1, &Frame.DepthOfFieldBlurRTVB, nullptr);
		DC->PSSetShaderResources(ESystemTexSlot::DepthOfFieldBlur, 1, &Frame.DepthOfFieldBlurSRVA);
		PoissonBlurShader->Bind(DC);
		DrawFullscreenTriangle(DC);
		DC->PSSetShaderResources(ESystemTexSlot::DepthOfFieldBlur, 1, &NullSRV);

		FinalBlurSRV = Frame.DepthOfFieldBlurSRVB;
	}

	// 4. Composite: sharp scene + blurred scene -> viewport scene color.
	DC->RSSetViewports(1, &SceneViewport);
	DC->OMSetRenderTargets(1, &Ctx.Cache.RTV, Ctx.Cache.DSV);
	DC->PSSetShaderResources(ESystemTexSlot::SceneColor, 1, &Frame.SceneColorCopySRV);
	DC->PSSetShaderResources(ESystemTexSlot::DepthOfFieldCoC, 1, &Frame.DepthOfFieldCoCSRV);
	DC->PSSetShaderResources(ESystemTexSlot::DepthOfFieldBlur, 1, &FinalBlurSRV);
	CompositeShader->Bind(DC);
	DrawFullscreenTriangle(DC);

	DC->PSSetShaderResources(ESystemTexSlot::SceneColor, 1, &NullSRV);
	DC->PSSetShaderResources(ESystemTexSlot::DepthOfFieldCoC, 1, &NullSRV);
	DC->PSSetShaderResources(ESystemTexSlot::DepthOfFieldBlur, 1, &NullSRV);
	DC->PSSetShaderResources(ESystemTexSlot::SceneDepth, ARRAYSIZE(NullSystemSRVs), NullSystemSRVs);

	ID3D11Buffer* NullCB = nullptr;
	DC->VSSetConstantBuffers(ECBSlot::PerShader0, 1, &NullCB);
	DC->PSSetConstantBuffers(ECBSlot::PerShader0, 1, &NullCB);

	Ctx.Cache.bForceAll = true;
}

void FDepthOfFieldPass::EndPass(const FPassContext& Ctx)
{
	ID3D11ShaderResourceView* NullSRV = nullptr;
	ID3D11DeviceContext* DC = Ctx.Device.GetDeviceContext();
	DC->PSSetShaderResources(ESystemTexSlot::SceneDepth, 1, &NullSRV);
	DC->PSSetShaderResources(ESystemTexSlot::SceneColor, 1, &NullSRV);
	DC->PSSetShaderResources(ESystemTexSlot::DepthOfFieldCoC, 1, &NullSRV);
	DC->PSSetShaderResources(ESystemTexSlot::DepthOfFieldBlur, 1, &NullSRV);
}
