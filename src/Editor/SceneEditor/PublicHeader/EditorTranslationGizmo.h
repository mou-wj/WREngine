#pragma once
#include "Math.hpp"
#include "SceneView.h"
#include <span>
#include <vector>

namespace Engine { class SceneComponent; }

namespace Editor
{
    // Shared screen primitives for the three concrete tools; no GPU ownership.
    namespace GizmoDrawing
    {
        struct Triangle { Core::Float2 A, B, C; Core::Float4 Color; };
        SCENEEDITOR_API bool Project(const Engine::SceneView& view, const Core::Float3& point, Core::Float2& screen);
        SCENEEDITOR_API float WorldLength(const Engine::SceneView& view, const Core::Float3& origin, float pixels = 90.0f);
        SCENEEDITOR_API bool MakeRay(const Core::Float4x4& inverse, const Engine::ViewRect& viewport, bool zeroToOne,
            Core::Float2 position, Core::Float3& origin, Core::Float3& direction);
        SCENEEDITOR_API Core::Float4x4 RotationBasis(const Engine::SceneComponent* target);
        SCENEEDITOR_API void Line(std::vector<Triangle>& triangles, Core::Float2 a, Core::Float2 b, Core::Float4 color, float width = 3.0f);
        SCENEEDITOR_API void AddPass(RenderCore::RenderGraphBuilder& builder, const Engine::SceneView& view,
            RenderCore::RenderGraphTextureRef sceneColor, std::span<const Triangle> triangles, const char* name);
    }
    enum class ETranslationHandle { None, X, Y, Z, XY, XZ, YZ };

    // Borrows a target; its owner must clear it before destroying the component.
    // The host applies Drag's world-space result after its normal resource waits.
    class SCENEEDITOR_API EditorTranslationGizmo
    {
    public:
        void SetTarget(Engine::SceneComponent* target);
        Engine::SceneComponent* GetTarget() const { return m_Target; }
        ETranslationHandle HitTest(const Engine::SceneView& view, Core::Float2 position) const;
        void UpdateHover(const Engine::SceneView& view, Core::Float2 position);
        void ClearHover();
        bool BeginDrag(const Engine::SceneView& view, Core::Float2 position);
        bool Drag(const Engine::SceneView& view, Core::Float2 position, Core::Float3& outWorldLocation) const;
        void EndDrag();
        bool IsDragging() const { return m_Active != ETranslationHandle::None; }
        void AddPass(RenderCore::RenderGraphBuilder& builder, const Engine::SceneView& view,
            RenderCore::RenderGraphTextureRef sceneColor) const;
    private:
        Engine::SceneComponent* m_Target = nullptr;
        ETranslationHandle m_Hovered = ETranslationHandle::None;
        ETranslationHandle m_Active = ETranslationHandle::None;
        Core::Float3 m_StartOrigin;
        Core::Float3 m_StartHit;
        Core::Float3 m_DragNormal;
        Core::Float4x4 m_DragInverseViewProjection;
        Engine::ViewRect m_DragViewport{};
        bool m_bDragDepthZeroToOne = true;
    };

    class SCENEEDITOR_API EditorScaleGizmo
    {
    public:
        void SetTarget(Engine::SceneComponent* target);
        Engine::SceneComponent* GetTarget() const { return m_Target; }
        int HitTest(const Engine::SceneView& view, Core::Float2 position) const;
        void UpdateHover(const Engine::SceneView& view, Core::Float2 position);
        void ClearHover();
        bool BeginDrag(const Engine::SceneView& view, Core::Float2 position);
        bool Drag(const Engine::SceneView& view, Core::Float2 position, Core::Float3& outLocalScale) const;
        void EndDrag();
        bool IsDragging() const { return m_Active >= 0; }
        void AddPass(RenderCore::RenderGraphBuilder& builder, const Engine::SceneView& view,
            RenderCore::RenderGraphTextureRef sceneColor) const;
    private:
        Engine::SceneComponent* m_Target = nullptr;
        int m_Hovered = -1;
        int m_Active = -1;
        Core::Float3 m_StartScale;
        Core::Float2 m_PressPosition;
        Core::Float2 m_DragDirection;
        Engine::ViewRect m_DragViewport{};
    };

    class SCENEEDITOR_API EditorRotationGizmo
    {
    public:
        void SetTarget(Engine::SceneComponent* target);
        Engine::SceneComponent* GetTarget() const { return m_Target; }
        int HitTest(const Engine::SceneView& view, Core::Float2 position) const;
        void UpdateHover(const Engine::SceneView& view, Core::Float2 position);
        void ClearHover();
        bool BeginDrag(const Engine::SceneView& view, Core::Float2 position);
        bool Drag(const Engine::SceneView& view, Core::Float2 position, Core::Float3& outLocalRotation);
        void EndDrag();
        bool IsDragging() const { return m_Active >= 0; }
        float GetAngleRadians() const { return m_Angle; }
        void AddPass(RenderCore::RenderGraphBuilder& builder, const Engine::SceneView& view,
            RenderCore::RenderGraphTextureRef sceneColor) const;
    private:
        Engine::SceneComponent* m_Target = nullptr;
        int m_Hovered = -1;
        int m_Active = -1;
        Core::Float3 m_StartEuler;
        Core::Float4x4 m_StartRotation;
        Core::Float4x4 m_StartBasis;
        Core::Float3 m_Origin;
        Core::Float3 m_StartDirection;
        Core::Float3 m_LastDirection;
        Core::Float2 m_PressPosition;
        Core::Float2 m_ScreenTangent;
        Core::Float4x4 m_DragInverseViewProjection;
        Engine::ViewRect m_DragViewport{};
        float m_Angle = 0;
        float m_Radius = 0;
        bool m_bDepthZeroToOne = true;
        bool m_bScreenDrag = false;
    };
}
