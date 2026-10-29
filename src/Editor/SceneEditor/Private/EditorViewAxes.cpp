#include "EditorViewAxes.h"
#include "GlobalShader.h"
#include "ShaderParameter.h"
#include "RenderGraphBuilder.h"
#include "RHIPipelineStateCache.h"
#include <algorithm>
#include <array>
#include <stdexcept>

namespace Editor::ViewAxes
{
    using namespace RenderCore;
    using namespace RHI;

    namespace
    {
        constexpr float AxisScale = 0.62f;
        constexpr float BallRadius = 0.22f;
        constexpr int32_t Longitudes = 16;
        constexpr int32_t Latitudes = 12;
        const std::array<Core::Float3, 6> Directions = {{
            {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1}
        }};

        float ViewDepth(const Core::Float4x4& view, int endpoint)
        {
            const auto& axis = Directions[endpoint];
            return view(2,0)*axis.X + view(2,1)*axis.Y + view(2,2)*axis.Z;
        }

        std::array<int, 6> GetDrawOrder(const Core::Float4x4& view)
        {
            std::array<int, 6> order = {0,1,2,3,4,5};
            std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return ViewDepth(view,a) < ViewDepth(view,b); });
            return order;
        }
    }

    BEGIN_SHADER_PARAMETER_STRUCT(EditorViewAxesVertexParameters)
        SHADER_PARAMETER(Core::Float4x4, ViewRotation)
        SHADER_PARAMETER(Core::Float4, Axis)
        SHADER_PARAMETER(Core::Float4, AxisColor)
    END_SHADER_PARAMETER_STRUCT(EditorViewAxesVertexParameters)

    BEGIN_SHADER_PARAMETER_STRUCT(EditorViewAxesPassParameters)
        SHADER_PARAMETER_RENDER_TARGET_BINDING_SLOTS(Targets)
    END_SHADER_PARAMETER_STRUCT(EditorViewAxesPassParameters)

    class EditorViewAxesVS : public GlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER_TYPE(EditorViewAxesVS)
        static void ModifyShaderCompilerEnvironment(const ShaderPermutationParameters&, ShaderCompilerEnvironment& environment)
        {
            environment.SetDefine("VERTEX_SHADER", 1);
            environment.SetDefine("AXIS_SCALE", std::to_string(AxisScale));
            environment.SetDefine("AXIS_LONGITUDES", Longitudes);
            environment.SetDefine("AXIS_LATITUDES", Latitudes);
        }
        static const ShaderParametersMetadata* GetShaderParameterMetadata()
        { return EditorViewAxesVertexParameters::GetMetaData(); }
    };

    class EditorViewAxesPS : public GlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER_TYPE(EditorViewAxesPS)
        static void ModifyShaderCompilerEnvironment(const ShaderPermutationParameters&, ShaderCompilerEnvironment&) {}
        static const ShaderParametersMetadata* GetShaderParameterMetadata()
        { return EditorViewAxesPassParameters::GetMetaData(); }
    };

    IMPLEMENT_GLOBAL_SHADER_TYPE(EditorViewAxesVS, "EditorViewAxesVS", "/editor/ViewAxes.sf", "MainVS", ERHIShaderFrequency::Vertex);
    IMPLEMENT_GLOBAL_SHADER_TYPE(EditorViewAxesPS, "EditorViewAxesPS", "/editor/ViewAxes.sf", "MainPS", ERHIShaderFrequency::Fragment);

    Engine::ViewRect GetRect(int width, int height)
    {
        const int size = std::min(144, std::min(width,height)-24);
        if (size < 48) return {0,0,0,0};
        return {width-size-12, 12, size, size};
    }

    int HitTest(const Core::Float4x4& view, int width, int height, const Core::Float2& position)
    {
        const auto rect = GetRect(width,height);
        if (!rect.Width) return -1;
        const float scale = rect.Width*0.5f*AxisScale;
        const float radius = scale*BallRadius;
        const auto order = GetDrawOrder(view);
        // Match the painter order: the nearest visible ball wins an overlap.
        for (auto it=order.rbegin(); it!=order.rend(); ++it)
        {
            const auto& axis = Directions[*it];
            const float x = rect.X + rect.Width*0.5f + scale*(view(0,0)*axis.X + view(0,1)*axis.Y + view(0,2)*axis.Z);
            const float y = rect.Y + rect.Height*0.5f - scale*(view(1,0)*axis.X + view(1,1)*axis.Y + view(1,2)*axis.Z);
            const float dx = position.X-x, dy = position.Y-y;
            if (dx*dx+dy*dy <= radius*radius) return *it;
        }
        return -1;
    }

    void AddPass(RenderGraphBuilder& builder, const Engine::SceneView& view, RenderGraphTextureRef sceneColor)
    {
        auto rect = GetRect(view.Viewport.Width,view.Viewport.Height);
        if (!rect.Width) return;
        rect.X += view.Viewport.X; rect.Y += view.Viewport.Y;
        auto* vs = GShaderMap.GetShader<EditorViewAxesVS>(0);
        auto* ps = GShaderMap.GetShader<EditorViewAxesPS>(0);
        if (!vs || !ps) throw std::runtime_error("Editor orientation axes shaders are unavailable");

        RHIGraphicsPipelineStateDesc desc;
        desc.ShaderStages.VertexShader = static_cast<RHIVertexShader*>(vs->GetRHIShader());
        desc.ShaderStages.FragmentShader = static_cast<RHIFragmentShader*>(ps->GetRHIShader());
        desc.VertexDescState = RHIPipelineStateCache::GetOrCreateVertexDescState({});
        RHIRasterizerStateDesc rasterizer;
        rasterizer.CullMode = ERHICullMode::None;
        desc.RasterizerState = RHIPipelineStateCache::GetOrCreateRasterizerState(rasterizer);
        desc.DepthStencilState = RHIPipelineStateCache::GetOrCreateDepthStencilState({});
        RHIColorBlendStateDesc blend;
        blend.Attachments.resize(1);
        desc.ColorBlendState = RHIPipelineStateCache::GetOrCreateColorBlendState(blend);
        desc.AttachmentDesc.ColorAttachmentCount = 1;
        desc.AttachmentDesc.ColorAttachments[0].Format = sceneColor->GetDesc().Format;
        desc.AttachmentDesc.ColorAttachments[0].Actions = ERenderTargetActions::Load_Store;
        desc.PrimitiveTopology = EPrimitiveTopology::LineList;
        auto* linePipeline = RHIPipelineStateCache::GetOrCreateGraphicsPipelineState(desc);
        desc.PrimitiveTopology = EPrimitiveTopology::TriangleList;
        rasterizer.CullMode = ERHICullMode::Back;
        rasterizer.FrontFace = ERHIFrontFace::CounterClockwise;
        desc.RasterizerState = RHIPipelineStateCache::GetOrCreateRasterizerState(rasterizer);
        auto* ballPipeline = RHIPipelineStateCache::GetOrCreateGraphicsPipelineState(desc);

        const std::array<Core::Float4, 3> colors = {{{1,0.12f,0.12f,1}, {0.12f,0.35f,1,1}, {0.12f,0.9f,0.2f,1}}};
        std::array<EditorViewAxesVertexParameters*, 6> lines, balls;
        for (int i=0; i<6; ++i)
        {
            auto color = colors[i/2];
            if (i%2) for (int c=0; c<3; ++c) color[c] = color[c]*0.45f+0.55f;
            const auto& axis = Directions[i];
            lines[i] = builder.AllocateParameter<EditorViewAxesVertexParameters>();
            lines[i]->ViewRotation = view.ViewMatrix;
            lines[i]->Axis = {axis.X,axis.Y,axis.Z,0};
            lines[i]->AxisColor = color;
            balls[i] = builder.AllocateParameter<EditorViewAxesVertexParameters>();
            *balls[i] = *lines[i];
            balls[i]->Axis.W = BallRadius;
        }
        const auto order = GetDrawOrder(view.ViewMatrix);
        auto* params = builder.AllocateParameter<EditorViewAxesPassParameters>();
        params->Targets.NumColorRenderTargets = 1;
        params->Targets[0].Texture = sceneColor;
        params->Targets[0].Action = ERenderTargetActions::Load_Store;
        // This is a corner overlay: it neither tests against nor modifies scene depth.
        builder.AddPass<EditorViewAxesPassParameters>("EditorViewAxes", EditorViewAxesPassParameters::GetMetaData(), params, EPassFlag::Graphic,
            [=](RHICommandListBase& commands) {
                auto& cmd = static_cast<RHIGraphicCommandList&>(commands);
                RHIRenderPassInfo info;
                info.RenderTargets = params->Targets.GetBoundRenderTarget();
                info.RenderArea = {rect.X,rect.Y,static_cast<uint32_t>(rect.Width),static_cast<uint32_t>(rect.Height)};
                cmd.BeginRenderPass(info);
                cmd.SetGraphicPipelineState(linePipeline);
                cmd.SetViewport(static_cast<float>(rect.X),static_cast<float>(rect.Y),
                    static_cast<float>(rect.Width),static_cast<float>(rect.Height),0,1);
                cmd.SetScissor(rect.X,rect.Y,rect.Width,rect.Height);
                for (int i : order) { SetShaderParameters(cmd,vs,lines[i]); cmd.Draw(2); }
                cmd.SetGraphicPipelineState(ballPipeline);
                for (int i : order) { SetShaderParameters(cmd,vs,balls[i]); cmd.Draw(Longitudes*Latitudes*6); }
                cmd.EndRenderPass();
            });
    }
}
