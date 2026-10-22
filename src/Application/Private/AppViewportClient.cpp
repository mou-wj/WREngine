#include "AppViewportClient.h"
#include "Module.h"
#include "RenderModule.h"
#include "Math.hpp"
#include "EngineGlobal.h"
namespace App {
    using namespace Engine;
    using namespace Core;
    using namespace RenderCore;

    AppViewportClient::AppViewportClient() = default;
    AppViewportClient::~AppViewportClient() = default;

    void AppViewportClient::InitResources()
    {
        AssetManager::Get().LoadSync<MaterialAsset>(Core::GetProjectDir() + "/resources/material/DefaultWhite/material.json");
        auto skyAsset = AssetManager::Get().LoadSync<SkyLightAsset>(Core::GetProjectDir() + "/resources/pic/DaySkyHDRI046A_1K-TONEMAPPED.jpg");
        m_StaticMeshAsset = AssetManager::Get().LoadSync<StaticMeshAsset>(Core::GetProjectDir() + "/resources/glb/sphere.glb");
        m_StaticMeshComponent = new StaticMeshComponent();
        m_StaticMeshComponent->SetStaticMesh(m_StaticMeshAsset->GetMesh());
        m_StaticMeshComponent2 = new StaticMeshComponent();
        m_StaticMeshComponent2->SetStaticMesh(m_StaticMeshAsset->GetMesh());
        m_Scene = GetRenderModuleInstance()->AllocateScene();
        m_Scene->AddPrimitive(m_StaticMeshComponent);
        m_StaticMeshComponent2->SetWorldLocation({ 3.0f, 3.0f, 3.0f });
        m_Scene->AddPrimitive(m_StaticMeshComponent2);

        // Create Directional Light
        m_DirectionalLight = new DirectionalLightComponent();
        m_DirectionalLight->SetWorldLocation({ 5.0f, 5.0f, 5.0f });
        m_DirectionalLight->SetDirection({ -1.0f, -1.0f, -1.0f });
        m_DirectionalLight->SetColor({ 1.0f, 1.0f, 1.0f });
        m_DirectionalLight->SetIntensity(1.0f);
        m_DirectionalLight->SetCastShadow(true);
        m_Scene->AddLight(m_DirectionalLight);

        // Create Point Light
        m_PointLight = new PointLightComponent();
        m_PointLight->SetWorldLocation({ 5.0f, 4.0f, 5.0f });
        m_PointLight->SetColor({ 1.0f, 0.5f, 0.2f });
        m_PointLight->SetIntensity(1.0f);
        m_PointLight->SetAttenuationRadius(10.0f);
        m_PointLight->SetCastShadow(true);
        m_Scene->AddLight(m_PointLight);

        // Create Spot Light
        m_SpotLight = new SpotLightComponent();
        m_SpotLight->SetWorldLocation({ 3.0f, 2.0f, 0.0f });
        m_SpotLight->SetDirection({ -1.0f, -0.5f, 0.0f });
        m_SpotLight->SetColor({ 0.2f, 0.5f, 1.0f });
        m_SpotLight->SetIntensity(1.0f);
        m_SpotLight->SetAttenuationRadius(15.0f);
        m_SpotLight->SetInnerConeAngle(20.0f);
        m_SpotLight->SetOuterConeAngle(45.0f);
        m_SpotLight->SetCastShadow(true);
        m_Scene->AddLight(m_SpotLight);

        // Create Sky Light
        m_SkyLight = new SkyLightComponent();
        m_SkyLight->SetDiffuseIrradiance(skyAsset->GetDiffuseIrradiance());
        m_SkyLight->SetSpecularIrradiance(skyAsset->GetSpecularPrefilter());
        m_Scene->AddLight(m_SkyLight);

        m_Scene->FlushPendingUpdates();

        m_Camera.SetPosition({ 2,2,2 });
        m_Camera.SetTarget({ 0.0f, 0.0f, 0.0f });
        m_Camera.SetUp({ 0.0f, 1.0f, 0.0f });
        m_Camera.SetPerspective(Core::DegToRad(45.0f), 1, 0.1f, 1000.0f);
    }

    void AppViewportClient::ReleaseResources()
    {
		if (m_Scene) {
			m_Scene->RemovePrimitive(m_StaticMeshComponent);
            m_Scene->RemovePrimitive(m_StaticMeshComponent2);
            // Remove lights from scene
            if (m_DirectionalLight) {
                m_Scene->RemoveLight(m_DirectionalLight);
            }
            if (m_PointLight) {
                m_Scene->RemoveLight(m_PointLight);
            }
            if (m_SpotLight) {
                m_Scene->RemoveLight(m_SpotLight);
            }
            if (m_SkyLight) {
                m_Scene->RemoveLight(m_SkyLight);
            }

            m_Scene->FlushPendingUpdates();
		}
		delete m_StaticMeshComponent;
		m_StaticMeshComponent = nullptr;
		delete m_StaticMeshComponent2;
		m_StaticMeshComponent2 = nullptr;
        
        // Delete lights
        delete m_DirectionalLight;
        m_DirectionalLight = nullptr;
        delete m_PointLight;
        m_PointLight = nullptr;
        delete m_SpotLight;
        m_SpotLight = nullptr;

		m_StaticMeshAsset.reset();
        delete m_Scene;
		m_Scene = nullptr;
        AssetManager::Get().Clear();
    }

