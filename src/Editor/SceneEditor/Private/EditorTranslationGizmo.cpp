#include "EditorTranslationGizmo.h"
#include "SceneComponent.h"
#include "Transform.hpp"
#include "GlobalShader.h"
#include "ShaderParameter.h"
#include "ShaderCompiler.h"
#include "RenderGraphBuilder.h"
#include "RHIPipelineStateCache.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace Editor
{
    namespace
    {
        constexpr float AxisPixels = 90.0f;
        constexpr float PlaneExtent = 0.30f;
        constexpr float MinAxisPixels = 12.0f;
        constexpr float HitPixels = 7.0f;
        constexpr float MinPlaneCosine = 0.06f;
        const std::array<Core::Float3, 3> Axes = {{{1,0,0},{0,1,0},{0,0,1}}};
        const std::array<std::array<int, 2>, 3> PlaneAxes = {{{0,1},{0,2},{1,2}}};
        const std::array<Core::Float4, 3> Colors = {{{1,0.12f,0.12f,1},{0.12f,0.35f,1,1},{0.12f,0.9f,0.2f,1}}};

        struct Geometry
        {
            Core::Float3 Origin;
            Core::Float2 Center;
            float Length = 0;
            std::array<Core::Float2, 3> Ends;
            std::array<bool, 3> bVisible{};
            std::array<bool, 3> bUsable{};
        };

        bool IsFinite(const Core::Float3& value)
        { return std::isfinite(value.X) && std::isfinite(value.Y) && std::isfinite(value.Z); }

        bool CanSetWorldLocation(const Engine::SceneComponent* target)
        {
            const auto* parent = target->GetAttachParent();
            if (!parent) return true;
            const auto& matrix = parent->GetComponentTransform();
            for (int row=0; row<4; ++row)
                for (int column=0; column<4; ++column)
                    if (!std::isfinite(matrix(row,column))) return false;
            double volume = 1;
            for (int column=0; column<3; ++column)
            {
                const Core::Float3 basis{matrix(0,column),matrix(1,column),matrix(2,column)};
                if (!IsFinite(basis)) return false;
                volume *= static_cast<double>(basis.Length());
            }
            const double determinant = Core::Determinant(matrix);
            // Relative test accepts legitimate small scales, while rejecting
            // singular parents before SceneComponent attempts their inverse.
            return std::isfinite(volume) && volume > 0 && std::isfinite(determinant) &&
                std::abs(determinant) > volume*1e-6;
        }

        Core::Float4 Transform(const Core::Float4x4& matrix, const Core::Float4& point)
        {
            Core::Float4 result;
            for (int row=0; row<4; ++row)
                for (int column=0; column<4; ++column) result[row] += matrix(row,column)*point[column];
            return result;
        }

        bool Project(const Engine::SceneView& view, const Core::Float3& point, Core::Float2& screen)
        {
            const auto clip = Transform(view.ViewProjectionMatrix,{point.X,point.Y,point.Z,1});
            if (!std::isfinite(clip.W) || !std::isfinite(clip.Z) || clip.W <= 1e-5f ||
                clip.Z < (view.bDepthRangeZeroToOne ? 0.0f : -clip.W) || clip.Z > clip.W) return false;
            screen = {(clip.X/clip.W+1)*0.5f*view.Viewport.Width,
                (1-clip.Y/clip.W)*0.5f*view.Viewport.Height};
            return std::isfinite(screen.X) && std::isfinite(screen.Y);
        }

        bool BuildGeometry(const Engine::SceneView& view, const Engine::SceneComponent* target, Geometry& geometry)
        {
            if (!target || view.Viewport.Width <= 0 || view.Viewport.Height <= 0) return false;
            geometry.Origin = target->GetWorldLocation();
            if (!IsFinite(geometry.Origin) || !Project(view,geometry.Origin,geometry.Center)) return false;
            const auto clip = Transform(view.ViewProjectionMatrix,{geometry.Origin.X,geometry.Origin.Y,geometry.Origin.Z,1});
            const float scale = std::abs(view.ProjectionMatrix(1,1))*view.Viewport.Height;
            if (!std::isfinite(scale) || scale < 1e-5f) return false;
            geometry.Length = AxisPixels*2*clip.W/scale;
            if (!std::isfinite(geometry.Length) || geometry.Length <= 0) return false;
            for (int axis=0; axis<3; ++axis)
            {
                geometry.bVisible[axis] = Project(view,geometry.Origin+Axes[axis]*geometry.Length,geometry.Ends[axis]);
                geometry.bUsable[axis] = geometry.bVisible[axis] &&
                    (geometry.Ends[axis]-geometry.Center).LengthSquared() >= MinAxisPixels*MinAxisPixels;
            }
            return true;
        }

        bool MakeRay(const Core::Float4x4& inverse, const Engine::ViewRect& viewport, bool zeroToOne,
            Core::Float2 position, Core::Float3& origin, Core::Float3& direction)
        {
            if (viewport.Width <= 0 || viewport.Height <= 0 || !std::isfinite(position.X) || !std::isfinite(position.Y)) return false;
            const float x = 2*position.X/viewport.Width-1, y = 1-2*position.Y/viewport.Height;
            auto nearPoint = Transform(inverse,{x,y,zeroToOne ? 0.0f : -1.0f,1});
            auto farPoint = Transform(inverse,{x,y,1,1});
            if (std::abs(nearPoint.W)<1e-6f || std::abs(farPoint.W)<1e-6f) return false;
            origin = {nearPoint.X/nearPoint.W,nearPoint.Y/nearPoint.W,nearPoint.Z/nearPoint.W};
            const Core::Float3 delta{farPoint.X/farPoint.W-origin.X,farPoint.Y/farPoint.W-origin.Y,farPoint.Z/farPoint.W-origin.Z};
            if (!IsFinite(origin) || !IsFinite(delta) || delta.LengthSquared()<1e-10f) return false;
            direction = Core::Normalize(delta);
            return true;
        }

        bool IntersectPlane(const Core::Float3& rayOrigin, const Core::Float3& rayDirection,
            const Core::Float3& planeOrigin, const Core::Float3& normal, Core::Float3& hit, float& distance)
        {
            const float denominator = Core::Dot(rayDirection,normal);
            if (std::abs(denominator) < MinPlaneCosine) return false;
            distance = Core::Dot(planeOrigin-rayOrigin,normal)/denominator;
            if (!std::isfinite(distance) || distance < 0) return false;
            hit = rayOrigin+rayDirection*distance;
            return IsFinite(hit);
        }

        bool GetAxisPlaneNormal(const Core::Float3& direction, int axis, Core::Float3& normal)
        {
            normal = direction-Axes[axis]*Core::Dot(direction,Axes[axis]);
            if (normal.LengthSquared()<0.01f) return false;
            normal = Core::Normalize(normal);
            return true;
        }

        int GetAxisIndex(ETranslationHandle handle) { return static_cast<int>(handle)-static_cast<int>(ETranslationHandle::X); }
        int GetPlaneIndex(ETranslationHandle handle) { return static_cast<int>(handle)-static_cast<int>(ETranslationHandle::XY); }

        bool ProjectPlane(const Engine::SceneView& view, const Geometry& geometry, int plane,
            std::array<Core::Float2, 4>& points)
        {
            const auto a = Axes[PlaneAxes[plane][0]]*(geometry.Length*PlaneExtent);
            const auto b = Axes[PlaneAxes[plane][1]]*(geometry.Length*PlaneExtent);
            points[0] = geometry.Center;
            return Project(view,geometry.Origin+a,points[1]) &&
                Project(view,geometry.Origin+a+b,points[2]) && Project(view,geometry.Origin+b,points[3]);
        }
    }

    bool GizmoDrawing::Project(const Engine::SceneView& view, const Core::Float3& point, Core::Float2& screen)
    { return Editor::Project(view,point,screen); }

    bool GizmoDrawing::MakeRay(const Core::Float4x4& inverse, const Engine::ViewRect& viewport, bool zeroToOne,
        Core::Float2 position, Core::Float3& origin, Core::Float3& direction)
    { return Editor::MakeRay(inverse,viewport,zeroToOne,position,origin,direction); }

    float GizmoDrawing::WorldLength(const Engine::SceneView& view, const Core::Float3& origin, float pixels)
    {
        Core::Float2 projected;
        if (view.Viewport.Height <= 0 || !Project(view,origin,projected)) return 0;
        const auto clip = Transform(view.ViewProjectionMatrix,{origin.X,origin.Y,origin.Z,1});
        const float denominator = std::abs(view.ProjectionMatrix(1,1))*view.Viewport.Height;
        if (!std::isfinite(denominator) || denominator < 1e-5f) return 0;
        const float length = pixels*2*clip.W/denominator;
        return std::isfinite(length) && length > 0 ? length : 0;
    }

    Core::Float4x4 GizmoDrawing::RotationBasis(const Engine::SceneComponent* target)
    {
        auto rotation = Core::Float4x4::Identity();
        // Local editing axes are the hierarchy's rotations, without signed or
        // nonuniform scales. Never decompose a potentially sheared world matrix.
        for (auto* component=target; component; component=component->GetAttachParent())
            rotation = Core::MakeRotationMatrix(component->GetLocalRotation())*rotation;
        return rotation;
    }

    void GizmoDrawing::Line(std::vector<Triangle>& triangles, Core::Float2 a, Core::Float2 b, Core::Float4 color, float width)
    {
        const auto edge=b-a;
        const float length=edge.Length();
        if (!std::isfinite(length) || length < 0.01f) return;
        const Core::Float2 half{-edge.Y/length*width*0.5f,edge.X/length*width*0.5f};
        triangles.push_back({a-half,b-half,b+half,color});
        triangles.push_back({a-half,b+half,a+half,color});
    }

    void EditorTranslationGizmo::SetTarget(Engine::SceneComponent* target)
    {
        if (m_Target == target) return;
        EndDrag(); ClearHover(); m_Target = target;
    }

    ETranslationHandle EditorTranslationGizmo::HitTest(const Engine::SceneView& view, Core::Float2 position) const
    {
        Geometry geometry;
        if (!m_Target || !CanSetWorldLocation(m_Target) || !BuildGeometry(view,m_Target,geometry) ||
            position.X<0 || position.Y<0 || position.X>=view.Viewport.Width || position.Y>=view.Viewport.Height)
            return ETranslationHandle::None;
        Core::Float3 rayOrigin, rayDirection;
        if (!MakeRay(view.InvViewProjectionMatrix,view.Viewport,view.bDepthRangeZeroToOne,position,rayOrigin,rayDirection))
            return ETranslationHandle::None;

        ETranslationHandle result = ETranslationHandle::None;
        float best = HitPixels*HitPixels;
        for (int axis=0; axis<3; ++axis)
        {
            Core::Float3 normal;
            if (!geometry.bUsable[axis] || !GetAxisPlaneNormal(rayDirection,axis,normal)) continue;
            const auto edge = geometry.Ends[axis]-geometry.Center;
            const float t = std::clamp(Core::Dot(position-geometry.Center,edge)/edge.LengthSquared(),0.38f,1.0f);
            const float distance = (position-(geometry.Center+edge*t)).LengthSquared();
            if (distance < best) { best=distance; result=static_cast<ETranslationHandle>(axis+static_cast<int>(ETranslationHandle::X)); }
        }
        if (result != ETranslationHandle::None) return result;

        Core::Float3 centerRayOrigin, centerRayDirection;
        if (!MakeRay(view.InvViewProjectionMatrix,view.Viewport,view.bDepthRangeZeroToOne,
            geometry.Center,centerRayOrigin,centerRayDirection)) return ETranslationHandle::None;
        float nearest = std::numeric_limits<float>::max();
        for (int plane=0; plane<3; ++plane)
        {
            std::array<Core::Float2,4> corners;
            if (!ProjectPlane(view,geometry,plane,corners)) continue;
            const int a=PlaneAxes[plane][0], b=PlaneAxes[plane][1];
            const auto normal = Core::Cross(Axes[a],Axes[b]);
            if (std::abs(Core::Dot(centerRayDirection,normal)) < MinPlaneCosine) continue;
            Core::Float3 hit; float distance;
            if (!IntersectPlane(rayOrigin,rayDirection,geometry.Origin,normal,hit,distance)) continue;
            const auto delta = hit-geometry.Origin;
            const float u=Core::Dot(delta,Axes[a])/geometry.Length, v=Core::Dot(delta,Axes[b])/geometry.Length;
            if (u>=0 && u<=PlaneExtent && v>=0 && v<=PlaneExtent && distance<nearest)
            {
                nearest=distance;
                result=static_cast<ETranslationHandle>(plane+static_cast<int>(ETranslationHandle::XY));
            }
        }
        return result;
    }

    void EditorTranslationGizmo::UpdateHover(const Engine::SceneView& view, Core::Float2 position)
    { if (!IsDragging()) m_Hovered = HitTest(view,position); }
    void EditorTranslationGizmo::ClearHover() { m_Hovered = ETranslationHandle::None; }

    bool EditorTranslationGizmo::BeginDrag(const Engine::SceneView& view, Core::Float2 position)
    {
        EndDrag();
        const auto handle = HitTest(view,position);
        if (handle == ETranslationHandle::None) return false;
        Core::Float3 rayOrigin, rayDirection;
        if (!MakeRay(view.InvViewProjectionMatrix,view.Viewport,view.bDepthRangeZeroToOne,position,rayOrigin,rayDirection)) return false;
        m_StartOrigin = m_Target->GetWorldLocation();
        if (handle <= ETranslationHandle::Z)
        {
            if (!GetAxisPlaneNormal(rayDirection,GetAxisIndex(handle),m_DragNormal)) return false;
        }
        else
        {
            const auto axes = PlaneAxes[GetPlaneIndex(handle)];
            m_DragNormal = Core::Cross(Axes[axes[0]],Axes[axes[1]]);
        }
        float distance;
        if (!IntersectPlane(rayOrigin,rayDirection,m_StartOrigin,m_DragNormal,m_StartHit,distance)) return false;
        // Keep the press-time camera and plane: moving the target cannot feed
        // back into the drag anchor, and a press does not snap its position.
        m_DragInverseViewProjection = view.InvViewProjectionMatrix;
        m_DragViewport = view.Viewport;
        m_bDragDepthZeroToOne = view.bDepthRangeZeroToOne;
        m_Active = m_Hovered = handle;
        return true;
    }

    bool EditorTranslationGizmo::Drag(const Engine::SceneView& view, Core::Float2 position, Core::Float3& outWorldLocation) const
    {
        if (!IsDragging() || !m_Target || !CanSetWorldLocation(m_Target) ||
            view.Viewport.X != m_DragViewport.X || view.Viewport.Y != m_DragViewport.Y ||
            view.Viewport.Width != m_DragViewport.Width || view.Viewport.Height != m_DragViewport.Height) return false;
        Core::Float3 rayOrigin, rayDirection, hit;
        if (!MakeRay(m_DragInverseViewProjection,m_DragViewport,m_bDragDepthZeroToOne,position,rayOrigin,rayDirection)) return false;
        float distance;
        if (!IntersectPlane(rayOrigin,rayDirection,m_StartOrigin,m_DragNormal,hit,distance)) return false;
        const auto delta = hit-m_StartHit;
        outWorldLocation = m_StartOrigin;
        if (m_Active <= ETranslationHandle::Z)
        {
            const int axis = GetAxisIndex(m_Active);
            outWorldLocation[axis] += delta[axis];
        }
        else
        {
            const auto axes = PlaneAxes[GetPlaneIndex(m_Active)];
            outWorldLocation[axes[0]] += delta[axes[0]];
            outWorldLocation[axes[1]] += delta[axes[1]];
        }
        return IsFinite(outWorldLocation);
    }

    void EditorTranslationGizmo::EndDrag() { m_Active = ETranslationHandle::None; }

    BEGIN_SHADER_PARAMETER_STRUCT(EditorTranslationVertexParameters)
        SHADER_PARAMETER(Core::Float4, Point0)
        SHADER_PARAMETER(Core::Float4, Point1)
        SHADER_PARAMETER(Core::Float4, Point2)
        SHADER_PARAMETER(Core::Float4, HandleColor)
    END_SHADER_PARAMETER_STRUCT(EditorTranslationVertexParameters)

    BEGIN_SHADER_PARAMETER_STRUCT(EditorTranslationPassParameters)
        SHADER_PARAMETER_RENDER_TARGET_BINDING_SLOTS(Targets)
    END_SHADER_PARAMETER_STRUCT(EditorTranslationPassParameters)

    class EditorTranslationVS : public RenderCore::GlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER_TYPE(EditorTranslationVS)
        static void ModifyShaderCompilerEnvironment(const RenderCore::ShaderPermutationParameters&, RenderCore::ShaderCompilerEnvironment& environment)
        { environment.SetDefine("VERTEX_SHADER",1); }
        static const RenderCore::ShaderParametersMetadata* GetShaderParameterMetadata()
        { return EditorTranslationVertexParameters::GetMetaData(); }
    };
    class EditorTranslationPS : public RenderCore::GlobalShader
    {
    public:
        DECLARE_GLOBAL_SHADER_TYPE(EditorTranslationPS)
        static void ModifyShaderCompilerEnvironment(const RenderCore::ShaderPermutationParameters&, RenderCore::ShaderCompilerEnvironment&)
        {
            // The include expander visits the shared file's VS include even in
            // the PS permutation. Register it here too; shader order is unordered.
            RenderCore::ShaderCompiler::GenerateOrGetShaderPrameterMetaDataSF(*EditorTranslationVertexParameters::GetMetaData());
        }
        static const RenderCore::ShaderParametersMetadata* GetShaderParameterMetadata()
        { return EditorTranslationPassParameters::GetMetaData(); }
    };
    IMPLEMENT_GLOBAL_SHADER_TYPE(EditorTranslationVS,"EditorTranslationVS","/editor/TranslationGizmo.sf","MainVS",RHI::ERHIShaderFrequency::Vertex);
    IMPLEMENT_GLOBAL_SHADER_TYPE(EditorTranslationPS,"EditorTranslationPS","/editor/TranslationGizmo.sf","MainPS",RHI::ERHIShaderFrequency::Fragment);

    void GizmoDrawing::AddPass(RenderCore::RenderGraphBuilder& builder, const Engine::SceneView& view,
        RenderCore::RenderGraphTextureRef sceneColor, std::span<const Triangle> triangles, const char* name)
    {
        using namespace RenderCore;
        using namespace RHI;
        if (triangles.empty() || view.Viewport.Width <= 0 || view.Viewport.Height <= 0) return;
        auto* vs=GShaderMap.GetShader<EditorTranslationVS>(0);
        auto* ps=GShaderMap.GetShader<EditorTranslationPS>(0);
        if (!vs || !ps) throw std::runtime_error("Editor gizmo shaders are unavailable");
        RHIGraphicsPipelineStateDesc desc;
        desc.ShaderStages.VertexShader=static_cast<RHIVertexShader*>(vs->GetRHIShader());
        desc.ShaderStages.FragmentShader=static_cast<RHIFragmentShader*>(ps->GetRHIShader());
        desc.VertexDescState=RHIPipelineStateCache::GetOrCreateVertexDescState({});
        RHIRasterizerStateDesc rasterizer; rasterizer.CullMode=ERHICullMode::None;
        desc.RasterizerState=RHIPipelineStateCache::GetOrCreateRasterizerState(rasterizer);
        desc.DepthStencilState=RHIPipelineStateCache::GetOrCreateDepthStencilState({});
        RHIColorBlendStateDesc blend; blend.Attachments.resize(1); blend.Attachments[0].bBlendEnable=true;
        blend.Attachments[0].ColorWriteMask=0x7; // Keep scene alpha opaque for GUI composition.
        desc.ColorBlendState=RHIPipelineStateCache::GetOrCreateColorBlendState(blend);
        desc.AttachmentDesc.ColorAttachmentCount=1;
        desc.AttachmentDesc.ColorAttachments[0].Format=sceneColor->GetDesc().Format;
        desc.AttachmentDesc.ColorAttachments[0].Actions=ERenderTargetActions::Load_Store;
        desc.PrimitiveTopology=EPrimitiveTopology::TriangleList;
        auto* pipeline=RHIPipelineStateCache::GetOrCreateGraphicsPipelineState(desc);
        auto* parameters=builder.AllocateParameter<std::vector<EditorTranslationVertexParameters*>>();
        parameters->reserve(triangles.size());
        const auto ndc=[&](Core::Float2 point) { return Core::Float4{2*point.X/view.Viewport.Width-1,1-2*point.Y/view.Viewport.Height,0.5f,1}; };
        for (const auto& triangle : triangles)
        {
            auto* data=builder.AllocateParameter<EditorTranslationVertexParameters>();
            data->Point0=ndc(triangle.A); data->Point1=ndc(triangle.B); data->Point2=ndc(triangle.C); data->HandleColor=triangle.Color;
            parameters->push_back(data);
        }
        auto* params=builder.AllocateParameter<EditorTranslationPassParameters>();
        params->Targets.NumColorRenderTargets=1;
        params->Targets[0].Texture=sceneColor;
        params->Targets[0].Action=ERenderTargetActions::Load_Store;
        const auto rect=view.Viewport;
        // Only immutable parameter snapshots cross into deferred recording.
        builder.AddPass<EditorTranslationPassParameters>(name,EditorTranslationPassParameters::GetMetaData(),params,EPassFlag::Graphic,
            [=](RHICommandListBase& commands) {
                auto& cmd=static_cast<RHIGraphicCommandList&>(commands);
                RHIRenderPassInfo info;
                info.RenderTargets=params->Targets.GetBoundRenderTarget();
                info.RenderArea={rect.X,rect.Y,static_cast<uint32_t>(rect.Width),static_cast<uint32_t>(rect.Height)};
                cmd.BeginRenderPass(info);
                cmd.SetGraphicPipelineState(pipeline);
                cmd.SetViewport(static_cast<float>(rect.X),static_cast<float>(rect.Y),static_cast<float>(rect.Width),static_cast<float>(rect.Height),0,1);
                cmd.SetScissor(rect.X,rect.Y,rect.Width,rect.Height);
                for (auto* data : *parameters) { SetShaderParameters(cmd,vs,data); cmd.Draw(3); }
                cmd.EndRenderPass();
            });
    }

    void EditorTranslationGizmo::AddPass(RenderCore::RenderGraphBuilder& builder, const Engine::SceneView& view,
        RenderCore::RenderGraphTextureRef sceneColor) const
    {
        Geometry geometry;
        if (!BuildGeometry(view,m_Target,geometry)) return;
        std::vector<GizmoDrawing::Triangle> triangles;
        triangles.reserve(15);
        const auto triangle=[&](Core::Float2 a,Core::Float2 b,Core::Float2 c,Core::Float4 color) {
            triangles.push_back({a,b,c,color});
        };
        const auto highlighted=[&](ETranslationHandle handle) {
            return handle == (IsDragging() ? m_Active : m_Hovered);
        };
        Core::Float3 rayOrigin, rayDirection;
        if (!MakeRay(view.InvViewProjectionMatrix,view.Viewport,view.bDepthRangeZeroToOne,geometry.Center,rayOrigin,rayDirection)) return;
        std::array<int,3> order={0,1,2};
        std::stable_sort(order.begin(),order.end(),[&](int a,int b) {
            const auto center=[&](int plane) { return geometry.Origin+
                (Axes[PlaneAxes[plane][0]]+Axes[PlaneAxes[plane][1]])*(geometry.Length*PlaneExtent*0.5f); };
            return Core::Dot(center(a)-rayOrigin,rayDirection)>Core::Dot(center(b)-rayOrigin,rayDirection);
        });
        for (int plane : order)
        {
            std::array<Core::Float2,4> points;
            if (!ProjectPlane(view,geometry,plane,points)) continue;
            const auto axes=PlaneAxes[plane];
            if (std::abs(Core::Dot(rayDirection,Core::Cross(Axes[axes[0]],Axes[axes[1]])))<MinPlaneCosine) continue;
            auto color=Colors[3-axes[0]-axes[1]];
            color.W=0.35f;
            if (highlighted(static_cast<ETranslationHandle>(plane+static_cast<int>(ETranslationHandle::XY)))) color={1,1,0,0.75f};
            triangle(points[0],points[1],points[2],color);
            triangle(points[0],points[2],points[3],color);
        }
        for (int axis=0; axis<3; ++axis)
        {
            if (!geometry.bVisible[axis]) continue;
            const auto edge=geometry.Ends[axis]-geometry.Center;
            const float length=edge.Length();
            if (length<1) continue;
            const auto direction=edge/length;
            const Core::Float2 perpendicular{-direction.Y,direction.X};
            const float headLength=std::min(12.0f,length*0.28f);
            const auto base=geometry.Ends[axis]-direction*headLength;
            auto color=Colors[axis];
            if (highlighted(static_cast<ETranslationHandle>(axis+static_cast<int>(ETranslationHandle::X)))) color={1,1,0,1};
            if (!geometry.bUsable[axis]) color.W=0.3f;
            const auto half=perpendicular*1.5f;
            triangle(geometry.Center-half,base-half,base+half,color);
            triangle(geometry.Center-half,base+half,geometry.Center+half,color);
            triangle(base-perpendicular*5,geometry.Ends[axis],base+perpendicular*5,color);
        }
        GizmoDrawing::AddPass(builder,view,sceneColor,triangles,"EditorTranslationGizmo");
    }
}
