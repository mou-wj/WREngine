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

    void LightComponent::SetEnabled(bool enabled)
    {
        if (_bEnabled==enabled) return;
        _bEnabled=enabled; MarkRenderStateDirty();
    }

    void LightComponent::DescribeProperties(std::vector<ComponentProperty>& properties)
    {
        const auto first=properties.size();
        SceneComponent::DescribeProperties(properties);
        const auto type=GetLightType();
        properties.erase(std::remove_if(properties.begin()+first,properties.end(),[type](const auto& property) {
            using Semantic=EComponentPropertySemantic;
            return property.Semantic==Semantic::Scale ||
                (type==ELightType::Sky && (property.Semantic!=Semantic::None || property.Id=="Visible")) ||
                (type==ELightType::Point && property.Semantic==Semantic::Rotation);
        }),properties.end());
        for (auto it=properties.begin()+first;it!=properties.end();++it)
            if (it->Id=="Visible") it->Tooltip="Show light range or direction hint; does not change illumination.";
        properties.insert(properties.begin()+first,MakeComponentProperty<bool>("Enabled","Enabled","Node",EComponentPropertyType::Boolean,
            [this] { return IsEnabled(); },[this](bool value) { SetEnabled(value); return true; }));
        properties[first].Tooltip="Enable illumination and shadows; independent of range visibility.";
        const char* names[]={"Directional light","Point light","Spot light","Sky light"};
        const std::string name=names[static_cast<int>(type)];
        using Type=EComponentPropertyType;
        properties.push_back(MakeComponentProperty<std::string>("LightType","Type","Light properties",Type::Text,[name] { return name; }));
        auto color=MakeComponentProperty<Core::Float3>("Color","Color","Light properties",Type::Color3,
            [this] { return GetColor(); },[this](const Core::Float3& value) {
                if (!std::isfinite(value.X)||!std::isfinite(value.Y)||!std::isfinite(value.Z)) return false;
                SetColor(value); return true;
            });
        color.bReadOnly=type==ELightType::Sky;
        if (color.bReadOnly) { color.Write={}; color.Tooltip="Sky color and intensity adjustment is not supported yet."; }
        properties.push_back(std::move(color));
        const auto scalar=[&](const char* id,const char* label,std::function<float()> read,
            std::function<void(float)> write,float minimum,float maximum,float speed) {
            auto property=MakeComponentProperty<float>(id,label,"Light properties",Type::Scalar,std::move(read),
                [write=std::move(write),minimum,maximum](float value) {
                    if (!std::isfinite(value)) return false;
                    write(std::clamp(value,minimum,maximum)); return true;
                });
            property.Minimum=minimum; property.Maximum=maximum; property.Speed=speed;
            properties.push_back(std::move(property));
        };
        scalar("Intensity","Intensity",[this] { return GetIntensity(); },[this](float v) { SetIntensity(v); },0,10000,0.05f);
        properties.back().bReadOnly=type==ELightType::Sky;
        if (type==ELightType::Sky) { properties.back().Write={}; properties.back().Tooltip="Sky color and intensity adjustment is not supported yet."; }
        else properties.push_back(MakeComponentProperty<bool>("CastShadow","Cast shadow","Light properties",Type::Boolean,
            [this] { return IsCastShadow(); },[this](bool value) { SetCastShadow(value); return true; }));
        const auto direction=[&](auto* light) {
            properties.push_back(MakeComponentProperty<Core::Float3>("Direction","Direction","Light properties",Type::Vector3,
                [light] { return light->GetDirection(); },[light](const Core::Float3& value) {
                    if (!std::isfinite(value.X)||!std::isfinite(value.Y)||!std::isfinite(value.Z)||value.LengthSquared()<1e-8f) return false;
                    light->SetDirection(Core::Normalize(value)); return true;
                }));
            properties.back().Speed=0.01f;
        };
        if (auto* light=dynamic_cast<DirectionalLightComponent*>(this)) direction(light);
        if (auto* local=dynamic_cast<LocalLightComponent*>(this))
            scalar("Range","Range (m)",[local] { return local->GetAttenuationRadius(); },
                [local](float v) { local->SetAttenuationRadius(v); },0.01f,10000,0.1f);
        if (auto* spot=dynamic_cast<SpotLightComponent*>(this))
        {
            direction(spot);
            scalar("InnerAngle","Inner angle (deg)",[spot] { return Core::RadToDeg(spot->GetInnerConeAngle()); },
                [spot](float v) { spot->SetInnerConeAngle(std::min(v,Core::RadToDeg(spot->GetOuterConeAngle()))); },
                0,Core::RadToDeg(spot->GetOuterConeAngle()),0.25f);
            scalar("OuterAngle","Outer angle (deg)",[spot] { return Core::RadToDeg(spot->GetOuterConeAngle()); },
                [spot](float v) {
                    spot->SetOuterConeAngle(v);
                    spot->SetInnerConeAngle(std::min(v,Core::RadToDeg(spot->GetInnerConeAngle())));
                },1,89,0.25f);
        }
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