    void AppViewportClient::Draw(Viewport* inViewport)
    {
        if (!inViewport)
            return;

        // 构建场景视图集合
        ;
        m_Family.RenderTarget = inViewport;
        BuildSceneViews(inViewport, m_Family);

        // 提交渲染模块执行绘制
        GetRenderModuleInstance()->BeginRender(&m_Family);
    }

    void AppViewportClient::BuildSceneViews(Engine::Viewport* inViewport, Engine::SceneViewFamily& outViews)
    {

        auto view = m_Camera.GetViewMatrix();
        auto proj = m_Camera.GetProjectionMatrix();
		auto vp = view * proj;
        ;
        // 填充场景视图参数
        SceneView sceneView;
        sceneView.CameraWorldPos = m_Camera.GetPosition();
        sceneView.NearClip = m_Camera.GetNearPlane();
        sceneView.FarClip = m_Camera.GetFarPlane();
        sceneView.ViewMatrix = view;
        sceneView.ProjectionMatrix = proj;
        sceneView.ViewProjectionMatrix = proj * view;
		sceneView.InvViewProjectionMatrix = Core::Inverse(sceneView.ViewProjectionMatrix);
        sceneView.bDepthRangeZeroToOne = m_Camera.GetDepthRangeMode() == Camera::ZeroToOne;
        Float4 testPos = { -4.89f, 0.0f, 0.0f, 1.0f };
        Float4 viewProjPos = sceneView.ViewProjectionMatrix * testPos.Data;
        Float4 inverseViewProjPos = sceneView.InvViewProjectionMatrix * viewProjPos.Data;

        sceneView.Viewport.Width = inViewport->GetWidth();
        sceneView.Viewport.Height = inViewport->GetHeight();
        m_Family.Scene = m_Scene;
        m_Family.ClearViews();
        m_Family.AddView(sceneView);
        m_Family.BuildAllViews();
    }
    bool AppViewportClient::
        OnMouseButton(
            const SlateCore::MouseButtonEvent&
            event)
    {
        const bool pressed =
            event.Event ==
            SlateCore::EInputEvent
            ::Pressed;

        switch (event.Button)
        {
            case SlateCore::EMouseButton
            ::Right:
            {
                m_bRightMouseDown =
                    pressed;

                return true;
            }

            case SlateCore::EMouseButton
            ::Middle:
            {
                m_bMiddleMouseDown =
                    pressed;

                return true;
            }
        }

        return false;
    }

    bool AppViewportClient::
        OnMouseMove(
            const SlateCore::MouseMoveEvent&
            event)
    {
        constexpr float
            rotateSpeed =
            0.005f;

        constexpr float
            panSpeed =
            0.005f;

        if (m_bRightMouseDown)
        {
            m_Yaw +=
                event.DeltaX *
                rotateSpeed;

            m_Pitch -=
                event.DeltaY *
                rotateSpeed;

            m_Pitch =
                std::clamp(
                    m_Pitch,
                    -1.5f,
                    1.5f);

            UpdateCameraTransform();

            return true;
        }
        
        if (m_bMiddleMouseDown)
        {
            auto position =
                m_Camera.GetPosition();

            auto target =
                m_Camera.GetTarget();

            Float3
                forward =
                Core::Normalize(
                    target -
                    position);

            const auto
                right =
                Core::Normalize(
                    Core::Cross(
                        forward,
                        m_Camera.GetUp()));

            const auto
                up =
                Core::Normalize(
                    Core::Cross(
                        right,
                        forward));

            const auto offset =
                (-right *
                    event.DeltaX +
                    up *
                    event.DeltaY)
                * panSpeed *
                m_CameraDistance;

            position +=
                offset;

            target +=
                offset;

            m_Camera.SetPosition(
                position);

            m_Camera.SetTarget(
                target);

            return true;
        }
        /**/
        return false;
    }
    bool AppViewportClient::
        OnMouseWheel(
            const SlateCore::MouseWheelEvent&
            event)
    {
        constexpr float
            zoomSpeed =
            0.25f;

        m_CameraDistance -=
            event.Delta *
            zoomSpeed;

        m_CameraDistance =
            CORE_MAX(
                m_CameraDistance,
                0.1f);

        UpdateCameraTransform();

        return true;
    }
    void AppViewportClient::
        UpdateCameraTransform()
    {
        const float cosPitch =
            std::cos(m_Pitch);

        Core::Float3 forward;
		forward = m_Camera.GetTarget() - m_Camera.GetPosition();

        
        forward.X =
            std::cos(m_Yaw) *
            cosPitch;
        
        forward.Y =
            std::sin(m_Pitch);
        
        forward.Z =
            std::sin(m_Yaw) *
            cosPitch;
        
        forward =
            Core::Normalize(
                forward);

        const auto target =
            m_Camera.GetTarget();

        const auto position =
            target -
            forward *
            m_CameraDistance;

        m_Camera.SetPosition(
            position);

        //camera.SetUp(
        //    { 0.0f, 1.0f, 0.0f });
        //camera.SetPerspective(Core::DegToRad(45.0f), 1, 0.1f, 1000.0f);
        //Yaw = Core::DegToRad(90.0f);
        //Pitch = 0;
    }



}