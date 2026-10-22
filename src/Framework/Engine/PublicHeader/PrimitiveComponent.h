// PrimitiveComponent.h
// Lightweight GameThread-side primitive description for renderer integration.
#pragma once
#include "EngineExport.h"
#include <cstdint>
#include <vector>
#include <typeinfo>
#include "BoxSphereBounds.h"
#include "TypeIDCast.h"
#include "SceneComponent.h"

namespace Engine {
	// Minimal placeholder types. Replace with engine math types in real project.
	struct FTransform { /* placeholder: implement transform math in engine */ };

	class SceneInterface;
	class PrimitiveSceneProxy;
	class UMaterialInterface;
	struct ViewInfo;

	enum class EMobility : uint8_t { Static = 0, Stationary = 1, Movable = 2 };

	// PrimitiveComponent: a lightweight, GameThread-only description of a renderable
	// primitive. It does NOT own GPU/RHI resources. To render, it creates a SceneProxy
	// snapshot which is enqueued to the RenderThread.
	class ENGINE_API PrimitiveComponent : public SceneComponent {
	public:
		PrimitiveComponent();
		virtual ~PrimitiveComponent();
		
		// ------------------ Spatial (GameThread only) ------------------
	protected:
		Core::Mat4 _localTransform;          // local transform
		Core::Mat4 _worldTransform;          // cached world transform (derived)
		Core::BoxSphereBounds _bounds;            // bounds used for frustum culling
		EMobility _mobility;                 // mobility hint

		// ------------------ Render state (GameThread only) ------------------
		bool _bVisible;                      // visible / hidden

		// ------------------ Scene linkage (GameThread only bookkeeping) ------------------

		PrimitiveSceneProxy* _pendingSceneProxy;      // bookkeeping pointer on GameThread (RenderThread owns proxy)

		// Debug
		const char* _debugName;

	public:
		// ------------------ Registration / lifecycle ------------------
		// Register/unregister with the scene. Must be called on GameThread.
		virtual void RegisterComponentWithScene(SceneInterface* scene);
		virtual void UnregisterComponentFromScene();

		// ------------------ SceneProxy management (virtual, override in derived) ------------------
		// Create a SceneProxy snapshot on GameThread. Returned pointer will be passed
		// to the RenderThread which takes ownership and manages its lifetime.
		virtual PrimitiveSceneProxy* CreateSceneProxy() const;

		// Request the SceneProxy be destroyed on RenderThread. Should enqueue deletion.
		virtual void DestroySceneProxy(PrimitiveSceneProxy* proxy) const;

		// Mark that render-related state changed (material/geometry/flags) and needs
		// update on the RenderThread.
		virtual void MarkRenderStateDirty();

		// Update transform from GameThread and notify RenderThread (lightweight update).
		virtual void UpdateTransform(const Core::Mat4& newLocalTransform);

		// Called when transform / bounds were updated on GameThread.
		virtual void OnTransformChanged() override;

		virtual const Core::BoxSphereBounds& GetBounds() const = 0;
		// Calculate bounds (derived must implement to provide frustum culling bounds).
		virtual Core::BoxSphereBounds CalcBounds(const Core::Mat4& localToWorld) = 0;

		// Quick GameThread-only visibility query.
		bool IsVisible() const;

		// Convenience: recompute WorldTransform/Bounds and call OnTransformUpdated().
		void UpdateWorldTransform();

		// Disallow copy (components are unique handles)
		PrimitiveComponent(const PrimitiveComponent&) = delete;
		PrimitiveComponent& operator=(const PrimitiveComponent&) = delete;
	};


	// Notes:
	// - PrimitiveComponent is GameThread-only. Do not touch its members from RenderThread.
	// - SceneProxy must be a RenderThread-only, immutable (or RenderThread-controlled) object
	//   that contains a snapshot of everything needed to render (transform, bounds, material IDs, flags).
	// - All cross-thread interactions must be performed via an explicit RenderCommand queue.
}