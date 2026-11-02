#include "SceneComponent.h"
#include "Transform.hpp"
#include <algorithm>
#include <cmath>
#include "SceneInterface.h"

namespace Engine
{
    SceneComponent::SceneComponent()
    {
    }

    void SceneComponent::SetVisible(bool visible)
    {
        if (_bVisible==visible) return;
        _bVisible=visible; MarkRenderStateDirty();
    }
    void SceneComponent::DescribeProperties(std::vector<ComponentProperty>& properties)
    {
        using Type=EComponentPropertyType;
        properties.push_back(MakeComponentProperty<bool>("Visible","Visible","Node",Type::Boolean,
            [this] { return IsVisible(); },[this](bool value) { SetVisible(value); return true; }));
        struct TransformProperty
        {
            const char* Label;
            EComponentPropertySemantic Semantic;
            const Core::Float3& (SceneComponent::*Read)() const;
            void (SceneComponent::*Write)(const Core::Float3&);
            float Speed;
        };
        const TransformProperty transforms[] = {
            {"Position",EComponentPropertySemantic::Position,&SceneComponent::GetLocalLocation,&SceneComponent::SetLocalLocation,0.05f},
            {"Rotation",EComponentPropertySemantic::Rotation,&SceneComponent::GetLocalRotation,&SceneComponent::SetLocalRotation,0.5f},
            {"Scale",EComponentPropertySemantic::Scale,&SceneComponent::GetLocalScale,&SceneComponent::SetLocalScale,0.01f}
        };
        for (const auto& transform:transforms)
        {
            auto property=MakeComponentProperty<Core::Float3>(transform.Label,transform.Label,"Local transform",Type::Vector3,
                [this,transform] { return (this->*transform.Read)(); },[this,transform](Core::Float3 value) {
                    for (int axis=0;axis<3;++axis) {
                        if (!std::isfinite(value[axis])) return false;
                        if (transform.Semantic==EComponentPropertySemantic::Scale && std::abs(value[axis])<0.01f)
                            value[axis]=value[axis]<0 ? -0.01f : 0.01f;
                    }
                    (this->*transform.Write)(value); return true;
                });
            property.Semantic=transform.Semantic; property.Speed=transform.Speed;
            properties.push_back(std::move(property));
        }
    }

    SceneComponent::~SceneComponent()
    {
        Detach();

        for (SceneComponent* child : _children)
        {
            child->_parent = nullptr;
        }
    }

    void SceneComponent::AttachTo(
        SceneComponent* parent,
        bool keepWorldTransform)
    {
        if (_parent == parent)
        {
            return;
        }

        Core::Mat4 worldTransform =
            GetComponentTransform();

        Detach(false);

        _parent = parent;

        if (_parent)
        {
            _parent->_children.push_back(this);
        }

        if (keepWorldTransform)
        {
            Core::Mat4 localTransform =
                worldTransform;

            if (_parent)
            {
                localTransform =
                    Core::Inverse(
                        _parent->GetComponentTransform())
                    * worldTransform;
            }

            _localLocation =
                Core::GetTranslation(
                    localTransform);

            _localRotation =
                Core::GetEulerRotation(
                    localTransform);

            _localScale =
                Core::GetScale(
                    localTransform);
        }

        MarkTransformDirty();
    }

    void SceneComponent::Detach(
        bool keepWorldTransform)
    {
        if (!_parent)
        {
            return;
        }

        Core::Mat4 worldTransform =
            GetComponentTransform();

        auto& siblings =
            _parent->_children;

        siblings.erase(
            std::remove(
                siblings.begin(),
                siblings.end(),
                this),
            siblings.end());

        _parent = nullptr;

        if (keepWorldTransform)
        {
            _localLocation =
                Core::GetTranslation(
                    worldTransform);

            _localRotation =
                Core::GetEulerRotation(
                    worldTransform);

            _localScale =
                Core::GetScale(
                    worldTransform);
        }

        MarkTransformDirty();
    }

    SceneComponent*
        SceneComponent::GetAttachParent() const
    {
        return _parent;
    }

    const std::vector<SceneComponent*>&
        SceneComponent::GetAttachChildren() const
    {
        return _children;
    }

    void SceneComponent::SetSceneOwner(SceneInterface* owner)
    {
        _sceneOwner = owner;
    }

