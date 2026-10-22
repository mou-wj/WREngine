#pragma once

#include <type_traits>
#include "SceneComponent.h"

namespace Engine
{
    // SceneObject represents a single object in the scene. It owns a root
    // SceneComponent reference and exposes the root component through a
    // templated typed accessor.
    class ENGINE_API SceneObject
    {
    public:
        SceneObject();
        explicit SceneObject(SceneComponent* rootComponent);
        ~SceneObject();

        SceneObject(const SceneObject&) = delete;
        SceneObject& operator=(const SceneObject&) = delete;

        SceneObject(SceneObject&&) noexcept = default;
        SceneObject& operator=(SceneObject&&) noexcept = default;

        template<typename T = SceneComponent>
        T* GetRootComponent();

        template<typename T = SceneComponent>
        const T* GetRootComponent() const;

        SceneComponent* GetRootComponentRaw() const;

        void SetRootComponent(SceneComponent* rootComponent);
        void ResetRootComponent();
        bool HasRootComponent() const;

    private:
        SceneComponent* m_RootComponent = nullptr;
    };

    template<typename T>
    T* SceneObject::GetRootComponent()
    {
        static_assert(
            std::is_base_of_v<SceneComponent, T>,
            "T must derive from SceneComponent");

        return m_RootComponent
            ? m_RootComponent->template Cast<T>()
            : nullptr;
    }

    template<typename T>
    const T* SceneObject::GetRootComponent() const
    {
        static_assert(
            std::is_base_of_v<SceneComponent, T>,
            "T must derive from SceneComponent");

        return m_RootComponent
            ? m_RootComponent->template Cast<T>()
            : nullptr;
    }
}
