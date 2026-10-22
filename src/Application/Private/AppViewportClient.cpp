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
        staticMeshAsset = AssetManager::Get().LoadSync<StaticMeshAsset>(Core::GetProjectDir() + "/resources/glb/sphere.glb");
        staticMeshComponent = new StaticMeshComponent();
        staticMeshComponent->SetStaticMesh(staticMeshAsset->GetMesh());
        staticMeshComponent2 = new StaticMeshComponent();
        staticMeshComponent2->SetStaticMesh(staticMeshAsset->GetMesh());
        scene = GetRenderModuleInstance()->AllocateScene();
        scene->AddPrimitive(staticMeshComponent);
        staticMeshComponent2->SetWorldLocation({ 3.0f, 3.0f, 3.0f });
        scene->AddPrimitive(staticMeshComponent2);

        // Create Directional Light
        directionalLight = new DirectionalLightComponent();
        directionalLight->SetWorldLocation({ 5.0f, 5.0f, 5.0f });
        directionalLight->SetDirection({ -1.0f, -1.0f, -1.0f });
        directionalLight->SetColor({ 1.0f, 1.0f, 1.0f });
        directionalLight->SetIntensity(1.0f);
        directionalLight->SetCastShadow(true);
        scene->AddLight(directionalLight);

        // Create Point Light
        pointLight = new PointLightComponent();
        pointLight->SetWorldLocation({ 5.0f, 4.0f, 5.0f });
        pointLight->SetColor({ 1.0f, 0.5f, 0.2f });
        pointLight->SetIntensity(1.0f);
        pointLight->SetAttenuationRadius(10.0f);
        pointLight->SetCastShadow(true);
        scene->AddLight(pointLight);

        // Create Spot Light
        spotLight = new SpotLightComponent();
        spotLight->SetWorldLocation({ 3.0f, 2.0f, 0.0f });
        spotLight->SetDirection({ -1.0f, -0.5f, 0.0f });
        spotLight->SetColor({ 0.2f, 0.5f, 1.0f });
        spotLight->SetIntensity(1.0f);
        spotLight->SetAttenuationRadius(15.0f);
        spotLight->SetInnerConeAngle(20.0f);
        spotLight->SetOuterConeAngle(45.0f);
        spotLight->SetCastShadow(true);
        scene->AddLight(spotLight);

        // Create Sky Light
        skyLight = new SkyLightComponent();
        skyLight->SetDiffuseIrradiance(skyAsset->GetDiffuseIrradiance());
        skyLight->SetSpecularIrradiance(skyAsset->GetSpecularPrefilter());
        scene->AddLight(skyLight);

        scene->FlushPendingUpdates();

        camera.SetPosition({ 2,2,2 });
        camera.SetTarget({ 0.0f, 0.0f, 0.0f });
        camera.SetUp({ 0.0f, 1.0f, 0.0f });
        camera.SetPerspective(Core::DegToRad(45.0f), 1, 0.1f, 1000.0f);
    }

    void AppViewportClient::ReleaseResources()
    {
		if (scene) {
			scene->RemovePrimitive(staticMeshComponent);
            scene->RemovePrimitive(staticMeshComponent2);
            // Remove lights from scene
            if (directionalLight) {
                scene->RemoveLight(directionalLight);
            }
            if (pointLight) {
                scene->RemoveLight(pointLight);
            }
            if (spotLight) {
                scene->RemoveLight(spotLight);
            }
            if (skyLight) {
                scene->RemoveLight(skyLight);
            }

            scene->FlushPendingUpdates();
		}
		delete staticMeshComponent;
		staticMeshComponent = nullptr;
		delete staticMeshComponent2;
		staticMeshComponent2 = nullptr;
        
        // Delete lights
        delete directionalLight;
        directionalLight = nullptr;
        delete pointLight;
        pointLight = nullptr;
        delete spotLight;
        spotLight = nullptr;

		staticMeshAsset.reset();
        delete scene;
		scene = nullptr;
        AssetManager::Get().Clear();
    }

    void AppViewportClient::Draw(Viewport* InViewport)
    {
        if (!InViewport)
            return;

        // ���� SceneViewCollection
        ;
        family.RenderTarget = InViewport;
        BuildSceneViews(InViewport, family);

        // ���� RenderInterface ִ����Ⱦ
        GetRenderModuleInstance()->BeginRender(&family);
    }

    void AppViewportClient::BuildSceneViews(Engine::Viewport* InViewport, Engine::SceneViewFamily& OutViews)
    {

        auto view = camera.GetViewMatrix();
        auto proj = camera.GetProjectionMatrix();
		auto vp = view * proj;
        ;
        //���Ƴ���
        SceneView sceneView;
        sceneView.CameraWorldPos = camera.GetPosition();
        sceneView.NearClip = camera.GetNearPlane();
        sceneView.FarClip = camera.GetFarPlane();
        sceneView.ViewMatrix = view;
        sceneView.ProjectionMatrix = proj;
        sceneView.ViewProjectionMatrix = proj * view;
		sceneView.InvViewProjectionMatrix = Core::Inverse(sceneView.ViewProjectionMatrix);
        sceneView.bDepthRangeZeroToOne = camera.GetDepthRangeMode() == Camera::ZeroToOne;
        Float4 testPos = { -4.89f, 0.0f, 0.0f, 1.0f };
        Float4 viewProjPos = sceneView.ViewProjectionMatrix * testPos.Data;
        Float4 inverseViewProjPos = sceneView.InvViewProjectionMatrix * viewProjPos.Data;

        sceneView.Viewport.Width = InViewport->GetWidth();
        sceneView.Viewport.Height = InViewport->GetHeight();
        family.Scene = scene;
        family.ClearViews();
        family.AddView(sceneView);
        family.BuildAllViews();
    }
    bool AppViewportClient::
        OnMouseButton(
            const SlateCore::MouseButtonEvent&
            Event)
    {
        const bool pressed =
            Event.Event ==
            SlateCore::EInputEvent
            ::Pressed;

        switch (Event.Button)
        {
            case SlateCore::EMouseButton
            ::Right:
            {
                bRightMouseDown =
                    pressed;

                return true;
            }

            case SlateCore::EMouseButton
            ::Middle:
            {
                bMiddleMouseDown =
                    pressed;

                return true;
            }
        }

        return false;
    }

    bool AppViewportClient::
        OnMouseMove(
            const SlateCore::MouseMoveEvent&
            Event)
    {
        constexpr float
            RotateSpeed =
            0.005f;

        constexpr float
            PanSpeed =
            0.005f;

        if (bRightMouseDown)
        {
            Yaw +=
                Event.DeltaX *
                RotateSpeed;

            Pitch -=
                Event.DeltaY *
                RotateSpeed;

            Pitch =
                std::clamp(
                    Pitch,
                    -1.5f,
                    1.5f);

            UpdateCameraTransform();

            return true;
        }
        
        if (bMiddleMouseDown)
        {
            auto position =
                camera.GetPosition();

            auto target =
                camera.GetTarget();

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
                        camera.GetUp()));

            const auto
                up =
                Core::Normalize(
                    Core::Cross(
                        right,
                        forward));

            const auto offset =
                (-right *
                    Event.DeltaX +
                    up *
                    Event.DeltaY)
                * PanSpeed *
                CameraDistance;

            position +=
                offset;

            target +=
                offset;

            camera.SetPosition(
                position);

            camera.SetTarget(
                target);

            return true;
        }
        /**/
        return false;
    }
    bool AppViewportClient::
        OnMouseWheel(
            const SlateCore::MouseWheelEvent&
            Event)
    {
        constexpr float
            ZoomSpeed =
            0.25f;

        CameraDistance -=
            Event.Delta *
            ZoomSpeed;

        CameraDistance =
            CORE_MAX(
                CameraDistance,
                0.1f);

        UpdateCameraTransform();

        return true;
    }
    void AppViewportClient::
        UpdateCameraTransform()
    {
        const float cosPitch =
            std::cos(Pitch);

        Core::Float3 forward;
		forward = camera.GetTarget() - camera.GetPosition();

        
        forward.x =
            std::cos(Yaw) *
            cosPitch;
        
        forward.y =
            std::sin(Pitch);
        
        forward.z =
            std::sin(Yaw) *
            cosPitch;
        
        forward =
            Core::Normalize(
                forward);

        const auto target =
            camera.GetTarget();

        const auto position =
            target -
            forward *
            CameraDistance;

        camera.SetPosition(
            position);

        //camera.SetUp(
        //    { 0.0f, 1.0f, 0.0f });
        //camera.SetPerspective(Core::DegToRad(45.0f), 1, 0.1f, 1000.0f);
        //Yaw = Core::DegToRad(90.0f);
        //Pitch = 0;
    }



}