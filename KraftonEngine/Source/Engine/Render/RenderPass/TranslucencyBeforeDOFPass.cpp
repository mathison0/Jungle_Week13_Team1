#include "TranslucencyBeforeDOFPass.h"
#include "RenderPassRegistry.h"

REGISTER_RENDER_PASS(FTranslucencyBeforeDOFPass)

FTranslucencyBeforeDOFPass::FTranslucencyBeforeDOFPass()
{
	PassType = ERenderPass::TranslucencyBeforeDOF;
	RenderState = { EDepthStencilState::DepthReadOnly, EBlendState::AlphaBlend,
	                ERasterizerState::SolidBackCull, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, true };
}
