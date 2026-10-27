// StaticMeshProxy.cpp
#include "StaticMeshProxy.h"
#include "SceneView.h"
#include "StaticMeshComponent.h"
#include "Transform.hpp"
#include <cstring>
namespace Engine {


StaticMeshProxy::StaticMeshProxy(const StaticMeshComponent* inComponent):m_MeshComponent(inComponent)
{
    _localToWorld = inComponent->GetComponentTransform();
    _worldToLocal = Core::Inverse(_localToWorld);
    _proxyBounds = inComponent->GetBounds();
}

StaticMeshProxy::~StaticMeshProxy() {}

} // namespace Engine
