// PrimitiveComponent.cpp
// Lightweight implementation examples and RenderCommand stubs.

#include "PrimitiveComponent.h"
#include <functional>
#include <utility>
namespace Engine {
    // PrimitiveComponent implementation
    PrimitiveComponent::PrimitiveComponent()
        : _localTransform(Core::Mat4::Identity()), _worldTransform(Core::Mat4::Identity()), _bounds(), _mobility(EMobility::Static),
        _bVisible(true), _pendingSceneProxy(nullptr), _debugName(nullptr) {
    }

    PrimitiveComponent::~PrimitiveComponent() {
        // Ensure scene proxy is destroyed on RenderThread if still present
        if (_pendingSceneProxy) {
            DestroySceneProxy(_pendingSceneProxy);
            _pendingSceneProxy = nullptr;
        }
        // Note: do NOT call OwnerScene removal here — caller should explicitly Unregister
    }

    void PrimitiveComponent::RegisterComponentWithScene(SceneInterface* scene) {
        OwnerScene = scene;
        // Scene is expected to perform bookkeeping on GameThread. Optionally, create proxy now.
        PrimitiveSceneProxy* proxy = CreateSceneProxy();
        if (proxy) {
            _pendingSceneProxy = proxy; // bookkeeping on GameThread
            // Transfer ownership to RenderThread / Scene via a render command.
            //RenderCommand::Enqueue([Scene, Proxy]() {
            //    // In real engine: Scene->OnAddPrimitive_RenderThread(Proxy);
            //    (void)Scene; (void)Proxy; // placeholder
            //});
        }
    }

    void PrimitiveComponent::UnregisterComponentFromScene() {
        // Caller must ensure GameThread context.
        OwnerScene = nullptr;
        if (_pendingSceneProxy) {
            PrimitiveSceneProxy* proxy = _pendingSceneProxy;
            _pendingSceneProxy = nullptr;
            DestroySceneProxy(proxy);
        }
    }

    PrimitiveSceneProxy* PrimitiveComponent::CreateSceneProxy() const {
        // Default: no proxy. Derived classes should allocate and return a heap object
        // which contains a read-only snapshot of the primitive's state.
        return nullptr;
    }

    void PrimitiveComponent::DestroySceneProxy(PrimitiveSceneProxy* proxy) const {
        if (!proxy) return;
        // Schedule deletion on the RenderThread.
        //RenderCommand::Enqueue([Proxy]() {
        //    delete Proxy;
        //});
    }

    void PrimitiveComponent::MarkRenderStateDirty() {
        // Example: ask the Scene/RenderThread to recreate or update the proxy.
        if (!OwnerScene) return;
        //RenderCommand::Enqueue([this]() {
        //    // In real engine: OwnerScene->MarkPrimitiveDirty_RenderThread(sceneProxyId);
        //    (void)this;
        //});
    }

    void PrimitiveComponent::UpdateTransform(const Core::Mat4& newLocalTransform) {
        // Update on GameThread
        _localTransform = newLocalTransform;
        UpdateWorldTransform();

        // Send a lightweight transform update to RenderThread if proxy exists.
        PrimitiveSceneProxy* proxy = _pendingSceneProxy;
        if (proxy) {
            // Capture a snapshot of world transform for the RenderThread.
            Core::Mat4 worldSnapshot = _worldTransform;
            //RenderCommand::Enqueue([Proxy, WorldSnapshot]() {
            //    // In real engine: Proxy->SetWorldTransform(WorldSnapshot);
            //    (void)Proxy; (void)WorldSnapshot;
            //});
        }
    }

    void PrimitiveComponent::OnTransformChanged() {
        // Default: nothing. Derived classes may override to react to transform changes.
        _worldTransform = _localTransform = GetComponentTransform();
        _bounds = CalcBounds(_worldTransform);
    }

    void PrimitiveComponent::UpdateWorldTransform() {
        // For this lightweight example assume LocalTransform == WorldTransform.
        // In a real engine, compose with parent transforms here.

        OnTransformChanged();
    }

}