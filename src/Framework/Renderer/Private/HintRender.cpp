#include "HintRender.h"
#include "GlobalShader.h"
#include "ShaderCompiler.h"
#include "ShaderParameter.h"
#include "RenderGraphBuilder.h"
#include "RHIPipelineStateCache.h"
#include "SceneView.h"
#include "LightComponent.h"
#include "LightSceneProxy.h"
#include "BVH.hpp"
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
        SHADER_PARAMETER(Core::Float4, CameraPositionAndFade)
        SHADER_PARAMETER(Core::UInt3, PatchSettings)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, GridVertices)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, PatchIndices)
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
        constexpr uint32_t GridVertexCount = 100;
        constexpr uint32_t GridCellCount = GridVertexCount-1;
        constexpr std::array<float,7> GridSpacings{0.1f,0.2f,0.5f,1,2,5,10};
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

        bool IsGridPatchVisible(const Core::Float4x4& vp, bool zeroToOne, float x, float z, float size)
        {
            // Reject only when all four corners lie outside the SAME clip plane.
            // This remains conservative for patches intersecting the near plane.
            uint32_t outside = 63;
            for (const auto corner : {Core::Float2{0,0},{1,0},{0,1},{1,1}})
            {
                const auto h = vp*std::array<float,4>{x+corner.X*size,0,z+corner.Y*size,1};
                uint32_t code = 0;
                if (h[0]<-h[3]) code |= 1; if (h[0]>h[3]) code |= 2;
                if (h[1]<-h[3]) code |= 4; if (h[1]>h[3]) code |= 8;
                if (h[2]<(zeroToOne ? 0 : -h[3])) code |= 16;
                if (h[2]>h[3]) code |= 32;
                outside &= code;
            }
            return outside == 0;
        }

        template<typename Visitor>
        void ForEachDirectionalLine(Core::Float3 origin, Core::Float3 direction, Visitor&& visit)
        {
            if (!IsFinite(origin) || !IsFinite(direction) || direction.LengthSquared()<1e-8f) return;
            direction=Core::Normalize(direction);
            const auto u=Core::Normalize(Core::Cross(direction,std::abs(direction.Y)<0.9f ? Core::Float3{0,1,0} : Core::Float3{1,0,0}));
            const auto v=Core::Cross(direction,u);
            const auto end=origin+direction*1.5f;
            visit(origin,end);
            for (int i=0; i<8; ++i)
            {
                const float a=TwoPi*i/8;
                visit(end,end-direction*0.3f+(u*std::cos(a)+v*std::sin(a))*0.15f);
            }
        }

        template<typename Visitor>
        void QuadraticRoots(double a, double b, double c, Visitor&& visit)
        {
            // Coefficients use a unit ray. Handle a generator-parallel cone ray
            // as a linear equation, and avoid cancellation in the quadratic roots.
            if (std::abs(a)<=1e-12)
            {
                if (std::abs(b)>1e-12) visit(-c/b);
                else if (std::abs(c)<=1e-12) visit(0.0);
                return;
            }
            double discriminant=b*b-4*a*c;
            if (discriminant < -1e-12*(b*b+std::abs(4*a*c))) return;
            discriminant=std::sqrt(std::max(0.0,discriminant));
            const double q=-0.5*(b+std::copysign(discriminant,b));
            if (q==0) visit(-b/(2*a));
            else { visit(q/a); visit(c/q); }
        }

        Core::Double3 ToDouble(const Core::Float3& p) { return {p.X,p.Y,p.Z}; }

        bool RayNearSegment(const Core::Ray& ray, double maximum, Core::Float3 a, Core::Float3 b, double& outDistance)
        {
            const auto d=ToDouble(ray.Direction), edge=ToDouble(b)-ToDouble(a), w=ToDouble(ray.Origin)-ToDouble(a);
            const double dd=Core::Dot(d,d), ee=Core::Dot(edge,edge), de=Core::Dot(d,edge);
            const double dw=Core::Dot(d,w), ew=Core::Dot(edge,w);
            double best=std::numeric_limits<double>::max();
            const auto test=[&](double t, double u) {
                const double distance=(w+d*t-edge*u).LengthSquared();
                if (distance<best || (distance==best && t<outDistance)) { best=distance;outDistance=t; }
            };
            if (ee<=1e-20) test(std::clamp(-dw/dd,0.0,maximum),0);
            else
            {
                // Interior stationary point plus the four boundary minima of
                // [0,maximum] x [0,1]; also handles parallel/degenerate segments.
                test(0,std::clamp(ew/ee,0.0,1.0));
                test(maximum,std::clamp((ew+de*maximum)/ee,0.0,1.0));
                test(std::clamp(-dw/dd,0.0,maximum),0);
                test(std::clamp((de-dw)/dd,0.0,maximum),1);
                const double determinant=dd*ee-de*de;
                if (determinant>1e-12*dd*ee)
                {
                    const double t=(de*ew-ee*dw)/determinant, u=(dd*ew-de*dw)/determinant;
                    if (t>=0 && t<=maximum && u>=0 && u<=1) test(t,u);
                }
            }
            return best<=0.05*0.05;
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
            RHIColorBlendStateDesc blend; blend.Attachments.resize(1);
            blend.Attachments[0].bBlendEnable = true;
            // RGB = src.rgb*src.a + dst.rgb*(1-src.a); preserve opaque scene A.
            blend.Attachments[0].ColorWriteMask = 0x7;
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
        for (float step : GridSpacings)
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
        // 100x100 control vertices form 99x99 regions. Square cells and their
        // origins are whole 10m periods, so every LOD has the same world phase.
        const float minX = (std::floor(bounds.Min.X/10)-1)*10;
        const float minZ = (std::floor(bounds.Min.Z/10)-1)*10;
        const float extentX = (std::ceil(bounds.Max.X/10)+1)*10-minX;
        const float extentZ = (std::ceil(bounds.Max.Z/10)+1)*10-minZ;
        const float cellSize = std::ceil(std::max(extentX,extentZ)/(GridCellCount*10))*10;
        if (!std::isfinite(cellSize) || cellSize<=0) return;
        std::array<std::vector<uint32_t>,GridSpacings.size()> groups;
        const float height = std::abs(relativeEye.Y);
        for (uint32_t z=0; z<GridCellCount; ++z)
            for (uint32_t x=0; x<GridCellCount; ++x)
            {
                const float cellX = minX+x*cellSize, cellZ = minZ+z*cellSize;
                if (!IsGridPatchVisible(viewProjection,view.bDepthRangeZeroToOne,cellX,cellZ,cellSize)) continue;
                const float dx = relativeEye.X-std::clamp(relativeEye.X,cellX,cellX+cellSize);
                const float dz = relativeEye.Z-std::clamp(relativeEye.Z,cellZ,cellZ+cellSize);
                const float distance = std::sqrt(dx*dx+height*height+dz*dz);
                // Near detail still follows height; distance along the ground
                // coarsens distant regions instead of expanding fine lines there.
                const float step = GetGridSpacing(std::max(height,distance*0.2f));
                const auto level = std::find(GridSpacings.begin(),GridSpacings.end(),step)-GridSpacings.begin();
                groups[level].push_back(z*GridVertexCount+x);
            }
        std::vector<uint32_t> patchIndices;
        for (const auto& group : groups) patchIndices.insert(patchIndices.end(),group.begin(),group.end());
        if (patchIndices.empty()) return;

        static const auto vertices = [] {
            // Integer control coordinates need only 16 bits per axis (40KB total).
            std::array<uint32_t,GridVertexCount*GridVertexCount> result;
            for (uint32_t z=0; z<GridVertexCount; ++z)
                for (uint32_t x=0; x<GridVertexCount; ++x)
                    result[z*GridVertexCount+x] = x | (z<<16);
            return result;
        }();
        struct GridDraw { HintGridParameters* Parameters; uint32_t Vertices, Instances; };
        auto* draws = builder.AllocateParameter<std::vector<GridDraw>>();
        // Builder resource names are keys. Arena identity keeps different views'
        // patch lists distinct without a persistent cache or global counter.
        const auto name = "HintGrid."+std::to_string(reinterpret_cast<uintptr_t>(draws));
        RenderGraphBufferDesc bufferDesc;
        bufferDesc.Size = sizeof(vertices); bufferDesc.Stride = sizeof(uint32_t);
        bufferDesc.Usage = ERHIBufferUsageFlag::Structured | ERHIBufferUsageFlag::ShaderResource | ERHIBufferUsageFlag::TransferDst;
        auto verticesBuffer = builder.CreateBuffer(name+".Vertices",bufferDesc);
        bufferDesc.Size = patchIndices.size()*sizeof(uint32_t); bufferDesc.Stride = sizeof(uint32_t);
        auto patchesBuffer = builder.CreateBuffer(name+".Patches",bufferDesc);
        // AddUploadBuffer copies both sources into the Builder arena immediately;
        // upload/SRV intents then order and transition the transient buffers.
        builder.AddUploadBuffer(verticesBuffer,vertices.data(),sizeof(vertices));
        builder.AddUploadBuffer(patchesBuffer,patchIndices.data(),patchIndices.size()*sizeof(uint32_t));
        RenderGraphBufferSRVDesc srvDesc;
        srvDesc.Buffer = verticesBuffer; srvDesc.Stride = sizeof(uint32_t); srvDesc.NumElements = vertices.size();
        auto verticesSRV = builder.CreateBufferSRV(name+".VerticesSRV",srvDesc);
        srvDesc.Buffer = patchesBuffer; srvDesc.Stride = sizeof(uint32_t); srvDesc.NumElements = patchIndices.size();
        auto patchesSRV = builder.CreateBufferSRV(name+".PatchesSRV",srvDesc);
        uint32_t offset = 0;
        for (size_t level=0; level<groups.size(); ++level)
        {
            const auto instances = static_cast<uint32_t>(groups[level].size());
            if (!instances) continue;
            const double lineCount = std::round(static_cast<double>(cellSize)/GridSpacings[level]);
            if (lineCount*4>std::numeric_limits<uint32_t>::max())
                throw std::runtime_error("Visible grid patch subdivision exceeds the RHI draw vertex count");
            auto* data = builder.AllocateParameter<HintGridParameters>();
            data->ViewProjection = viewProjection;
            data->GridBounds = {minX,minZ,cellSize,cellSize};
            data->GridSettings = {GridSpacings[level],static_cast<float>(view.Viewport.Width),static_cast<float>(view.Viewport.Height),0};
            data->CameraPositionAndFade = {relativeEye.X,relativeEye.Y,relativeEye.Z,std::max(40.0f,height*10)};
            data->PatchSettings = {offset,static_cast<uint32_t>(lineCount),GridVertexCount};
            data->GridVertices = verticesSRV; data->PatchIndices = patchesSRV;
            data->Targets.NumColorRenderTargets = 1;
            data->Targets[0].Texture = color; data->Targets[0].Action = ERenderTargetActions::Load_Store;
            data->Targets.DepthStencil.Texture = depth; data->Targets.DepthStencil.DepthAction = ERenderTargetActions::Load_Store;
            draws->push_back({data,static_cast<uint32_t>(lineCount)*4,instances});
            offset += instances;
        }
        auto* vs = GShaderMap.GetShader<HintGridVS>(0);
        auto* ps = GShaderMap.GetShader<HintGridPS>(0);
        auto* pipeline = GetHintPipeline(vs,ps,color,depth,EPrimitiveTopology::LineList);
        auto* pass = draws->front().Parameters;
        const auto rect = view.Viewport;
        builder.AddPass<HintGridParameters>("HintRender.XZGrid",HintGridParameters::GetMetaData(),pass,EPassFlag::Graphic,
            [=](RHICommandListBase& commands) {
                auto& cmd = static_cast<RHIGraphicCommandList&>(commands);
                RHIRenderPassInfo info; info.RenderTargets = pass->Targets.GetBoundRenderTarget();
                info.RenderArea = {rect.X,rect.Y,static_cast<uint32_t>(rect.Width),static_cast<uint32_t>(rect.Height)};
                cmd.BeginRenderPass(info); cmd.SetGraphicPipelineState(pipeline);
                cmd.SetViewport(static_cast<float>(rect.X),static_cast<float>(rect.Y),static_cast<float>(rect.Width),static_cast<float>(rect.Height),0,1);
                cmd.SetScissor(rect.X,rect.Y,rect.Width,rect.Height);
                for (const auto& draw : *draws)
                {
                    SetShaderParameters(cmd,vs,draw.Parameters); SetShaderParameters(cmd,ps,draw.Parameters);
                    cmd.Draw(draw.Vertices,draw.Instances);
                }
                cmd.EndRenderPass();
            });
    }

    bool RaycastLight(const Engine::LightSceneProxy& light, const Core::Ray& ray, float maxDistance, float& outDistance)
    {
        outDistance=0;
        const auto origin=light.GetPosition();
        if (!IsFinite(origin) || !IsFinite(ray.Origin) || !IsFinite(ray.Direction) ||
            ray.Direction.LengthSquared()<1e-8f || !std::isfinite(maxDistance) || maxDistance<=0) return false;
        const auto d=ToDouble(ray.Direction), offset=ToDouble(ray.Origin)-ToDouble(origin);
        double nearest=maxDistance;bool hit=false;
        const auto accept=[&](double t) {
            if (std::isfinite(t) && t>=0 && t<=nearest) { nearest=t;hit=true; }
        };
        if (const auto* point=dynamic_cast<const Engine::PointLightSceneProxy*>(&light))
        {
            const double radius=point->GetAttenuationRadius();
            if (!std::isfinite(radius) || radius<=0) return false;
            QuadraticRoots(Core::Dot(d,d),2*Core::Dot(offset,d),Core::Dot(offset,offset)-radius*radius,accept);
        }
        else if (const auto* spot=dynamic_cast<const Engine::SpotLightSceneProxy*>(&light))
        {
            const double length=spot->GetAttenuationRadius(), angle=spot->GetOuterConeAngle();
            if (!std::isfinite(length) || length<=0 || !std::isfinite(angle) || angle<=0 || angle>=Core::Pi<double>()/2 ||
                !IsFinite(spot->GetDirection()) || spot->GetDirection().LengthSquared()<1e-8f) return false;
            const auto axis=Core::Normalize(ToDouble(spot->GetDirection()));
            const double height=Core::Dot(offset,axis), slope=Core::Dot(d,axis);
            const double cosine=std::cos(angle), cosine2=cosine*cosine;
            const double tolerance=1e-7*std::max(1.0,length);
            QuadraticRoots(slope*slope-cosine2*Core::Dot(d,d),
                2*(height*slope-cosine2*Core::Dot(offset,d)),height*height-cosine2*Core::Dot(offset,offset),
                [&](double t) {
                    const double h=height+slope*t;
                    if (h>=-tolerance && h<=length+tolerance) accept(t);
                });
            // The finite cone includes its base disc and apex. These also handle
            // rays on the cone generator when the side equation is degenerate.
            const double tangent=std::tan(angle);
            for (double cap : {0.0,length})
            {
                const auto testCap=[&](double t) {
                    const auto radial=offset+d*t-axis*cap;
                    const double radius=cap*tangent;
                    if (radial.LengthSquared()<=radius*radius+tolerance*tolerance) accept(t);
                };
                if (std::abs(height-cap)<=tolerance) testCap(0);
                if (std::abs(slope)>1e-12) testCap((cap-height)/slope);
            }
        }
        else if (const auto* directional=dynamic_cast<const Engine::DirectionalLightSceneProxy*>(&light))
            ForEachDirectionalLine(origin,directional->GetDirection(),[&](Core::Float3 a, Core::Float3 b) {
                double distance=0;
                if (RayNearSegment(ray,nearest,a,b,distance)) accept(distance);
            });
        if (hit) outDistance=static_cast<float>(nearest);
        return hit;
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
                ForEachDirectionalLine(origin,direction,[&](Core::Float3 a, Core::Float3 b) {
                    lines.push_back({a,b,yellow});
                });
            }
        }
        AddLines(builder,view,color,depth,lines,"HintRender.Light");
    }
}
