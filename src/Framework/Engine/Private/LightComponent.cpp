#include "LightComponent.h"
#include "LightSceneProxy.h"
#include "RenderResource.h"
namespace Engine
{

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
        _direction = direction;

        MarkRenderStateDirty();
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
        _direction = direction;

        MarkRenderStateDirty();
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