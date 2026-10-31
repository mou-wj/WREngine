#include "HintRender.h"
#include "GlobalShader.h"
#include "ShaderCompiler.h"
#include "ShaderParameter.h"
#include "RenderGraphBuilder.h"
#include "RHIPipelineStateCache.h"
#include "SceneView.h"
#include "LightComponent.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>
#include <stdexcept>

namespace Renderer::HintRender
{
    using namespace RenderCore;
    using namespace RHI;

    BEGIN_SHADER_PARAMETER_STRUCT(HintLineVertexParameters)
        SHADER_PARAMETER(Core::Float4x4, ViewProjection)
        SHADER_PARAMETER(Core::Float4, Point0)
        SHADER_PARAMETER(Core::Float4, Point1)
        SHADER_PARAMETER(Core::Float4, LineColor)
    END_SHADER_PARAMETER_STRUCT(HintLineVertexParameters)
    BEGIN_SHADER_PARAMETER_STRUCT(HintLinePassParameters)
        SHADER_PARAMETER_RENDER_TARGET_BINDING_SLOTS(Targets)
    END_SHADER_PARAMETER_STRUCT(HintLinePassParameters)
    BEGIN_SHADER_PARAMETER_STRUCT(HintGridParameters)
        SHADER_PARAMETER(Core::Float4x4, ViewProjection)
        SHADER_PARAMETER(Core::Float4, GridBounds)
        SHADER_PARAMETER(Core::Float4, GridSettings)
        SHADER_PARAMETER(Core::UInt3, LineCounts)
        SHADER_PARAMETER_RENDER_TARGET_BINDING_SLOTS(Targets)
    END_SHADER_PARAMETER_STRUCT(HintGridParameters)

    static void RegisterHintShaderHeaders()
    {
        // Includes are expanded before stage/variant #if directives are evaluated.
        ShaderCompiler::GenerateOrGetShaderPrameterMetaDataSF(*HintLineVertexParameters::GetMetaData());
        ShaderCompiler::GenerateOrGetShaderPrameterMetaDataSF(*HintGridParameters::GetMetaData());
    }

