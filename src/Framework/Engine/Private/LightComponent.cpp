#include "LightComponent.h"
#include "LightSceneProxy.h"
#include "RenderResource.h"
#include "Transform.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace Engine
{
    namespace
    {
        void SetLightDirection(SceneComponent& component, const Core::Float3& direction)
        {
            auto local = direction;
            if (auto* parent = component.GetAttachParent())
                local = Core::TransformVector(Core::Inverse(parent->GetComponentTransform()),local);
            if (!std::isfinite(local.X) || !std::isfinite(local.Y) || !std::isfinite(local.Z) || local.LengthSquared()<1e-8f)
                throw std::invalid_argument("Light direction must be finite and nonzero with an invertible parent transform");
            local = Core::Normalize(local);
            // SceneComponent's forward is +X; Rz*Ry*Rx maps it to
            // (cos(z)*cos(y), sin(z)*cos(y), -sin(y)). Roll is unconstrained.
            component.SetLocalRotation({0,Core::RadToDeg(-std::asin(std::clamp(local.Z,-1.0f,1.0f))),
                Core::RadToDeg(std::atan2(local.Y,local.X))});
        }
    }

    LightComponent::LightComponent()
    {
    }

    void LightComponent::SetColor(
        const Core::Float3& color)
    {
        _color = color;

        MarkRenderStateDirty();
    }

    void LightComponent::SetIntensity(
        float intensity)
    {
        _intensity = intensity;

        MarkRenderStateDirty();
    }

    void LightComponent::SetCastShadow(
        bool castShadow)
    {
        _bCastShadow = castShadow;

        MarkRenderStateDirty();
    }

    const Core::Float3&
        LightComponent::GetColor() const
    {
        return _color;
    }

    float LightComponent::GetIntensity() const
    {
        return _intensity;
    }

    bool LightComponent::IsCastShadow() const
    {
        return _bCastShadow;
    }

    DirectionalLightComponent::
        DirectionalLightComponent()
    {
        SetDirection(_direction);
    }

    LightSceneProxy* DirectionalLightComponent::CreateSceneProxy()
    {
        return new DirectionalLightSceneProxy(this);
    }

    ELightType
        DirectionalLightComponent::
        GetLightType() const
    {
        return ELightType::Directional;
    }

    void DirectionalLightComponent::
        SetDirection(
            const Core::Float3& direction)
    {
        SetLightDirection(*this,direction);
    }

    void DirectionalLightComponent::OnTransformChanged()
    {
        LightComponent::OnTransformChanged();
        _direction = GetForwardVector();
    }

    const Core::Float3&
        DirectionalLightComponent::
        GetDirection() const
    {
        return _direction;
    }

    void LocalLightComponent::
        SetAttenuationRadius(
            float radius)
    {
        _attenuationRadius = radius;

        MarkRenderStateDirty();
    }

    float LocalLightComponent::
        GetAttenuationRadius() const
    {
        return _attenuationRadius;
    }


    PointLightComponent::
        PointLightComponent()
    {
    }

    LightSceneProxy* PointLightComponent::CreateSceneProxy()
    {
        return new PointLightSceneProxy(this);
    }

    ELightType
        PointLightComponent::
        GetLightType() const
    {
        return ELightType::Point;
    }

    SpotLightComponent::
        SpotLightComponent()
    {
        SetDirection(_direction);
    }

    LightSceneProxy* SpotLightComponent::CreateSceneProxy()
    {
        return new SpotLightSceneProxy(this);
    }

    ELightType
        SpotLightComponent::
        GetLightType() const
    {
        return ELightType::Spot;
    }

    void SpotLightComponent::
        SetInnerConeAngle(
            float angle)
    {
        _innerConeAngle = Core::DegToRad(angle);

        MarkRenderStateDirty();
    }

    void SpotLightComponent::
        SetOuterConeAngle(
            float angle)
    {
        _outerConeAngle = Core::DegToRad(angle);

        MarkRenderStateDirty();
    }

    float SpotLightComponent::
        GetInnerConeAngle() const
    {
        return _innerConeAngle;
    }

    float SpotLightComponent::
        GetOuterConeAngle() const
    {
        return _outerConeAngle;
    }

    void SpotLightComponent::
        SetDirection(
            const Core::Float3& direction)
    {
        SetLightDirection(*this,direction);
    }

    void SpotLightComponent::OnTransformChanged()
    {
        LightComponent::OnTransformChanged();
        _direction = GetForwardVector();
    }

    const Core::Float3&
        SpotLightComponent::
        GetDirection() const
    {
        return _direction;
    }

    SkyLightComponent::
        SkyLightComponent()
    {
    }


    LightSceneProxy* SkyLightComponent::CreateSceneProxy()
    {
        return new SkyLightSceneProxy(this);
    }

    ELightType
        SkyLightComponent::
        GetLightType() const
    {
        return ELightType::Sky;
    }

}
