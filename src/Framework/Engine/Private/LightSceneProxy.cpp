#include "LightSceneProxy.h"
#include "Transform.hpp"

namespace Engine
{

    LightSceneProxy::
        LightSceneProxy(
            const LightComponent* component) : _component(component)
    {
        
    }

    void LightSceneProxy::
        UpdateFromComponent()
    {
        if (_position != _component->GetWorldLocation()) {
            bUpdateCastShadow = true;
        }
        _position =
            _component->GetWorldLocation();
        _color =
            _component->GetColor();

        _intensity =
            _component->GetIntensity();

        _bCastShadow =
            _component->IsCastShadow();
    }

    const Core::Float3&
        LightSceneProxy::
        GetPosition() const
    {
        return _position;
    }


    const Core::Float3&
        LightSceneProxy::
        GetColor() const
    {
        return _color;
    }

    float LightSceneProxy::
        GetIntensity() const
    {
        return _intensity;
    }

    bool LightSceneProxy::
        IsCastShadow() const
    {
        return _bCastShadow;
    }
    DirectionalLightSceneProxy::
        DirectionalLightSceneProxy(
            const DirectionalLightComponent*
            component)
        :
        LightSceneProxy(component)
    {
        UpdateFromComponent();
    }

    void DirectionalLightSceneProxy::
        UpdateFromComponent()
    {
        LightSceneProxy::UpdateFromComponent();

        auto dir = static_cast<const DirectionalLightComponent*>(_component);
        if (dir)
        {
            if (_direction != dir->GetDirection()) {
                bUpdateCastShadow = true;
            }
            _direction = dir->GetDirection();
        }
    }

    const Core::Float3& DirectionalLightSceneProxy::GetDirection() const
    {
        return _direction;
    }

    ELightType
        DirectionalLightSceneProxy::
        GetLightType() const
    {
        return ELightType::Directional;
    }

    LocalLightSceneProxy::
        LocalLightSceneProxy(
            const LocalLightComponent*
            component)
        :
        LightSceneProxy(component)
    {
        _attenuationRadius =
            component->GetAttenuationRadius();
    }

    void LocalLightSceneProxy::
        UpdateFromComponent()
    {
        LightSceneProxy::
            UpdateFromComponent();

        auto localLight =
            static_cast<
            const LocalLightComponent*>(
                _component);

        _attenuationRadius =
            localLight->
            GetAttenuationRadius();
    }

    float LocalLightSceneProxy::
        GetAttenuationRadius()
        const
    {
        return _attenuationRadius;
    }

    PointLightSceneProxy::
        PointLightSceneProxy(
            const PointLightComponent*
            component)
        :
        LocalLightSceneProxy(
            component)
    {
        UpdateFromComponent();
    }

    ELightType
        PointLightSceneProxy::
        GetLightType() const
    {
        return ELightType::Point;
    }

    SpotLightSceneProxy::
        SpotLightSceneProxy(
            const SpotLightComponent*
            component)
        :
        LocalLightSceneProxy(
            component)
    {

        _innerConeAngle =
            component->
            GetInnerConeAngle();

        _outerConeAngle =
            component->
            GetOuterConeAngle();
        if(_direction != component->GetDirection()) {
            bUpdateCastShadow = true;
        }
        _direction = component->GetDirection();
        UpdateFromComponent();
    }

    void SpotLightSceneProxy::
        UpdateFromComponent()
    {
        LocalLightSceneProxy::
            UpdateFromComponent();

        auto spot =
            static_cast<
            const SpotLightComponent*>(
                _component);

        _innerConeAngle =
            spot->GetInnerConeAngle();

        _outerConeAngle =
            spot->GetOuterConeAngle();
        _direction = spot->GetDirection();
    }

    const Core::Float3& SpotLightSceneProxy::GetDirection() const
    {
        return _direction;
    }

    ELightType
        SpotLightSceneProxy::
        GetLightType() const
    {
        return ELightType::Spot;
    }

    float SpotLightSceneProxy::
        GetInnerConeAngle()
        const
    {
        return _innerConeAngle;
    }

    float SpotLightSceneProxy::
        GetOuterConeAngle()
        const
    {
        return _outerConeAngle;
    }

    SkyLightSceneProxy::
        SkyLightSceneProxy(
            const SkyLightComponent*
            component)
        :
        LightSceneProxy(component)
    {
        UpdateFromComponent();
    }

    void SkyLightSceneProxy::UpdateFromComponent()
    {
        LightSceneProxy::UpdateFromComponent();

        auto sky = static_cast<const SkyLightComponent*>(_component);
        if (sky)
        {
            _diffuseIrradianceMap = sky->GetDiffuseIrradiance();
            _specularPrefilterMap = sky->GetSpecularPrefilter();
        }
    }

    ELightType
        SkyLightSceneProxy::
        GetLightType() const
    {
        return ELightType::Sky;
    }

}