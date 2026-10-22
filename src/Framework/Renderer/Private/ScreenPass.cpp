#include "ScreenPass.h"
#include "RHIPipelineStateCache.h"
#include "GlobalShader.h"
namespace Renderer {
	//BEGIN_SHADER_PARAMETER_STRUCT(ScenePassShaderParameter)
	//	SHADER_PARAMETER_TEXTURE(InText)
	//	SHADER_PARAMETER_RENDER_TARGETS()
	//END_SHADER_PARAMETER_STRUCT(ScenePassShaderParameter);

	void AddScreenPass(RenderGraphBuilder& builder, RenderGraphTexture* inTexture, RenderGraphTexture* outTexture) {
		auto& gShaderMap = RenderCore::GShaderMap;
		auto drawTextureShaderType = ShaderType::GetRegisterMap()[ShaderType::EShaderTypeFlag::Global]["DrawTexturePS"];
        auto fullScreenQuadShaderType = ShaderType::GetRegisterMap()[ShaderType::EShaderTypeFlag::Global]["FullScreenVS"];
		
		auto drawTexture = gShaderMap.GetShader(drawTextureShaderType,0);
		auto fullScreenQuad = gShaderMap.GetShader(fullScreenQuadShaderType, 0);
		// Build the default screen-pass pipeline state.
		RHIGraphicsPipelineStateDesc pipelineStateDesc;
		pipelineStateDesc.ShaderStages.VertexShader = dynamic_cast<RHI::RHIVertexShader*>(drawTexture->GetRHIShader());
        pipelineStateDesc.ShaderStages.FragmentShader = dynamic_cast<RHI::RHIFragmentShader*>(fullScreenQuad->GetRHIShader());
		auto graphicStates = RHIPipelineStateCache::GetOrCreateGraphicsPipelineState(pipelineStateDesc);
		//auto parameter = builder.AllocateParameter<ScenePassShaderParameter>();
		//parameter->InText.Value = inTexture;
		//parameter->RenderTargets.ColorTargets[0].Texture = outTexture;
		//RenderGraphPassInfo info;
		//info.ShaderParmeters = (const ScenePassShaderParameter*) & parameter;
		//info.PassFlag = EPassFlag::Graphic;
		//
		//builder.AddPass("ScreenPass", info, [graphicStates,parameter, info, drawTextureShaderType, //fullScreenQuadShaderType, drawTexture](RHI::RHIGraphicCommandList& cmdList) {
		//	cmdList.SetGraphicPipelineState(graphicStates.get());
		//	info.ShaderParmeters.SetShaderParameters(cmdList, drawTexture, drawTextureShaderType-//>ParameterBindingInfo);
		//	cmdList.SetStreamSource(0, nullptr, 0);
		//	cmdList.Draw(3, 1, 0, 0);
		//	});
		//
	}

}