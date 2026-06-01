#include "TranslucencyAfterDOFPass.h"
#include "RenderPassRegistry.h"

REGISTER_RENDER_PASS(FTranslucencyAfterDOFPass)

FTranslucencyAfterDOFPass::FTranslucencyAfterDOFPass()
{
	PassType = ERenderPass::TranslucencyAfterDOF;
	RenderState = { EDepthStencilState::DepthReadOnly, EBlendState::AlphaBlend,
	                ERasterizerState::SolidBackCull, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, true };
}