    class HintLineVS : public GlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER_TYPE(HintLineVS)
        static void ModifyShaderCompilerEnvironment(const ShaderPermutationParameters&, ShaderCompilerEnvironment& environment)
        { RegisterHintShaderHeaders(); environment.SetDefine("VERTEX_SHADER",1); }
        static const ShaderParametersMetadata* GetShaderParameterMetadata() { return HintLineVertexParameters::GetMetaData(); }
    };
    class HintLinePS : public GlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER_TYPE(HintLinePS)
        static void ModifyShaderCompilerEnvironment(const ShaderPermutationParameters&, ShaderCompilerEnvironment&)
        {
            RegisterHintShaderHeaders();
        }
        static const ShaderParametersMetadata* GetShaderParameterMetadata() { return HintLinePassParameters::GetMetaData(); }
    };
    IMPLEMENT_GLOBAL_SHADER_TYPE(HintLineVS,"HintLineVS","/HintRender.sf","MainVS",ERHIShaderFrequency::Vertex);
    IMPLEMENT_GLOBAL_SHADER_TYPE(HintLinePS,"HintLinePS","/HintRender.sf","MainPS",ERHIShaderFrequency::Fragment);

    class HintGridVS : public GlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER_TYPE(HintGridVS)
        static void ModifyShaderCompilerEnvironment(const ShaderPermutationParameters&, ShaderCompilerEnvironment& environment)
        { RegisterHintShaderHeaders(); environment.SetDefine("GRID_SHADER",1); environment.SetDefine("VERTEX_SHADER",1); }
        static const ShaderParametersMetadata* GetShaderParameterMetadata() { return HintGridParameters::GetMetaData(); }
    };
    class HintGridPS : public GlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER_TYPE(HintGridPS)
        static void ModifyShaderCompilerEnvironment(const ShaderPermutationParameters&, ShaderCompilerEnvironment& environment)
        { RegisterHintShaderHeaders(); environment.SetDefine("GRID_SHADER",1); }
        static const ShaderParametersMetadata* GetShaderParameterMetadata() { return HintGridParameters::GetMetaData(); }
    };
    IMPLEMENT_GLOBAL_SHADER_TYPE(HintGridVS,"HintGridVS","/HintRender.sf","MainGridVS",ERHIShaderFrequency::Vertex);
    IMPLEMENT_GLOBAL_SHADER_TYPE(HintGridPS,"HintGridPS","/HintRender.sf","MainGridPS",ERHIShaderFrequency::Fragment);

    namespace
    {
        constexpr int Segments = 64;
        constexpr float TwoPi = 6.28318530718f;
        struct Line { Core::Float3 A, B; Core::Float4 Color; };
        bool IsFinite(const Core::Float3& p)
        { return std::isfinite(p.X) && std::isfinite(p.Y) && std::isfinite(p.Z); }

        bool GetGroundBounds(const Core::Float4x4& inverseViewProjection, bool zeroToOne, Core::AABB& bounds)
        {
            bounds.SetEmpty();
            const Core::Float2 corners[] = {{-1,-1},{1,-1},{1,1},{-1,1}};
            std::array<Core::Float3,8> points;
            for (int layer=0; layer<2; ++layer)
                for (int corner=0; corner<4; ++corner)
                {
                    const float z = layer ? 1.0f : (zeroToOne ? 0.0f : -1.0f);
                    const auto h = inverseViewProjection*std::array<float,4>{corners[corner].X,corners[corner].Y,z,1};
                    if (!std::isfinite(h[3]) || std::abs(h[3])<1e-7f) return false;
                    const Core::Float3 p{h[0]/h[3],h[1]/h[3],h[2]/h[3]};
                    if (!IsFinite(p)) return false;
                    points[layer*4+corner] = p;
                    if (std::abs(p.Y)<=1e-5f) bounds.ExpandBy({p.X,0,p.Z});
                }
            // The plane section can have more than four vertices (e.g. a rolled
            // camera). Test all 12 frustum edges, not only the four corner rays.
            constexpr int edges[][2] = {{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
            for (const auto& edge : edges)
            {
                const auto a = points[edge[0]], b = points[edge[1]];
                if ((a.Y<0 && b.Y>0) || (a.Y>0 && b.Y<0))
                {
                    const auto p = a+(b-a)*(-a.Y/(b.Y-a.Y));
                    bounds.ExpandBy({p.X,0,p.Z});
                }
            }
            return !bounds.IsEmpty() && bounds.Max.X-bounds.Min.X>1e-5f && bounds.Max.Z-bounds.Min.Z>1e-5f;
        }

        void Circle(std::vector<Line>& lines, Core::Float3 center, Core::Float3 u, Core::Float3 v,
            float radius, Core::Float4 color)
        {
            for (int i=0; i<Segments; ++i)
            {
                const float a = TwoPi*i/Segments, b = TwoPi*(i+1)/Segments;
                lines.push_back({center+(u*std::cos(a)+v*std::sin(a))*radius,
                    center+(u*std::cos(b)+v*std::sin(b))*radius,color});
            }
        }

        RHIGraphicsPipelineState* GetHintPipeline(Shader* vs, Shader* ps, RenderGraphTextureRef color,
            RenderGraphTextureRef depth, EPrimitiveTopology topology)
        {
            if (!vs || !ps) throw std::runtime_error("HintRender shaders are unavailable");
            RHIGraphicsPipelineStateDesc desc;
            desc.ShaderStages.VertexShader = static_cast<RHIVertexShader*>(vs->GetRHIShader());
            desc.ShaderStages.FragmentShader = static_cast<RHIFragmentShader*>(ps->GetRHIShader());
            desc.VertexDescState = RHIPipelineStateCache::GetOrCreateVertexDescState({});
            RHIRasterizerStateDesc rasterizer; rasterizer.CullMode = ERHICullMode::None;
            desc.RasterizerState = RHIPipelineStateCache::GetOrCreateRasterizerState(rasterizer);
            RHIDepthStencilStateDesc depthState;
            depthState.bDepthTestEnable = true; depthState.bDepthWriteEnable = false;
            depthState.DepthCompareOp = ERHICompareOp::LessOrEqual;
            desc.DepthStencilState = RHIPipelineStateCache::GetOrCreateDepthStencilState(depthState);
            RHIColorBlendStateDesc blend; blend.Attachments.resize(1); blend.Attachments[0].bBlendEnable = true;
            desc.ColorBlendState = RHIPipelineStateCache::GetOrCreateColorBlendState(blend);
            desc.AttachmentDesc.ColorAttachmentCount = 1;
            desc.AttachmentDesc.ColorAttachments[0].Format = color->GetDesc().Format;
            desc.AttachmentDesc.ColorAttachments[0].Actions = ERenderTargetActions::Load_Store;
            desc.AttachmentDesc.bEnableDepth = true;
            desc.AttachmentDesc.DepthStencilFormat = depth->GetDesc().Format;
            desc.PrimitiveTopology = topology;
            return RHIPipelineStateCache::GetOrCreateGraphicsPipelineState(desc);
        }

        void AddLines(RenderGraphBuilder& builder, const Engine::SceneView& view, RenderGraphTextureRef color,
            RenderGraphTextureRef depth, const std::vector<Line>& lines, const char* name)
        {
            if (lines.empty() || !color || !depth || view.Viewport.Width<=0 || view.Viewport.Height<=0) return;
            auto* vs = GShaderMap.GetShader<HintLineVS>(0);
            auto* ps = GShaderMap.GetShader<HintLinePS>(0);
            auto* pipeline = GetHintPipeline(vs,ps,color,depth,EPrimitiveTopology::LineList);

            // Snapshot component/camera data before deferred recording; arena
            // destructs the pointer vector after this frame's recording completes.
            auto* parameters = builder.AllocateParameter<std::vector<HintLineVertexParameters*>>();
            parameters->reserve(lines.size());
            for (const auto& line : lines)
            {
                auto* data = builder.AllocateParameter<HintLineVertexParameters>();
                data->ViewProjection = view.ViewProjectionMatrix;
                data->Point0 = {line.A.X,line.A.Y,line.A.Z,1};
                data->Point1 = {line.B.X,line.B.Y,line.B.Z,1};
                data->LineColor = line.Color;
                parameters->push_back(data);
            }
            auto* pass = builder.AllocateParameter<HintLinePassParameters>();
            pass->Targets.NumColorRenderTargets = 1;
            pass->Targets[0].Texture = color; pass->Targets[0].Action = ERenderTargetActions::Load_Store;
            pass->Targets.DepthStencil.Texture = depth;
            pass->Targets.DepthStencil.DepthAction = ERenderTargetActions::Load_Store;
            // RHI render passes bind depth in attachment layout. Conservatively
            // declare that access; the pipeline above never writes depth values.
            const auto rect = view.Viewport;
            builder.AddPass<HintLinePassParameters>(name,HintLinePassParameters::GetMetaData(),pass,EPassFlag::Graphic,
                [=](RHICommandListBase& commands) {
                    auto& cmd = static_cast<RHIGraphicCommandList&>(commands);
                    RHIRenderPassInfo info; info.RenderTargets = pass->Targets.GetBoundRenderTarget();
                    info.RenderArea = {rect.X,rect.Y,static_cast<uint32_t>(rect.Width),static_cast<uint32_t>(rect.Height)};
                    cmd.BeginRenderPass(info); cmd.SetGraphicPipelineState(pipeline);
                    cmd.SetViewport(static_cast<float>(rect.X),static_cast<float>(rect.Y),static_cast<float>(rect.Width),static_cast<float>(rect.Height),0,1);
                    cmd.SetScissor(rect.X,rect.Y,rect.Width,rect.Height);
                    for (auto* data : *parameters) { SetShaderParameters(cmd,vs,data); cmd.Draw(2); }
                    cmd.EndRenderPass();
                });
        }
    }

    float GetGridSpacing(float cameraHeight)
    {
        if (!std::isfinite(cameraHeight)) return 0.1f;
        const float desired = std::clamp(std::abs(cameraHeight)*0.1f,0.1f,10.0f);
        for (float step : {0.1f,0.2f,0.5f,1.0f,2.0f,5.0f,10.0f})
            if (step >= desired) return step;
        return 10.0f;
    }

    void AddGridPass(RenderGraphBuilder& builder, const Engine::SceneView& view, RenderGraphTextureRef color, RenderGraphTextureRef depth)
    {
        if (!IsFinite(view.CameraWorldPos) || !color || !depth || view.Viewport.Width<=0 || view.Viewport.Height<=0) return;
        // Every grid level divides 10 metres. Removing whole 10m periods keeps
        // the pattern world-anchored while avoiding large-coordinate grid noise.
        auto relativeEye = view.CameraWorldPos;
        relativeEye.X -= std::floor(relativeEye.X/10)*10;
        relativeEye.Z -= std::floor(relativeEye.Z/10)*10;
        auto relativeView = view.ViewMatrix;
        for (int row=0; row<3; ++row)
            relativeView(row,3) = -(relativeView(row,0)*relativeEye.X+relativeView(row,1)*relativeEye.Y+relativeView(row,2)*relativeEye.Z);
        const auto viewProjection = view.ProjectionMatrix*relativeView;
        Core::AABB bounds;
        if (!GetGroundBounds(Core::Inverse(viewProjection),view.bDepthRangeZeroToOne,bounds)) return;
        const float step = GetGridSpacing(view.CameraWorldPos.Y);
        // Expand to whole cells outside the tight visible section; boundaries
        // stay beyond the viewport, and panning never moves the world grid phase.
        const float minX = (std::floor(bounds.Min.X/step)-1)*step;
        const float minZ = (std::floor(bounds.Min.Z/step)-1)*step;
        const double countX = std::ceil(bounds.Max.X/step)-std::floor(bounds.Min.X/step)+3;
        const double countZ = std::ceil(bounds.Max.Z/step)-std::floor(bounds.Min.Z/step)+3;
        if ((countX+countZ)*2>std::numeric_limits<uint32_t>::max())
            throw std::runtime_error("Visible grid subdivision exceeds the RHI draw vertex count");
        const auto linesX = static_cast<uint32_t>(countX), linesZ = static_cast<uint32_t>(countZ);
        auto* vs = GShaderMap.GetShader<HintGridVS>(0);
        auto* ps = GShaderMap.GetShader<HintGridPS>(0);
        auto* pipeline = GetHintPipeline(vs,ps,color,depth,EPrimitiveTopology::LineList);
        auto* pass = builder.AllocateParameter<HintGridParameters>();
        pass->ViewProjection = viewProjection;
        pass->GridBounds = {minX,minZ,(linesX-1)*step,(linesZ-1)*step};
        pass->GridSettings = {step,static_cast<float>(view.Viewport.Width),static_cast<float>(view.Viewport.Height),0};
        pass->LineCounts = {linesX,linesZ,0};
        pass->Targets.NumColorRenderTargets = 1;
        pass->Targets[0].Texture = color; pass->Targets[0].Action = ERenderTargetActions::Load_Store;
        pass->Targets.DepthStencil.Texture = depth; pass->Targets.DepthStencil.DepthAction = ERenderTargetActions::Load_Store;
        const auto rect = view.Viewport;
        // CPU determines subdivisions of a unit XZ grid; VS maps its unit corners
        // into the visible AABB. Emit each shared edge once, O(nx+nz), not cells.
        builder.AddPass<HintGridParameters>("HintRender.XZGrid",HintGridParameters::GetMetaData(),pass,EPassFlag::Graphic,
            [=](RHICommandListBase& commands) {
                auto& cmd = static_cast<RHIGraphicCommandList&>(commands);
                RHIRenderPassInfo info; info.RenderTargets = pass->Targets.GetBoundRenderTarget();
                info.RenderArea = {rect.X,rect.Y,static_cast<uint32_t>(rect.Width),static_cast<uint32_t>(rect.Height)};
                cmd.BeginRenderPass(info); cmd.SetGraphicPipelineState(pipeline);
                cmd.SetViewport(static_cast<float>(rect.X),static_cast<float>(rect.Y),static_cast<float>(rect.Width),static_cast<float>(rect.Height),0,1);
                cmd.SetScissor(rect.X,rect.Y,rect.Width,rect.Height);
                SetShaderParameters(cmd,vs,pass); SetShaderParameters(cmd,ps,pass);
                cmd.Draw(2*(linesX+linesZ)); cmd.EndRenderPass();
            });
    }

    void AddLightPass(RenderGraphBuilder& builder, const Engine::SceneView& view, RenderGraphTextureRef color,
        RenderGraphTextureRef depth, const Engine::LightComponent& light)
    {
        const auto origin = light.GetWorldLocation();
        if (!IsFinite(origin)) return;
        std::vector<Line> lines;
        if (const auto* point = dynamic_cast<const Engine::PointLightComponent*>(&light))
        {
            const float radius = point->GetAttenuationRadius();
            if (!std::isfinite(radius) || radius<=0) return;
            const Core::Float4 blue{0.1f,0.35f,1,1};
            Circle(lines,origin,{1,0,0},{0,1,0},radius,blue);
            Circle(lines,origin,{1,0,0},{0,0,1},radius,blue);
            Circle(lines,origin,{0,1,0},{0,0,1},radius,blue);
        }
        else
        {
            const auto* spot = dynamic_cast<const Engine::SpotLightComponent*>(&light);
            const auto* directional = dynamic_cast<const Engine::DirectionalLightComponent*>(&light);
            if (!spot && !directional) return; // Sky lights have no finite volume.
            auto direction = spot ? spot->GetDirection() : directional->GetDirection();
            if (!IsFinite(direction) || direction.LengthSquared()<1e-8f) return;
            direction = Core::Normalize(direction);
            const auto u = Core::Normalize(Core::Cross(direction,std::abs(direction.Y)<0.9f ? Core::Float3{0,1,0} : Core::Float3{1,0,0}));
            const auto v = Core::Cross(direction,u);
            const Core::Float4 yellow{1,0.75f,0.1f,1};
            if (spot)
            {
                const float distance = spot->GetAttenuationRadius(), angle = spot->GetOuterConeAngle();
                if (!std::isfinite(distance) || distance<=0 || !std::isfinite(angle) || angle<=0 || angle>=TwoPi*0.25f) return;
                const auto center = origin+direction*distance;
                const float radius = distance*std::tan(angle);
                Circle(lines,center,u,v,radius,yellow);
                for (int i=0; i<16; ++i)
                {
                    const float a = TwoPi*i/16;
                    lines.push_back({origin,center+(u*std::cos(a)+v*std::sin(a))*radius,yellow});
                }
                lines.push_back({origin,center,yellow});
            }
            else
            {
                const auto end = origin+direction*1.5f;
                lines.push_back({origin,end,yellow});
                for (int i=0; i<8; ++i)
                {
                    const float a = TwoPi*i/8;
                    lines.push_back({end,end-direction*0.3f+(u*std::cos(a)+v*std::sin(a))*0.15f,yellow});
                }
            }
        }
        AddLines(builder,view,color,depth,lines,"HintRender.Light");
    }
}
