#include "EditorTranslationGizmo.h"
#include "SceneComponent.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace Editor
{
    namespace
    {
        constexpr float MinimumScale = 0.01f;
        constexpr float MaximumScale = 10000.0f;
        constexpr float HitPixels = 7.0f;
        constexpr float MarkerRadius = 5.0f;
        constexpr float UnitAxisPixels = 55.0f;
        constexpr float MinimumAxisPixels = 12.0f;
        constexpr int MarkerSegments = 12;
        constexpr float TwoPi = 6.28318530718f;
        const std::array<Core::Float4,3> Colors = {{{1,0.12f,0.12f,1},{0.12f,0.35f,1,1},{0.12f,0.9f,0.2f,1}}};

        struct ScaleGeometry
        {
            Core::Float2 Origin;
            std::array<Core::Float2,3> Ends;
            std::array<Core::Float2,3> Midpoints;
            std::array<bool,3> bVisible{};
            std::array<bool,3> bUsable{};
        };

        bool IsFinite(const Core::Float3& value)
        { return std::isfinite(value.X) && std::isfinite(value.Y) && std::isfinite(value.Z); }

        bool IsEditable(float value)
        {
            const float magnitude = std::abs(value);
            return std::isfinite(value) && magnitude >= MinimumScale && magnitude <= MaximumScale;
        }

        bool BuildGeometry(const Engine::SceneView& view, const Engine::SceneComponent* target, ScaleGeometry& geometry)
        {
            if (!target || view.Viewport.Width <= 0 || view.Viewport.Height <= 0) return false;
            const auto origin = target->GetWorldLocation();
            if (!IsFinite(origin) || !GizmoDrawing::Project(view,origin,geometry.Origin)) return false;
            const float length = GizmoDrawing::WorldLength(view,origin,UnitAxisPixels);
            if (!std::isfinite(length) || length <= 0) return false;
            const auto basis = GizmoDrawing::RotationBasis(target);
            const auto scale = target->GetLocalScale();
            for (int axis=0; axis<3; ++axis)
            {
                // Keep the unit tool compact, then move each endpoint with its
                // local scale magnitude. Signed scales retain the colored basis.
                const Core::Float3 direction{basis(0,axis),basis(1,axis),basis(2,axis)};
                const auto offset = direction*(length*std::abs(scale[axis]));
                geometry.bVisible[axis] = IsFinite(direction) &&
                    GizmoDrawing::Project(view,origin+offset,geometry.Ends[axis]) &&
                    GizmoDrawing::Project(view,origin+offset*0.5f,geometry.Midpoints[axis]);
                geometry.bUsable[axis] = geometry.bVisible[axis] && IsEditable(scale[axis]) &&
                    (geometry.Ends[axis]-geometry.Origin).LengthSquared() >= MinimumAxisPixels*MinimumAxisPixels;
            }
            return true;
        }

        void Marker(std::vector<GizmoDrawing::Triangle>& triangles, Core::Float2 center, Core::Float4 color)
        {
            for (int segment=0; segment<MarkerSegments; ++segment)
            {
                const float first = TwoPi*segment/MarkerSegments;
                const float second = TwoPi*(segment+1)/MarkerSegments;
                const auto a = center+Core::Float2{std::cos(first),std::sin(first)}*MarkerRadius;
                const auto b = center+Core::Float2{std::cos(second),std::sin(second)}*MarkerRadius;
                triangles.push_back({center,a,b,color});
            }
        }
    }

    void EditorScaleGizmo::SetTarget(Engine::SceneComponent* target)
    {
        if (m_Target == target) return;
        EndDrag(); ClearHover(); m_Target = target;
    }

    int EditorScaleGizmo::HitTest(const Engine::SceneView& view, Core::Float2 position) const
    {
        if (!std::isfinite(position.X) || !std::isfinite(position.Y) || position.X < 0 || position.Y < 0 ||
            position.X >= view.Viewport.Width || position.Y >= view.Viewport.Height) return -1;
        ScaleGeometry geometry;
        if (!BuildGeometry(view,m_Target,geometry)) return -1;

        int result = -1;
        float best = (MarkerRadius+3)*(MarkerRadius+3);
        // The three unit markers take precedence at projected intersections.
        for (int axis=0; axis<3; ++axis)
        {
            if (!geometry.bUsable[axis]) continue;
            const float distance = (position-geometry.Ends[axis]).LengthSquared();
            if (distance < best) { best=distance; result=axis; }
        }
        if (result >= 0) return result;

        best = HitPixels*HitPixels;
        for (int axis=0; axis<3; ++axis)
        {
            if (!geometry.bUsable[axis]) continue;
            const auto edge = geometry.Ends[axis]-geometry.Origin;
            // Leave the shared origin unpickable, avoiding three simultaneous
            // axis candidates at the same pixel. Connecting edges are decorative.
            const float t = std::clamp(Core::Dot(position-geometry.Origin,edge)/edge.LengthSquared(),0.15f,1.0f);
            const float distance = (position-(geometry.Origin+edge*t)).LengthSquared();
            if (distance < best) { best=distance; result=axis; }
        }
        return result;
    }

    void EditorScaleGizmo::UpdateHover(const Engine::SceneView& view, Core::Float2 position)
    { if (!IsDragging()) m_Hovered = HitTest(view,position); }

    void EditorScaleGizmo::ClearHover() { m_Hovered = -1; }

    bool EditorScaleGizmo::BeginDrag(const Engine::SceneView& view, Core::Float2 position)
    {
        EndDrag();
        const int axis = HitTest(view,position);
        if (axis < 0) return false;
        const auto scale = m_Target->GetLocalScale();
        if (!IsFinite(scale)) return false;
        ScaleGeometry geometry;
        if (!BuildGeometry(view,m_Target,geometry) || !geometry.bUsable[axis]) return false;
        m_StartScale = scale;
        m_PressPosition = position;
        // Freeze the outward screen direction at press time: moving endpoints
        // must not feed back into the drag's sign or sensitivity.
        m_DragDirection = Core::Normalize(geometry.Ends[axis]-geometry.Origin);
        m_DragViewport = view.Viewport;
        m_Active = m_Hovered = axis;
        return true;
    }

    bool EditorScaleGizmo::Drag(const Engine::SceneView& view, Core::Float2 position, Core::Float3& outLocalScale) const
    {
        if (!IsDragging() || !m_Target || !std::isfinite(position.X) || !std::isfinite(position.Y) ||
            view.Viewport.X != m_DragViewport.X || view.Viewport.Y != m_DragViewport.Y ||
            view.Viewport.Width != m_DragViewport.Width || view.Viewport.Height != m_DragViewport.Height) return false;

        outLocalScale = m_StartScale;
        const float distance = Core::Dot(position-m_PressPosition,m_DragDirection);
        if (distance == 0) return true;
        // Accumulate from the press value, preserving its sign. A logarithmic
        // multiplier makes outward/inward drags reciprocal and never crosses 0.
        const double exponent = std::clamp(static_cast<double>(distance)*0.01,-20.0,20.0);
        const double magnitude = std::abs(static_cast<double>(m_StartScale[m_Active]))*std::exp(exponent);
        outLocalScale[m_Active] = std::copysign(
            static_cast<float>(std::clamp(magnitude,static_cast<double>(MinimumScale),static_cast<double>(MaximumScale))),
            m_StartScale[m_Active]);
        return IsFinite(outLocalScale);
    }

    void EditorScaleGizmo::EndDrag() { m_Active = -1; }

    void EditorScaleGizmo::AddPass(RenderCore::RenderGraphBuilder& builder, const Engine::SceneView& view,
        RenderCore::RenderGraphTextureRef sceneColor) const
    {
        ScaleGeometry geometry;
        if (!BuildGeometry(view,m_Target,geometry)) return;
        std::vector<GizmoDrawing::Triangle> triangles;
        triangles.reserve(48);
        // Connect the world-space axis midpoints, leaving the end markers free.
        // The triangle edges are visual guides,
        // while only the colored stems and markers edit their respective axis.
        for (int axis=0; axis<3; ++axis)
        {
            const int next = (axis+1)%3;
            if (!geometry.bVisible[axis] || !geometry.bVisible[next]) continue;
            auto color = (Colors[axis]+Colors[next])*0.5f;
            color.W = 0.5f;
            GizmoDrawing::Line(triangles,geometry.Midpoints[axis],geometry.Midpoints[next],color,1.5f);
        }
        for (int axis=0; axis<3; ++axis)
        {
            if (!geometry.bVisible[axis] || (geometry.Ends[axis]-geometry.Origin).LengthSquared() < 1) continue;
            auto color = Colors[axis];
            if (axis == (IsDragging() ? m_Active : m_Hovered)) color = {1,1,0,1};
            if (!geometry.bUsable[axis]) color.W = 0.25f;
            GizmoDrawing::Line(triangles,geometry.Origin,geometry.Ends[axis],color);
            Marker(triangles,geometry.Ends[axis],color);
        }
        GizmoDrawing::AddPass(builder,view,sceneColor,triangles,"EditorScaleGizmo");
    }
}
