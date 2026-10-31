#pragma once

#include "SceneComponent.h"
#include "RenderResource.h"

namespace Engine
{
	class LightSceneProxy;
    enum class ELightType
    {
        Directional,
        Point,
        Spot,
        Sky
    };

    class ENGINE_API LightComponent
        : public SceneComponent
    {
    public:
        LightComponent();
        virtual ~LightComponent() = default;

    public:
        virtual ELightType GetLightType()
            const = 0;
        virtual LightSceneProxy* CreateSceneProxy() = 0;
    public:
        void SetColor(
            const Core::Float3& color);

        void SetIntensity(
            float intensity);

        void SetCastShadow(
            bool castShadow);

    public:
        const Core::Float3&
            GetColor() const;

        float GetIntensity()
            const;

        bool IsCastShadow()
            const;

    protected:
        Core::Float3 _color =
            Core::Float3(
                1.0f,
                1.0f,
                1.0f);

        float _intensity =
            1.0f;

        bool _bCastShadow =
            true;
    };

    class ENGINE_API DirectionalLightComponent
        : public LightComponent
    {
    public:
        DirectionalLightComponent();
        LightSceneProxy* CreateSceneProxy() override;
    public:
        ELightType GetLightType()
            const override;

    public:
        void SetDirection(
            const Core::Float3& direction);

        const Core::Float3&
            GetDirection() const;

    protected:
        void OnTransformChanged() override;

        Core::Float3 _direction =
            Core::Float3(
                0.0f,
                -1.0f,
                0.0f);
    };

    class ENGINE_API LocalLightComponent
        : public LightComponent
    {
    public:
        void SetAttenuationRadius(
            float radius);

        float GetAttenuationRadius()
            const;
    protected:
        float _attenuationRadius =
            1000.0f;
    };

    class ENGINE_API PointLightComponent
        : public LocalLightComponent
    {
    public:
        PointLightComponent();
        LightSceneProxy* CreateSceneProxy() override;
    public:
        ELightType GetLightType()
            const override;
    };

    class ENGINE_API SpotLightComponent
        : public LocalLightComponent
    {
    public:
        SpotLightComponent();
        LightSceneProxy* CreateSceneProxy() override;
    public:
        ELightType GetLightType()
            const override;

    public:
        void SetInnerConeAngle(
            float angle);

        void SetOuterConeAngle(
            float angle);

        float GetInnerConeAngle()
            const;

        float GetOuterConeAngle()
            const;

        void SetDirection(
            const Core::Float3& direction);

        const Core::Float3&
            GetDirection() const;

    protected:
        void OnTransformChanged() override;

        float _innerConeAngle =
            Core::DegToRad(20.0f);

        float _outerConeAngle =
            Core::DegToRad(45.0f);

        Core::Float3 _direction =
            Core::Float3(
                0.0f,
                -1.0f,
                0.0f);
    };

    class ENGINE_API SkyLightComponent
        : public LightComponent
    {
    public:
        SkyLightComponent();
        LightSceneProxy* CreateSceneProxy() override;
    public:
        ELightType GetLightType()
            const override;
    
    public:
        void SetDiffuseIrradiance(RenderCore::RenderTexture* diffuseIrradianceMap) {
            _diffuseIrradianceMap = diffuseIrradianceMap ;
        }
        void SetSpecularIrradiance(RenderCore::RenderTexture* specularIrradianceMap) {
            _specularPrefilterMap = specularIrradianceMap;
        }
        RenderCore::RenderTexture* GetDiffuseIrradiance() const { return _diffuseIrradianceMap; }
        RenderCore::RenderTexture* GetSpecularPrefilter() const { return _specularPrefilterMap; }

    protected:
        RenderCore::RenderTexture* _diffuseIrradianceMap;
        RenderCore::RenderTexture* _specularPrefilterMap;

        bool _bPrecomputeDirty = true;
    };

}
