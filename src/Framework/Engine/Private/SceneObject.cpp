#include "SceneObject.h"

namespace Engine
{
    SceneObject::SceneObject() = default;

    SceneObject::SceneObject(SceneComponent* rootComponent)
        : m_RootComponent(rootComponent)
    {
    }

    SceneObject::~SceneObject() = default;

    SceneComponent* SceneObject::GetRootComponentRaw() const
    {
        return m_RootComponent;
    }

    void SceneObject::SetRootComponent(SceneComponent* rootComponent)
    {
        m_RootComponent = rootComponent;
    }

    void SceneObject::ResetRootComponent()
    {
        m_RootComponent = nullptr;
    }

    bool SceneObject::HasRootComponent() const
    {
        return m_RootComponent != nullptr;
    }
}
