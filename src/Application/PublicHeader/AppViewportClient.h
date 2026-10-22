#pragma once
#include "SceneView.h"
#include "Viewport.h"
#include "ViewportClient.h"
#include "Camera.h"
#include "SceneInterface.h"
#include "AssetManager.h"
#include "StaticMeshComponent.h"
#include "SceneView.h"
#include "LightComponent.h"
namespace App {

    class APPLICATION_API AppViewportClient : public Engine::ViewportClient
    {
    public:
        AppViewportClient();
        virtual ~AppViewportClient();
        void InitResources();
        void ReleaseResources();

        // ViewportClient 绘制接口
        void Draw(Engine::Viewport* inViewport) override;
    public:
        bool OnMouseButton(
            const SlateCore::MouseButtonEvent&
            event) override;

        bool OnMouseMove(
            const SlateCore::MouseMoveEvent&
            event) override;

        bool OnMouseWheel(
            const SlateCore::MouseWheelEvent&
            event) override;
    private:
        void BuildSceneViews(Engine::Viewport* inViewport, Engine::SceneViewFamily& outViews);
        void UpdateCameraTransform();
    private:
        bool m_bRightMouseDown =
            false;

        bool m_bMiddleMouseDown =
            false;

        float m_CameraDistance =
            5.0f;

        float m_Yaw =
            Core::DegToRad(90.0f);

        float m_Pitch =
            0.0f;

    private:
        Engine::Camera m_Camera;
        Engine::SceneInterface* m_Scene;
        std::shared_ptr<Engine::StaticMeshAsset> m_StaticMeshAsset;
        Engine::StaticMeshComponent* m_StaticMeshComponent;
        Engine::StaticMeshComponent* m_StaticMeshComponent2;
        Engine::SceneViewFamily m_Family;

        Engine::DirectionalLightComponent* m_DirectionalLight;
        Engine::PointLightComponent* m_PointLight;
        Engine::SpotLightComponent* m_SpotLight;
        Engine::SkyLightComponent* m_SkyLight;
    };


}