    SceneInterface* SceneComponent::GetSceneOwner() const
    {
        if (_sceneOwner)
        {
            return _sceneOwner;
        }

        if (_parent)
        {
            return _parent->GetSceneOwner();
        }

        return nullptr;
    }

    void SceneComponent::SetLocalLocation(
        const Core::Float3& location)
    {
        _localLocation = location;
        MarkTransformDirty();
    }

    void SceneComponent::SetLocalRotation(
        const Core::Float3& rotation)
    {
        _localRotation = rotation;
        MarkTransformDirty();
    }

    void SceneComponent::SetLocalScale(
        const Core::Float3& scale)
    {
        _localScale = scale;
        MarkTransformDirty();
    }

    void SceneComponent::SetWorldLocation(
        const Core::Float3& location)
    {
        if (!_parent)
        {
            _localLocation = location;
        }
        else
        {
            Core::Mat4 parentInverse =
                Core::Inverse(
                    _parent->GetComponentTransform());

            _localLocation =
                Core::TransformPoint(
                    parentInverse,
                    location);
        }

        MarkTransformDirty();
    }

    void SceneComponent::SetWorldRotation(
        const Core::Float3& rotation)
    {
        _localRotation = rotation;
        MarkTransformDirty();
    }

    void SceneComponent::SetWorldScale(
        const Core::Float3& scale)
    {
        _localScale = scale;
        MarkTransformDirty();
    }

    const Core::Float3&
        SceneComponent::GetLocalLocation() const
    {
        return _localLocation;
    }

    const Core::Float3&
        SceneComponent::GetLocalRotation() const
    {
        return _localRotation;
    }

    const Core::Float3&
        SceneComponent::GetLocalScale() const
    {
        return _localScale;
    }

    Core::Float3
        SceneComponent::GetWorldLocation() const
    {
        UpdateTransformIfDirty();

        return Core::GetTranslation(
            _worldTransform);
    }

    Core::Float3
        SceneComponent::GetWorldRotation() const
    {
        UpdateTransformIfDirty();

        return Core::GetEulerRotation(
            _worldTransform);
    }

    Core::Float3
        SceneComponent::GetWorldScale() const
    {
        UpdateTransformIfDirty();

        return Core::GetScale(
            _worldTransform);
    }

    const Core::Mat4&
        SceneComponent::GetComponentTransform() const
    {
        UpdateTransformIfDirty();

        return _worldTransform;
    }

    Core::Float3
        SceneComponent::GetForwardVector() const
    {
        UpdateTransformIfDirty();

        return Core::Normalize(
            Core::TransformVector(
                _worldTransform,
                Core::Float3(
                    1.0f,
                    0.0f,
                    0.0f)));
    }

    Core::Float3
        SceneComponent::GetRightVector()
    {
        UpdateTransformIfDirty();

        return Core::Normalize(
            Core::TransformVector(
                _worldTransform,
                Core::Float3(
                    0.0f,
                    1.0f,
                    0.0f)));
    }

    Core::Float3
        SceneComponent::GetUpVector()
    {
        UpdateTransformIfDirty();

        return Core::Normalize(
            Core::TransformVector(
                _worldTransform,
                Core::Float3(
                    0.0f,
                    0.0f,
                    1.0f)));
    }

    void SceneComponent::MarkRenderStateDirty()
    {
        if (auto* owner = GetSceneOwner())
        {
            owner->NotifyComponentChanged(this);
        }
    }

    void SceneComponent::OnTransformChanged()
    {
        
    }

    void SceneComponent::MarkTransformDirty()
    {
        _bTransformDirty = true;

        for (SceneComponent* child : _children)
        {
            child->MarkTransformDirty();
        }

        OnTransformChanged();
        MarkRenderStateDirty();
    }

    void SceneComponent::UpdateTransformIfDirty() const
    {
        if (!_bTransformDirty)
        {
            return;
        }

        UpdateWorldTransform();
    }

    void SceneComponent::UpdateWorldTransform() const
    {
        Core::Mat4 localTransform =
            Core::MakeTransformMatrix(
                _localLocation,
                _localRotation,
                _localScale);

        if (_parent)
        {
            _worldTransform =
                _parent->GetComponentTransform()
                * localTransform;
        }
        else
        {
            _worldTransform =
                localTransform;
        }

        _bTransformDirty =
            false;
    }
}
