#include "EditorTranslationGizmo.h"
#include "SceneComponent.h"
#include "Transform.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace Editor
{
    namespace
    {
        constexpr float Pi = 3.14159265358979323846f;
        constexpr float TwoPi = 2*Pi;
        constexpr int Segments = 64;
        constexpr float HitPixels = 7;
        const std::array<Core::Float4,3> Colors = {{{1,0.12f,0.12f,1},{0.12f,0.35f,1,1},{0.12f,0.9f,0.2f,1}}};

        bool IsFinite(const Core::Float3& value)
        { return std::isfinite(value.X) && std::isfinite(value.Y) && std::isfinite(value.Z); }

        Core::Float3 Axis(const Core::Float4x4& basis, int axis)
        { return {basis(0,axis),basis(1,axis),basis(2,axis)}; }

        Core::Float3 Rotate(const Core::Float3& direction, const Core::Float3& normal, float angle)
        {
            return direction*std::cos(angle)+Core::Cross(normal,direction)*std::sin(angle)+
                normal*(Core::Dot(normal,direction)*(1-std::cos(angle)));
        }

        struct RingGeometry
        {
            Core::Float3 Origin;
            Core::Float4x4 Basis;
            float Radius = 0;
        };

        bool BuildGeometry(const Engine::SceneView& view, const Engine::SceneComponent* target, RingGeometry& geometry)
        {
            if (!target || view.Viewport.Width<=0 || view.Viewport.Height<=0) return false;
            geometry.Origin=target->GetWorldLocation();
            geometry.Basis=GizmoDrawing::RotationBasis(target);
            geometry.Radius=GizmoDrawing::WorldLength(view,geometry.Origin,75);
            Core::Float2 center;
            if (!IsFinite(geometry.Origin) || !std::isfinite(geometry.Radius) || geometry.Radius<=0 ||
                !GizmoDrawing::Project(view,geometry.Origin,center)) return false;
            for (int axis=0;axis<3;++axis)
            {
                const auto direction=Axis(geometry.Basis,axis);
                if (!IsFinite(direction) || direction.LengthSquared()<0.5f) return false;
            }
            return true;
        }

        Core::Float3 RingDirection(const Core::Float4x4& basis, int axis, float angle)
        {
            // Cyclic bases ensure u x v points along the positive selected axis.
            return Axis(basis,(axis+1)%3)*std::cos(angle)+Axis(basis,(axis+2)%3)*std::sin(angle);
        }

        struct Pick
        {
            int AxisIndex = -1;
            Core::Float3 Direction;
            float DistanceSquared = HitPixels*HitPixels;
            float Depth = std::numeric_limits<float>::max();
        };

        Pick PickRing(const Engine::SceneView& view, const RingGeometry& geometry, Core::Float2 position)
        {
            Pick result;
            if (!std::isfinite(position.X) || !std::isfinite(position.Y) || position.X<0 || position.Y<0 ||
                position.X>=view.Viewport.Width || position.Y>=view.Viewport.Height) return result;
            for (int axis=0;axis<3;++axis)
            {
                for (int segment=0;segment<Segments;++segment)
                {
                    const auto first=RingDirection(geometry.Basis,axis,TwoPi*segment/Segments);
                    const auto second=RingDirection(geometry.Basis,axis,TwoPi*(segment+1)/Segments);
                    Core::Float2 a,b;
                    if (!GizmoDrawing::Project(view,geometry.Origin+first*geometry.Radius,a) ||
                        !GizmoDrawing::Project(view,geometry.Origin+second*geometry.Radius,b)) continue;
                    const auto edge=b-a;
                    if (edge.LengthSquared()<1e-6f) continue;
                    const float t=std::clamp(Core::Dot(position-a,edge)/edge.LengthSquared(),0.0f,1.0f);
                    const float distance=(position-(a+edge*t)).LengthSquared();
                    const auto direction=Core::Normalize(first*(1-t)+second*t);
                    const auto world=geometry.Origin+direction*geometry.Radius;
                    const float depth=-(view.ViewMatrix(2,0)*world.X+view.ViewMatrix(2,1)*world.Y+
                        view.ViewMatrix(2,2)*world.Z+view.ViewMatrix(2,3));
                    const bool nearer=std::abs(distance-result.DistanceSquared)<0.15f && depth<result.Depth;
                    if (distance<HitPixels*HitPixels && (distance<result.DistanceSquared-0.15f || nearer))
                    {
                        result.AxisIndex=axis; result.Direction=direction;
                        result.DistanceSquared=distance; result.Depth=depth;
                    }
                }
            }
            return result;
        }

        bool PlaneDirection(const Core::Float4x4& inverse, const Engine::ViewRect& viewport, bool zeroToOne,
            Core::Float2 position, const Core::Float3& origin, const Core::Float3& normal, float radius,
            Core::Float3& direction)
        {
            Core::Float3 rayOrigin,rayDirection;
            if (!GizmoDrawing::MakeRay(inverse,viewport,zeroToOne,position,rayOrigin,rayDirection)) return false;
            const float denominator=Core::Dot(rayDirection,normal);
            if (std::abs(denominator)<0.08f) return false;
            const float distance=Core::Dot(origin-rayOrigin,normal)/denominator;
            if (!std::isfinite(distance) || distance<0) return false;
            direction=rayOrigin+rayDirection*distance-origin;
            direction=direction-normal*Core::Dot(direction,normal);
            if (!IsFinite(direction) || direction.LengthSquared()<radius*radius*0.01f) return false;
            direction=Core::Normalize(direction);
            return true;
        }

        bool DragReference(const Engine::SceneView& view, const RingGeometry& geometry, const Pick& pick, Core::Float2 position,
            Core::Float3& direction, Core::Float2& tangent, bool& screenDrag)
        {
            if (pick.AxisIndex<0) return false;
            const auto normal=Core::Normalize(Axis(geometry.Basis,pick.AxisIndex));
            screenDrag=!PlaneDirection(view.InvViewProjectionMatrix,view.Viewport,view.bDepthRangeZeroToOne,
                position,geometry.Origin,normal,geometry.Radius,direction);
            if (!screenDrag) return true;
            direction=pick.Direction;
            Core::Float2 start,next;
            if (!GizmoDrawing::Project(view,geometry.Origin+direction*geometry.Radius,start) ||
                !GizmoDrawing::Project(view,geometry.Origin+Rotate(direction,normal,0.01f)*geometry.Radius,next)) return false;
            tangent=(next-start)/0.01f;
            return std::isfinite(tangent.X) && std::isfinite(tangent.Y) && tangent.LengthSquared()>=16;
        }

        float NearAngle(float angle, float reference)
        { return angle+360.0f*std::round((reference-angle)/360.0f); }

        Core::Float3 NearEuler(const Core::Float4x4& rotation, const Core::Float3& reference)
        {
            // MakeRotationMatrix is Rz(Z)*Ry(Y)*Rx(X). The existing general
            // GetEulerRotation returns its Y/Z results in the opposite order.
            // Extract this proper rotation directly without decomposing scale.
            const float cosine=std::hypot(rotation(0,0),rotation(1,0));
            float x,y,z;
            y=std::atan2(-rotation(2,0),cosine);
            if (cosine>1e-5f)
            {
                x=std::atan2(rotation(2,1),rotation(2,2));
                z=std::atan2(rotation(1,0),rotation(0,0));
            }
            else
            {
                // At either Y pole only z +/- x is observable. Keep the prior
                // X angle and solve Z instead of snapping X to an arbitrary zero.
                x=Core::DegToRad(reference.X);
                z=std::atan2(-rotation(0,1),rotation(1,1))+(y>=0 ? x : -x);
            }
            const float degrees=180.0f/Pi;
            Core::Float3 first{x*degrees,y*degrees,z*degrees};
            Core::Float3 second{first.X+180,180-first.Y,first.Z+180};
            for (int axis=0;axis<3;++axis)
            {
                first[axis]=NearAngle(first[axis],reference[axis]);
                second[axis]=NearAngle(second[axis],reference[axis]);
            }
            return (first-reference).LengthSquared()<=(second-reference).LengthSquared() ? first : second;
        }

        bool SameViewport(const Engine::ViewRect& first, const Engine::ViewRect& second)
        { return first.X==second.X && first.Y==second.Y && first.Width==second.Width && first.Height==second.Height; }
    }

    void EditorRotationGizmo::SetTarget(Engine::SceneComponent* target)
    {
        if (m_Target==target) return;
        EndDrag(); ClearHover(); m_Target=target;
    }

    int EditorRotationGizmo::HitTest(const Engine::SceneView& view, Core::Float2 position) const
    {
        RingGeometry geometry;
        if (!BuildGeometry(view,m_Target,geometry) || !IsFinite(m_Target->GetLocalRotation())) return -1;
        const auto pick=PickRing(view,geometry,position);
        Core::Float3 direction; Core::Float2 tangent; bool screenDrag;
        return DragReference(view,geometry,pick,position,direction,tangent,screenDrag) ? pick.AxisIndex : -1;
    }

    void EditorRotationGizmo::UpdateHover(const Engine::SceneView& view, Core::Float2 position)
    { if (!IsDragging()) m_Hovered=HitTest(view,position); }
    void EditorRotationGizmo::ClearHover() { m_Hovered=-1; }

    bool EditorRotationGizmo::BeginDrag(const Engine::SceneView& view, Core::Float2 position)
    {
        EndDrag();
        RingGeometry geometry;
        if (!BuildGeometry(view,m_Target,geometry) || !IsFinite(m_Target->GetLocalRotation())) return false;
        const auto pick=PickRing(view,geometry,position);
        if (pick.AxisIndex<0) return false;
        m_Origin=geometry.Origin; m_Radius=geometry.Radius; m_StartBasis=geometry.Basis;
        m_StartEuler=m_Target->GetLocalRotation(); m_StartRotation=Core::MakeRotationMatrix(m_StartEuler);
        m_DragInverseViewProjection=view.InvViewProjectionMatrix; m_DragViewport=view.Viewport;
        m_bDepthZeroToOne=view.bDepthRangeZeroToOne; m_PressPosition=position;
        // The shared reference check also gates hover feedback. Edge-on tips
        // with no usable projected tangent are not advertised as draggable.
        if (!DragReference(view,geometry,pick,position,m_StartDirection,m_ScreenTangent,m_bScreenDrag)) return false;
        m_LastDirection=m_StartDirection;
        m_Active=m_Hovered=pick.AxisIndex;
        return true;
    }

    bool EditorRotationGizmo::Drag(const Engine::SceneView& view, Core::Float2 position, Core::Float3& outLocalRotation)
    {
        if (!IsDragging() || !m_Target || !SameViewport(view.Viewport,m_DragViewport) ||
            !std::isfinite(position.X) || !std::isfinite(position.Y)) return false;
        const auto normal=Core::Normalize(Axis(m_StartBasis,m_Active));
        float angle;
        Core::Float3 currentDirection;
        if (m_bScreenDrag)
            angle=Core::Dot(position-m_PressPosition,m_ScreenTangent)/m_ScreenTangent.LengthSquared();
        else
        {
            if (!PlaneDirection(m_DragInverseViewProjection,m_DragViewport,m_bDepthZeroToOne,
                position,m_Origin,normal,m_Radius,currentDirection)) return false;
            const float increment=std::atan2(Core::Dot(normal,Core::Cross(m_LastDirection,currentDirection)),
                std::clamp(Core::Dot(m_LastDirection,currentDirection),-1.0f,1.0f));
            angle=m_Angle+increment;
        }
        if (!std::isfinite(angle)) return false;
        Core::Float3 delta{}; delta[m_Active]=angle*(180.0f/Pi);
        const auto candidate=m_StartRotation*Core::MakeRotationMatrix(delta);
        outLocalRotation=std::abs(angle)<1e-7f ? m_StartEuler : NearEuler(candidate,m_Target->GetLocalRotation());
        if (!IsFinite(outLocalRotation)) return false;
        m_Angle=angle;
        if (!m_bScreenDrag) m_LastDirection=currentDirection;
        return true;
    }

    void EditorRotationGizmo::EndDrag()
    { m_Active=-1; m_Angle=0; }

    void EditorRotationGizmo::AddPass(RenderCore::RenderGraphBuilder& builder, const Engine::SceneView& view,
        RenderCore::RenderGraphTextureRef sceneColor) const
    {
        RingGeometry geometry;
        if (!BuildGeometry(view,m_Target,geometry)) return;
        std::vector<GizmoDrawing::Triangle> triangles;
        triangles.reserve(3*Segments*2+Segments+4);
        if (IsDragging() && std::abs(m_Angle)>1e-6f)
        {
            // One full disk is the maximum visual sweep; the actual accumulated
            // angle stays unwrapped for repeated turns and matrix composition.
            const float sweep=std::clamp(m_Angle,-TwoPi,TwoPi);
            const int count=std::max(1,static_cast<int>(std::ceil(std::abs(sweep)*Segments/TwoPi)));
            const auto normal=Core::Normalize(Axis(m_StartBasis,m_Active));
            Core::Float2 center;
            if (GizmoDrawing::Project(view,m_Origin,center))
            {
                for (int segment=0;segment<count;++segment)
                {
                    Core::Float2 a,b;
                    const auto first=Rotate(m_StartDirection,normal,sweep*segment/count);
                    const auto second=Rotate(m_StartDirection,normal,sweep*(segment+1)/count);
                    if (GizmoDrawing::Project(view,m_Origin+first*m_Radius,a) &&
                        GizmoDrawing::Project(view,m_Origin+second*m_Radius,b))
                        triangles.push_back({center,a,b,{1,1,0,0.28f}});
                }
                Core::Float2 first,last;
                if (GizmoDrawing::Project(view,m_Origin+m_StartDirection*m_Radius,first))
                    GizmoDrawing::Line(triangles,center,first,{1,1,0,0.95f},2);
                if (GizmoDrawing::Project(view,m_Origin+Rotate(m_StartDirection,normal,m_Angle)*m_Radius,last))
                    GizmoDrawing::Line(triangles,center,last,{1,1,0,0.95f},2);
            }
        }
        for (int axis=0;axis<3;++axis)
        {
            const auto color=axis==(IsDragging() ? m_Active : m_Hovered) ? Core::Float4{1,1,0,1} : Colors[axis];
            for (int segment=0;segment<Segments;++segment)
            {
                Core::Float2 a,b;
                if (GizmoDrawing::Project(view,geometry.Origin+RingDirection(geometry.Basis,axis,TwoPi*segment/Segments)*geometry.Radius,a) &&
                    GizmoDrawing::Project(view,geometry.Origin+RingDirection(geometry.Basis,axis,TwoPi*(segment+1)/Segments)*geometry.Radius,b))
                    GizmoDrawing::Line(triangles,a,b,color);
            }
        }
        GizmoDrawing::AddPass(builder,view,sceneColor,triangles,"EditorRotationGizmo");
    }
}
