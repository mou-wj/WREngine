#pragma once
#include "Widgets/ImCommonWidgets.h"
#include <functional>
namespace Engine { class SceneComponent; }
namespace Editor
{
    // Borrow the current node only for this UI call; the scene owns it.
    class NODEEDITOR_API EditorNodePanel final : public ImGUISlate::ImPanelWidget
    {
    public:
        EditorNodePanel(std::function<Engine::SceneComponent*()> getSelected, std::function<void()> onChanged);
        void SetTranslationGizmoCallbacks(std::function<bool()> isEnabled, std::function<void(bool)> setEnabled);
        void SetRotationGizmoCallbacks(std::function<bool()> isEnabled, std::function<void(bool)> setEnabled);
        void SetScaleGizmoCallbacks(std::function<bool()> isEnabled, std::function<void(bool)> setEnabled);
        void Draw() override;
        bool HitTest(float x, float y) const override;
    protected:
        void DrawContent() override;
    private:
        std::function<Engine::SceneComponent*()> m_GetSelected;
        std::function<void()> m_OnChanged;
        std::function<bool()> m_IsTranslationGizmoEnabled;
        std::function<void(bool)> m_SetTranslationGizmoEnabled;
        std::function<bool()> m_IsRotationGizmoEnabled;
        std::function<void(bool)> m_SetRotationGizmoEnabled;
        std::function<bool()> m_IsScaleGizmoEnabled;
        std::function<void(bool)> m_SetScaleGizmoEnabled;
    };
}
